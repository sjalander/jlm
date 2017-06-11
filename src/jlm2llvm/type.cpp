/*
 * Copyright 2017 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include <jlm/jlm2llvm/type.hpp>

#include <typeindex>
#include <unordered_map>

namespace jlm {
namespace jlm2llvm {

static inline llvm::Type *
convert_integer_type(const jive::base::type & type, llvm::LLVMContext & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const jive::bits::type*>(&type));
	auto & t = *static_cast<const jive::bits::type*>(&type);

	return llvm::Type::getIntNTy(ctx, t.nbits());
}

static inline llvm::Type *
convert_function_type(const jive::base::type & type, llvm::LLVMContext & ctx)
{
	JLM_DEBUG_ASSERT(dynamic_cast<const jive::fct::type*>(&type));
	auto & t = *static_cast<const jive::fct::type*>(&type);

	using namespace llvm;

	std::vector<Type*> ats;
	for (size_t n = 0; n < t.narguments()-1; n++)
		ats.push_back(convert_type(t.argument_type(n), ctx));

	JLM_DEBUG_ASSERT(t.nresults() == 1 || t.nresults() == 2);
	auto rt = t.nresults() == 1 ? llvm::Type::getVoidTy(ctx) : convert_type(t.result_type(0), ctx);

	return FunctionType::get(rt, ats, false);
}

llvm::Type *
convert_type(const jive::base::type & type, llvm::LLVMContext & ctx)
{
	static std::unordered_map<
		std::type_index
	, std::function<llvm::Type*(const jive::base::type&, llvm::LLVMContext&)>
	> map({
	  {std::type_index(typeid(jive::bits::type)), convert_integer_type}
	, {std::type_index(typeid(jive::fct::type)), convert_function_type}
	});

	JLM_DEBUG_ASSERT(map.find(std::type_index(typeid(type))) != map.end());
	return map[std::type_index(typeid(type))](type, ctx);
}

}}