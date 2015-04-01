/*
 * Copyright 2014 2015 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include <jlm/common.hpp>
#include <jlm/construction/constant.hpp>
#include <jlm/construction/context.hpp>
#include <jlm/construction/jlm.hpp>
#include <jlm/construction/type.hpp>

#include <jlm/IR/basic_block.hpp>
#include <jlm/IR/clg.hpp>
#include <jlm/IR/operators.hpp>
#include <jlm/IR/tac.hpp>

#include <jive/arch/address.h>
#include <jive/arch/load.h>
#include <jive/arch/memorytype.h>
#include <jive/arch/store.h>
#include <jive/types/bitstring/arithmetic.h>
#include <jive/types/bitstring/comparison.h>
#include <jive/types/bitstring/constant.h>
#include <jive/types/bitstring/slice.h>
#include <jive/types/float/comparison.h>
#include <jive/vsdg/controltype.h>
#include <jive/vsdg/operators/match.h>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>

#include <typeindex>

namespace jlm {

const variable *
convert_value(
	const llvm::Value * v,
	const context & ctx)
{
	if (auto c = dynamic_cast<const llvm::Constant*>(v))
		return convert_constant(c, ctx.entry_block());

	return ctx.lookup_value(v);
}

/* instructions */

static void
convert_return_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::ReturnInst*>(i));
	const llvm::ReturnInst * instruction = static_cast<const llvm::ReturnInst*>(i);

	if (instruction->getReturnValue()) {
		const variable * value = convert_value(instruction->getReturnValue(), ctx);
		bb->append(jlm::assignment_op(ctx.result()->type()), {value}, {ctx.result()});
	}
}

static void
convert_branch_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::BranchInst*>(i));
	const llvm::BranchInst * instruction = static_cast<const llvm::BranchInst*>(i);

	if (instruction->isConditional()) {
		const variable * c = convert_value(instruction->getCondition(), ctx);
		bb->append(jive::match_op(dynamic_cast<const jive::bits::type&>(c->type()), {0}), {c});
	}
}

static void
convert_switch_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::SwitchInst*>(i));
	const llvm::SwitchInst * instruction = static_cast<const llvm::SwitchInst*>(i);

	JLM_DEBUG_ASSERT(bb->outedges().size() == instruction->getNumCases()+1);
	std::vector<uint64_t> constants(instruction->getNumCases());
	for (auto it = instruction->case_begin(); it != instruction->case_end(); it++) {
		JLM_DEBUG_ASSERT(it != instruction->case_default());
		constants[it.getCaseIndex()] = it.getCaseValue()->getZExtValue();
	}

	const jlm::variable * c = convert_value(instruction->getCondition(), ctx);
	bb->append(jive::match_op(dynamic_cast<const jive::bits::type&>(c->type()), constants), {c});
}

static void
convert_unreachable_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	/* Nothing needs to be done. */
}

static inline void
convert_icmp_instruction(
	const llvm::Instruction * instruction,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::ICmpInst*>(instruction));
	const llvm::ICmpInst * i = static_cast<const llvm::ICmpInst*>(instruction);

	/* FIXME: this unconditionally casts to integer type, take also care of other types */

	static std::map<
		const llvm::CmpInst::Predicate,
		std::unique_ptr<jive::operation>(*)(size_t)> map({
			{llvm::CmpInst::ICMP_SLT,	[](size_t nbits){jive::bits::slt_op op(nbits); return op.copy();}}
		,	{llvm::CmpInst::ICMP_ULT,	[](size_t nbits){jive::bits::ult_op op(nbits); return op.copy();}}
		,	{llvm::CmpInst::ICMP_SLE,	[](size_t nbits){jive::bits::sle_op op(nbits); return op.copy();}}
		,	{llvm::CmpInst::ICMP_ULE,	[](size_t nbits){jive::bits::ule_op op(nbits); return op.copy();}}
		,	{llvm::CmpInst::ICMP_EQ,	[](size_t nbits){jive::bits::eq_op op(nbits); return op.copy();}}
		,	{llvm::CmpInst::ICMP_NE,	[](size_t nbits){jive::bits::ne_op op(nbits); return op.copy();}}
		,	{llvm::CmpInst::ICMP_SGE,	[](size_t nbits){jive::bits::sge_op op(nbits); return op.copy();}}
		,	{llvm::CmpInst::ICMP_UGE,	[](size_t nbits){jive::bits::uge_op op(nbits); return op.copy();}}
		,	{llvm::CmpInst::ICMP_SGT,	[](size_t nbits){jive::bits::sgt_op op(nbits); return op.copy();}}
		, {llvm::CmpInst::ICMP_UGT,	[](size_t nbits){jive::bits::ugt_op op(nbits); return op.copy();}}
	});

	const jlm::variable * op1 = convert_value(i->getOperand(0), ctx);
	const jlm::variable * op2 = convert_value(i->getOperand(1), ctx);
	size_t nbits = static_cast<const llvm::IntegerType*>(i->getOperand(0)->getType())->getBitWidth();
	bb->append(*map[i->getPredicate()](nbits), {op1, op2}, {ctx.lookup_value(i)});
}

static void
convert_fcmp_instruction(
	const llvm::Instruction * instruction,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::FCmpInst*>(instruction));
	const llvm::FCmpInst * i = static_cast<const llvm::FCmpInst*>(instruction);

	/* FIXME: vector type is not yet supported */
	if (i->getType()->isVectorTy())
		JLM_DEBUG_ASSERT(0);

	/* FIXME: we currently don't have an operation for FCMP_ORD and FCMP_UNO, just use flt::eq_op */

	static std::map<
		const llvm::CmpInst::Predicate,
		std::unique_ptr<jive::operation>(*)()> map({
			{llvm::CmpInst::FCMP_OEQ, [](){jive::flt::eq_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_OGT, [](){jive::flt::gt_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_OGE, [](){jive::flt::ge_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_OLT, [](){jive::flt::lt_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_OLE, [](){jive::flt::le_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_ONE, [](){jive::flt::ne_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_ORD, [](){jive::flt::eq_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_UNO, [](){jive::flt::eq_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_UEQ, [](){jive::flt::eq_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_UGT, [](){jive::flt::gt_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_UGE, [](){jive::flt::ge_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_ULT, [](){jive::flt::lt_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_ULE, [](){jive::flt::le_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_UNE, [](){jive::flt::ne_op op; return op.copy();}}
		,	{llvm::CmpInst::FCMP_FALSE,
				[](){jive::bits::constant_op op(jive::bits::value_repr(1, 0)); return op.copy();}}
		,	{llvm::CmpInst::FCMP_TRUE,
				[](){jive::bits::constant_op op(jive::bits::value_repr(1, 1)); return op.copy();}}
	});

	std::vector<const variable*> operands;
	if (i->getPredicate() != llvm::CmpInst::FCMP_TRUE
	&& i->getPredicate() != llvm::CmpInst::FCMP_FALSE) {
		operands.push_back(convert_value(i->getOperand(0), ctx));
		operands.push_back(convert_value(i->getOperand(1), ctx));
	}

	bb->append(*map[i->getPredicate()](), operands, {ctx.lookup_value(i)});
}

static void
convert_load_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::LoadInst*>(i));
	const llvm::LoadInst * instruction = static_cast<const llvm::LoadInst*>(i);

	/* FIXME: handle volatile correctly */

	const variable * value = ctx.lookup_value(i);
	const variable * address = convert_value(instruction->getPointerOperand(), ctx);

	jive::addrload_op op({jive::mem::type()}, dynamic_cast<const jive::value::type&>(value->type()));
	bb->append(op, {address, ctx.state()}, {value});
}

static void
convert_store_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::StoreInst*>(i));
	const llvm::StoreInst * instruction = static_cast<const llvm::StoreInst*>(i);

	const variable * address = convert_value(instruction->getPointerOperand(), ctx);
	const variable * value = convert_value(instruction->getValueOperand(), ctx);

	jive::addrstore_op op({jive::mem::type()}, dynamic_cast<const jive::value::type&>(value->type()));
	bb->append(op, {address, value, ctx.state()}, {ctx.state()});
}

static void
convert_phi_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::PHINode*>(i));
	const llvm::PHINode * phi = static_cast<const llvm::PHINode*>(i);

	std::vector<const jlm::variable*> operands;
	for (auto edge : bb->inedges()) {
		const basic_block * tmp = static_cast<basic_block*>(edge->source());
		const llvm::BasicBlock * ib = ctx.lookup_basic_block(tmp);
		const jlm::variable * v = convert_value(phi->getIncomingValueForBlock(ib), ctx);
		operands.push_back(v);
	}

	JLM_DEBUG_ASSERT(operands.size() != 0);
	bb->append(phi_op(operands.size(), operands[0]->type()), operands, {ctx.lookup_value(phi)});
}

static void
convert_getelementptr_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::GetElementPtrInst*>(i));
	const llvm::GetElementPtrInst * instruction = static_cast<const llvm::GetElementPtrInst*>(i);

	const jlm::variable * base = convert_value(instruction->getPointerOperand(), ctx);
	for (auto idx = instruction->idx_begin(); idx != instruction->idx_end(); idx++) {
		const jlm::variable * offset = convert_value(idx->get(), ctx);
		const jive::value::type & basetype = dynamic_cast<const jive::value::type&>(base->type());
		const jive::bits::type & offsettype = dynamic_cast<const jive::bits::type&>(offset->type());
		jive::address::arraysubscript_op op(basetype, offsettype);
		base = bb->append(op, {base, offset}, {ctx.lookup_value(i)})->output(0);
	}
}

static void
convert_trunc_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::TruncInst*>(i));
	const llvm::TruncInst * instruction = static_cast<const llvm::TruncInst*>(i);

	const jlm::variable * op = convert_value(instruction->getOperand(0), ctx);
	size_t high = static_cast<const llvm::IntegerType*>(i->getType())->getBitWidth();
	bb->append(jive::bits::slice_op(dynamic_cast<const jive::bits::type&>(op->type()), 0, high),
		{op}, {ctx.lookup_value(i)});
}

static void
convert_call_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::CallInst*>(i));
	const llvm::CallInst * instruction = static_cast<const llvm::CallInst*>(i);

	llvm::Function * f = instruction->getCalledFunction();

	jlm::clg_node * caller = bb->cfg()->function();
	jlm::clg_node * callee = caller->clg().lookup_function(f->getName());
	JLM_DEBUG_ASSERT(callee != nullptr);
	caller->add_call(callee);

	std::vector<const jlm::variable*> arguments;
	for (size_t n = 0; n < instruction->getNumArgOperands(); n++)
		arguments.push_back(convert_value(instruction->getArgOperand(n), ctx));
	arguments.push_back(ctx.state());

	jive::fct::type type = dynamic_cast<jive::fct::type&>(*convert_type(f->getFunctionType()));

	std::vector<const jlm::variable*> results;
	if (type.nreturns() == 2)
		results.push_back(ctx.lookup_value(i));
	results.push_back(ctx.state());

	bb->append(jlm::apply_op(callee), arguments, results);
}

static void
convert_select_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::SelectInst*>(i));
	const llvm::SelectInst * instruction = static_cast<const llvm::SelectInst*>(i);

	const jlm::variable * condition = convert_value(instruction->getCondition(), ctx);
	const jlm::variable * tv = convert_value(instruction->getTrueValue(), ctx);
	const jlm::variable * fv = convert_value(instruction->getFalseValue(), ctx);
	bb->append(select_op(tv->type()), {condition, tv, fv}, {ctx.lookup_value(i)});
}

static inline void
convert_binary_operator(
	const llvm::Instruction * instruction,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::BinaryOperator*>(instruction));
	const llvm::BinaryOperator * i = static_cast<const llvm::BinaryOperator*>(instruction);

	/* FIXME: take care of floating point operations and vector type as well */

	static std::map<
		const llvm::Instruction::BinaryOps,
		std::unique_ptr<jive::operation>(*)(size_t)> map({
			{llvm::Instruction::Add,	[](size_t nbits){jive::bits::add_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::And,	[](size_t nbits){jive::bits::and_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::AShr,	[](size_t nbits){jive::bits::ashr_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::Sub,	[](size_t nbits){jive::bits::sub_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::UDiv,	[](size_t nbits){jive::bits::udiv_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::SDiv,	[](size_t nbits){jive::bits::sdiv_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::URem,	[](size_t nbits){jive::bits::umod_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::SRem,	[](size_t nbits){jive::bits::smod_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::Shl,	[](size_t nbits){jive::bits::shl_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::LShr,	[](size_t nbits){jive::bits::shr_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::Or,		[](size_t nbits){jive::bits::or_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::Xor,	[](size_t nbits){jive::bits::xor_op op(nbits); return op.copy();}}
		,	{llvm::Instruction::Mul,	[](size_t nbits){jive::bits::mul_op op(nbits); return op.copy();}}
	});

	const jlm::variable * op1 = convert_value(i->getOperand(0), ctx);
	const jlm::variable * op2 = convert_value(i->getOperand(1), ctx);
	size_t nbits = static_cast<const llvm::IntegerType*>(i->getType())->getBitWidth();
	bb->append(*map[i->getOpcode()](nbits), {op1, op2}, {ctx.lookup_value(i)});
}

static inline void
convert_alloca_instruction(
	const llvm::Instruction * instruction,
	basic_block * bb,
	const context & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const llvm::AllocaInst*>(instruction));
	const llvm::AllocaInst * i = static_cast<const llvm::AllocaInst*>(instruction);

	/* FIXME: the number of bytes is not correct */

	size_t nbytes = 4;
	bb->append(alloca_op(nbytes), {ctx.state()}, {ctx.lookup_value(i), ctx.state()});
}

typedef std::unordered_map<
		std::type_index,
		void(*)(const llvm::Instruction*, jlm::basic_block*, const context&)
	> instruction_map;

static instruction_map imap({
		{std::type_index(typeid(llvm::ReturnInst)), convert_return_instruction}
	, {std::type_index(typeid(llvm::BranchInst)), convert_branch_instruction}
	, {std::type_index(typeid(llvm::SwitchInst)), convert_switch_instruction}
	, {std::type_index(typeid(llvm::UnreachableInst)), convert_unreachable_instruction}
	, {std::type_index(typeid(llvm::BinaryOperator)), convert_binary_operator}
	, {std::type_index(typeid(llvm::ICmpInst)), convert_icmp_instruction}
	, {std::type_index(typeid(llvm::FCmpInst)), convert_fcmp_instruction}
	, {std::type_index(typeid(llvm::LoadInst)), convert_load_instruction}
	, {std::type_index(typeid(llvm::StoreInst)), convert_store_instruction}
	, {std::type_index(typeid(llvm::PHINode)), convert_phi_instruction}
	, {std::type_index(typeid(llvm::GetElementPtrInst)), convert_getelementptr_instruction}
	, {std::type_index(typeid(llvm::TruncInst)), convert_trunc_instruction}
	, {std::type_index(typeid(llvm::CallInst)), convert_call_instruction}
	, {std::type_index(typeid(llvm::SelectInst)), convert_select_instruction}
	, {std::type_index(typeid(llvm::AllocaInst)), convert_alloca_instruction}
});

void
convert_instruction(
	const llvm::Instruction * i,
	basic_block * bb,
	const context & ctx)
{
	/* FIXME: add an JLM_DEBUG_ASSERT here if an instruction is not present */
	if (imap.find(std::type_index(typeid(*i))) == imap.end())
		return;

	imap[std::type_index(typeid(*i))](i, bb, ctx);
}

}
