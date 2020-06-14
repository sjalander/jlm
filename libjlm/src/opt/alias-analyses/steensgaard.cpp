/*
 * Copyright 2020 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include <jive/rvsdg/graph.h>
#include <jive/rvsdg/node.h>
#include <jive/rvsdg/structural-node.h>
#include <jive/rvsdg/traverser.h>

#include <jlm/ir/rvsdg-module.hpp>
#include <jlm/ir/types.hpp>
#include <jlm/ir/operators.hpp>
#include <jlm/opt/alias-analyses/pointsto-graph.hpp>
#include <jlm/opt/alias-analyses/steensgaard.hpp>
#include <jlm/util/strfmt.hpp>

/*
	FIXME: to be removed again
*/
#include <iostream>

namespace jlm {
namespace aa {

/**
* FIXME: Some documentation
*/
class location {
public:
	virtual
	~location()
	{}

	constexpr
	location(bool unknown)
	: unknown_(unknown)
	, pointsto_(nullptr)
	{}

	location(const location &) = delete;

	location(location &&) = delete;

	location &
	operator=(const location &) = delete;

	location &
	operator=(location &&) = delete;

	virtual std::string
	debug_string() const noexcept = 0;

	bool
	unknown() const noexcept
	{
		return unknown_;
	}

	location *
	pointsto() const noexcept
	{
		return pointsto_;
	}

	void
	set_pointsto(location * l) noexcept
	{
		JLM_DEBUG_ASSERT(l != nullptr);

		pointsto_ = l;
		pointsto_->unknown_ = unknown_;
	}

private:
	bool unknown_;
	location * pointsto_;
};

class regloc final : public location {
public:
	constexpr
	regloc(const jive::output * output, bool unknown = false)
	: location(unknown)
	, output_(output)
	{}

	const jive::output *
	output() const noexcept
	{
		return output_;
	}

	virtual std::string
	debug_string() const noexcept override
	{
		auto node = output_->node();
		auto index = output_->index();

		if (jive::is<jive::simple_op>(node)) {
			auto nodestr = node->operation().debug_string();
			auto outputstr = output_->type().debug_string();
			return strfmt(nodestr, ":", index, "[" + outputstr + "]");
		}

		if (is_lambda_cv(output_)) {
			auto dbgstr = output_->region()->node()->operation().debug_string();
			return strfmt(dbgstr, ":cv:", index);
		}

		if (is_lambda_argument(output_)) {
			auto dbgstr = output_->region()->node()->operation().debug_string();
			return strfmt(dbgstr, ":arg:", index);
		}

		if (is_gamma_argument(output_)) {
			auto dbgstr = output_->region()->node()->operation().debug_string();
			return strfmt(dbgstr, ":arg", index);
		}

		if (is_theta_argument(output_)) {
			auto dbgstr = output_->region()->node()->operation().debug_string();
			return strfmt(dbgstr, ":arg", index);
		}

		if (is_theta_output(output_)) {
			auto dbgstr = output_->node()->operation().debug_string();
			return strfmt(dbgstr, ":out", index);
		}

		if (is_gamma_output(output_)) {
			auto dbgstr = output_->node()->operation().debug_string();
			return strfmt(dbgstr, ":out", index);
		}

		if (is_import(output_)) {
			auto imp = static_cast<const jive::impport*>(&output_->port());
			return strfmt("imp:", imp->name());
		}

		return strfmt(output_->node()->operation().debug_string(), ":", index);
	}

	static std::unique_ptr<location>
	create(const jive::output * output, bool unknown)
	{
		return std::unique_ptr<location>(new regloc(output, unknown));
	}

private:
	const jive::output * output_;
};

/** \brief FIXME: write documentation
*
*/
class memloc final : public location {
public:
	virtual
	~memloc()
	{}

	constexpr
	memloc(const jive::node * node)
	: location(false)
	, node_(node)
	{}

	const jive::node *
	node() const noexcept
	{
		return node_;
	}

	virtual std::string
	debug_string() const noexcept override
	{
		return node_->operation().debug_string();
	}

	static std::unique_ptr<location>
	create(const jive::node * node)
	{
		return std::unique_ptr<location>(new memloc(node));
	}

private:
	const jive::node * node_;
};

class anyloc final : public location {
public:
	anyloc()
	: location(false)
	{}

	virtual std::string
	debug_string() const noexcept override
	{
		return "ANY";
	}

	static std::unique_ptr<location>
	create()
	{
		return std::unique_ptr<location>(new anyloc());
	}
};

/* locationmap class */

locationset::~locationset() = default;

locationset::locationset()
: any_(nullptr)
{
	any_ = create_any();	
}

void
locationset::clear()
{
	map_.clear();
	djset_.clear();
	locations_.clear();
	any_ = create_any();
}

jlm::aa::location *
locationset::create_any()
{
	locations_.push_back(anyloc::create());
	auto location = locations_.back().get();

	djset_.insert(location);

	return location;
}

jlm::aa::location *
locationset::insert(const jive::output * output, bool unknown)
{
	JLM_DEBUG_ASSERT(lookup(output) == nullptr);

	locations_.push_back(regloc::create(output, unknown));
	auto location = locations_.back().get();

	map_[output] = location;
	djset_.insert(location);

	return location;
}

jlm::aa::location *
locationset::insert(const jive::node * node)
{
	locations_.push_back(memloc::create(node));
	auto location = locations_.back().get();
	djset_.insert(location);

	return location;
}

jlm::aa::location *
locationset::lookup(const jive::output * output)
{
	auto it = map_.find(output);
	return it == map_.end() ? nullptr : it->second;
}

jlm::aa::location *
locationset::find_or_insert(const jive::output * output)
{
	if (auto location = lookup(output))
		return find(location);

	return insert(output, false);
}

jlm::aa::location *
locationset::find(jlm::aa::location * l) const
{
	return djset_.find(l)->value();
}


jlm::aa::location *
locationset::find(const jive::output * output)
{
	auto loc = lookup(output);
	JLM_DEBUG_ASSERT(loc != nullptr);

	return djset_.find(loc)->value();
}

jlm::aa::location *
locationset::merge(jlm::aa::location * l1, jlm::aa::location * l2)
{
	return djset_.merge(l1, l2)->value();
}
/*
jlm::aa::location *
locationset::merge(const jive::output * o1, const jive::output * o2)
{
	auto l1 = find_or_insert(o1);
	auto l2 = find_or_insert(o2);
	return djset_.merge((l1), (l2))->value();
}
*/
std::string
locationset::to_dot() const
{
	auto dot_node = [](const locdjset::set & set)
	{
		std::string label;
		for (auto & l : set) {
			/*
				FIXME: We should mark the root element of the set with [*] and only
				print its points_to attribute, as this is the only important one.
			*/
			auto unknownstr = l->unknown() ? "{U}" : "";
			auto ptstr = strfmt("{pt:", (intptr_t)l->pointsto(), "}");
			auto locstr = strfmt((intptr_t)l, " : ", l->debug_string());
			label += strfmt(locstr, unknownstr, ptstr, "\\n");
		}

		return strfmt("{ ", (intptr_t)&set, " [label = \"", label, "\"]; }");
	};

	//FIXME: This should be const location &
	auto dot_edge = [&](const locdjset::set & set, const locdjset::set & ptset)
	{
		return strfmt((intptr_t)&set, " -> ", (intptr_t)&ptset);
	};

	std::string str;
	str.append("digraph ptg {\n");

	for (auto & set : djset_) {
		str += dot_node(set) + "\n";

		auto pt = set.value()->pointsto();
		if (pt != nullptr) {
			auto ptset = djset_.find(pt);
			str += dot_edge(set, *ptset) + "\n";
		}
	}

	str.append("}\n");

	return str;
}

/* steensgaard class */

steensgaard::~steensgaard() = default;

/*
	FIXME: I would like this to be somewhere else.
*/
static void
show_dot(const std::string & dotstr, FILE * out)
{
	fputs(dotstr.c_str(), out);
	fflush(out);
}

location *
steensgaard::join(location * x, location * y)
{
	/*
		FIXME: I believe we can remove all those if-statements
		from the beginning of the function. The return value
		of join should be nowhere used.
	*/
	if (x == nullptr)
		return y;

	if (y == nullptr)
		return x;

	if (x == y)
		return x;

	auto rootx = lset_.find(x);
	auto rooty = lset_.find(y);
	auto tmp = lset_.merge(rootx, rooty);

	if (auto root = join(rootx->pointsto(), rooty->pointsto()))
		tmp->set_pointsto(root);

	return tmp;
}

void
steensgaard::perform_aa(const jive::graph & graph)
{
	/* handle imports */
	auto region = graph.root();
	for (size_t n = 0; n < region->narguments(); n++) {
		auto argument = region->argument(n);
		if (!is<ptrtype>(argument->type()))
			continue;

		lset_.insert(argument, true);	
	}

	perform_aa(*graph.root());
}

void
steensgaard::perform_aa(jive::region & region)
{
	using namespace jive;

	topdown_traverser traverser(&region);
	for (auto & node : traverser) {
		if (auto smpnode = dynamic_cast<const simple_node*>(node)) {
			perform_aa(*smpnode);
			continue;
		}

		JLM_DEBUG_ASSERT(is<structural_op>(node));
		auto structnode = static_cast<const structural_node*>(node);
		perform_aa(*structnode);
	}
}


void
steensgaard::perform_aa(const jive::simple_node & node)
{
	namespace placeholder = std::placeholders;

	auto & operation = node.operation();

	/*
		FIXME: I would like to make this map static, but this might conflict with std::bind and this.
	  How about using lambdas?
	*/
	std::unordered_map<
		std::type_index
	, std::function<void(const jive::simple_node&)>> nodes
	({
	  {typeid(alloca_op),
	    std::bind(&steensgaard::perform_aa_alloca, this, placeholder::_1)}
	, {typeid(load_op),
			std::bind(&steensgaard::perform_aa_load, this, placeholder::_1)}
	, {typeid(store_op),
			std::bind(&steensgaard::perform_aa_store, this, placeholder::_1)}
	, {typeid(call_op),
			std::bind(&steensgaard::perform_aa_call, this, placeholder::_1)}
	, {typeid(getelementptr_op),
			std::bind(&steensgaard::perform_aa_gep, this, placeholder::_1)}
	, {typeid(bitcast_op),
			std::bind(&steensgaard::perform_aa_bitcast, this, placeholder::_1)}
	, {typeid(bits2ptr_op),
			std::bind(&steensgaard::perform_aa_bits2ptr, this, placeholder::_1)}
	, {typeid(ptr_constant_null_op),
			std::bind(&steensgaard::perform_aa_ptr_constant_null, this, placeholder::_1)}
	, {typeid(undef_constant_op),
	    std::bind(&steensgaard::perform_aa_undef, this, placeholder::_1)}
	});

	auto it = nodes.find(typeid(operation));
	if (it != nodes.end()) {
		nodes[typeid(operation)](node);
		return;
	}

	/*
		Ensure that we really took care of all pointer-producing instructions
	*/
	for (size_t n = 0; n < node.noutputs(); n++) {
		if (is<ptrtype>(node.output(n)->type()))
			JLM_ASSERT("We should have never reached this statement" && 0);
	}
}

void
steensgaard::perform_aa_alloca(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(jive::is<alloca_op>(&node));

	auto ptr = lset_.find_or_insert(node.output(0));
	ptr->set_pointsto(lset_.insert(&node));
}

void
steensgaard::perform_aa_load(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(jive::is<load_op>(&node));

	if (!dynamic_cast<const ptrtype*>(&node.output(0)->type()))
		return;

	auto address = lset_.find_or_insert(node.input(0)->origin());
	auto result = lset_.find_or_insert(node.output(0));

	if (address->pointsto() == nullptr) {
		address->set_pointsto(result);
		return;
	}

	join(result, address->pointsto());
}

void
steensgaard::perform_aa_store(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(jive::is<store_op>(&node));

	if (!dynamic_cast<const ptrtype*>(&node.input(1)->origin()->type()))
		return;

	auto address = lset_.find_or_insert(node.input(0)->origin());
	auto value = lset_.find_or_insert(node.input(1)->origin());

	if (address->pointsto() == nullptr) {
		address->set_pointsto(value);
		return;
	}

	join(address->pointsto(), value);
}

void
steensgaard::perform_aa_call(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(jive::is<call_op>(&node));

	auto handle_direct_call = [&](const jive::simple_node & call, const lambda_node & lambda)
	{
		/*
			FIXME: What about varargs
		*/

		/* handle call node arguments */
		auto lambdaarg = lambda.begin_argument();
		JLM_DEBUG_ASSERT(lambda.narguments() == call.ninputs()-1);
		for (size_t n = 1; n < call.ninputs(); n++, lambdaarg++) {
			auto callarg = call.input(n)->origin();
			if (!is<ptrtype>(callarg->type()))
				continue;

			auto callargloc = lset_.find_or_insert(callarg);
			auto lambdaargloc = lset_.find_or_insert(lambdaarg.argument());
			join(callargloc, lambdaargloc);
		}

		/* handle call node results */
		auto subregion = lambda.subregion();
		JLM_DEBUG_ASSERT(subregion->nresults() == node.noutputs());
		for (size_t n = 0; n < call.noutputs(); n++) {
			auto callres = call.output(n);
			if (!is<ptrtype>(callres->type()))
				continue;

			auto callresloc = lset_.find_or_insert(callres);
			auto lambdaresloc = lset_.find_or_insert(subregion->result(n)->origin());
			join(callresloc, lambdaresloc);
		}
	};

	auto handle_indirect_call = [&](const jive::simple_node & call)
	{
		/*
			FIXME: What about varargs
		*/

		/* handle call node arguments */
		for (size_t n = 1; n < call.ninputs(); n++) {
			auto callarg = call.input(n)->origin();
			if (!is<ptrtype>(callarg->type()))
				continue;

			auto callargloc = lset_.find_or_insert(callarg);
			if (callargloc->pointsto() == nullptr) {
				callargloc->set_pointsto(lset_.any());
				return;
			}

			join(callargloc->pointsto(), lset_.any());
		}

		/* handle call node results */
		for (size_t n = 0; n < call.noutputs(); n++) {
			auto callres = call.output(n);
			if (!is<ptrtype>(callres->type()))
				continue;

			auto callresloc = lset_.find_or_insert(callres);
			if (callresloc->pointsto() == nullptr) {
				callresloc->set_pointsto(lset_.any());
				return;
			}

			join(callresloc->pointsto(), lset_.any());
		}
	};

	/*
		FIXME: What about exported functions? The arguments of a lambda need to be treated
		conservatively and must be treated as aliasing.
	*/

	if (auto lambda = is_direct_call(node)) {
		handle_direct_call(node, *lambda);
		return;
	}

	handle_indirect_call(node);
}

void
steensgaard::perform_aa_gep(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(jive::is<getelementptr_op>(&node));

	auto base = lset_.find_or_insert(node.input(0)->origin());
	auto value = lset_.find_or_insert(node.output(0));

	join(base, value);
}

void
steensgaard::perform_aa_bitcast(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(jive::is<bitcast_op>(&node));

	auto operand = lset_.find_or_insert(node.input(0)->origin());
	auto result = lset_.find_or_insert(node.output(0));

	join(operand, result);
}

void
steensgaard::perform_aa_bits2ptr(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(jive::is<bits2ptr_op>(&node));

	/*
		FIXME: I do not know how to handle this case gracefully yet.
	*/
//	JLM_ASSERT(0 && "Not implemented yet.");
}

void
steensgaard::perform_aa_ptr_constant_null(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(jive::is<ptr_constant_null_op>(&node));

	lset_.find_or_insert(node.output(0));
}

void
steensgaard::perform_aa_undef(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(jive::is<undef_constant_op>(&node));

	/*
		FIXME: check whether this implementation is correct
	*/
	lset_.find_or_insert(node.output(0));
}

void
steensgaard::perform_aa(const jive::structural_node & node)
{
	static std::unordered_map<
		std::type_index
	, std::function<void(steensgaard*, const jive::structural_node&)>> nodes
	({
	  {typeid(lambda_op), [](steensgaard * stg, const jive::structural_node & node){
			stg->perform_aa(*static_cast<const lambda_node*>(&node)); }}
	, {typeid(delta_op), [](steensgaard * stg, const jive::structural_node & node){
			stg->perform_aa(*static_cast<const delta_node*>(&node)); }}
	, {typeid(jive::gamma_op), [](steensgaard * stg, const jive::structural_node & node){
			stg->perform_aa(*static_cast<const jive::gamma_node*>(&node)); }}
	, {typeid(jive::theta_op), [](steensgaard * stg, const jive::structural_node & node){
			stg->perform_aa(*static_cast<const jive::theta_node*>(&node)); }}
	});

	auto & op = node.operation();
	JLM_DEBUG_ASSERT(nodes.find(typeid(op)) != nodes.end());
	nodes[typeid(op)](this, node);
}

void
steensgaard::perform_aa(const lambda_node & lambda)
{
	/* handle context variables */
	for (auto it = lambda.begin_cv(); it != lambda.end_cv(); it++) {
		if (!is<ptrtype>(it->type()))
			continue;

		auto origin = lset_.find_or_insert(it->origin());
		auto argument = lset_.find_or_insert(it.argument());
		join(origin, argument);
	}

	/* handle function arguments */
	for (auto it = lambda.begin_argument(); it != lambda.end_argument(); it++) {
		if (!is<ptrtype>(it->type()))
			continue;

		lset_.insert(it.argument(), is_exported(lambda));
	}

	perform_aa(*lambda.subregion());

	auto ptr = lset_.find_or_insert(lambda.output(0));
	ptr->set_pointsto(lset_.insert(&lambda));
}

void
steensgaard::perform_aa(const delta_node & delta)
{
	/* handle context variables */
	for (auto & input : delta) {
		if (!is<ptrtype>(input.type()))
			continue;

		auto origin = lset_.find_or_insert(input.origin());
		auto argument = lset_.find_or_insert(input.arguments.first());
		join(origin, argument);
	}

	perform_aa(*delta.subregion());

	auto deltaptr = lset_.find_or_insert(delta.output(0));
	auto valueptr = lset_.find_or_insert(delta.subregion()->result(0)->origin());
	auto loc = lset_.insert(&delta);
	deltaptr->set_pointsto(loc);
	valueptr->set_pointsto(loc);
}

void
steensgaard::perform_aa(const jive::phi_node & phi)
{
	/*
		FIXME: provide implementation
	*/
	JLM_ASSERT("Not implemented yet." && 0);
}

void
steensgaard::perform_aa(const jive::gamma_node & node)
{
	/* handle entry variables */
	for (auto ev = node.begin_entryvar(); ev != node.end_entryvar(); ev++) {
		if (!is<ptrtype>(ev->type()))
			continue;

		auto originloc = lset_.find(ev->origin());
		for (auto & argument : *ev) {
			auto argumentloc = lset_.insert(&argument, false);
			join(argumentloc, originloc);
		}
	}

	/* handle subregions */
	for (size_t n = 0; n < node.nsubregions(); n++)
		perform_aa(*node.subregion(n));

	/* handle exit variables */
	for (auto ex = node.begin_exitvar(); ex != node.end_exitvar(); ex++) {
		if (!is<ptrtype>(ex->type()))
			continue;

		auto outputloc = lset_.insert(ex.output(), false);
		for (auto & result : *ex) {
			auto resultloc = lset_.find(result.origin());
			join(outputloc, resultloc);
		}
	}
}

void
steensgaard::perform_aa(const jive::theta_node & theta)
{
	for (auto lv : theta) {
		if (!is<ptrtype>(lv->type()))
			continue;

		auto originloc = lset_.find(lv->input()->origin());
		auto argumentloc = lset_.insert(lv->argument(), false);

		join(argumentloc, originloc);
	}

	perform_aa(*theta.subregion());

	for (auto lv : theta) {
		if (!is<ptrtype>(lv->type()))
			continue;

		auto originloc = lset_.find(lv->result()->origin());
		auto argumentloc = lset_.find(lv->argument());
		auto outputloc = lset_.insert(lv, false);

		join(originloc, argumentloc);
		join(originloc, outputloc);
	}
}

void
steensgaard::run(rvsdg_module & module, const stats_descriptor & sd)
{
	reset_state();

//	populate(*module.graph());

	perform_aa(*module.graph());

//	show_dot(lset_.to_dot(), stdout);

	auto ptg = construct_ptg(lset_);

	/*
		FIXME: add RVSDG encoding
	*/
}

std::unique_ptr<ptg>
steensgaard::run(const rvsdg_module & module)
{
	reset_state();

	perform_aa(*module.graph());
//	show_dot(lset_.to_dot(), stdout);
	auto ptg = construct_ptg(lset_);

	return ptg;
}

std::unique_ptr<ptg>
steensgaard::construct_ptg(const locationset & lset) const
{
	auto ptg = ptg::create();

	/*
		Create points-to graph nodes
	*/
	std::unordered_map<location*, ptg::node*> map;
	std::unordered_map<const disjointset<location*>::set*, std::vector<ptg::memnode*>> allocators;
	for (auto & set : lset) {
		for (auto & loc : set) {
			/*
				FIXME: we still need to handle this case
			*/
			if (dynamic_cast<anyloc*>(loc))
				continue;

			if (auto regloc = dynamic_cast<jlm::aa::regloc*>(loc)) {
				map[loc] = ptg::regnode::create(ptg.get(), regloc->output());
				continue;
			}

			if (auto memloc = dynamic_cast<jlm::aa::memloc*>(loc)) {
				auto node = ptg::allocator::create(ptg.get(), memloc->node());
				allocators[&set].push_back(node);
				map[loc] = node;
				continue;
			}
		}
	}

	/*
		Create points-to graph edges
	*/
	for (auto & set : lset) {
		for (auto & loc : set) {
			if (loc->unknown())
				map[loc]->add_edge(&ptg->memunknown());
			/*
				FIXME: we still need to handle this case
			*/
			if (dynamic_cast<anyloc*>(loc))
				continue;

			auto pt = set.value()->pointsto();
			if (pt == nullptr)
				continue;

			auto & ptset = lset.set(pt);
			for (auto & allocator : allocators[&ptset])
				map[loc]->add_edge(allocator);
		}
	}

	return ptg;
}

void
steensgaard::reset_state()
{
	lset_.clear();
}

}}
