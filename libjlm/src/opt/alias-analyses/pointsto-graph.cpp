/*
 * Copyright 2020 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include <jlm/ir/types.hpp>
#include <jlm/ir/operators.hpp>
#include <jlm/ir/rvsdg-module.hpp>
#include <jlm/opt/alias-analyses/pointsto-graph.hpp>
#include <jlm/opt/dne.hpp>
#include <jlm/util/strfmt.hpp>

#include <jive/arch/addresstype.h>
#include <jive/rvsdg/node.h>
#include <jive/rvsdg/region.h>
#include <jive/rvsdg/structural-node.h>
#include <jive/rvsdg/traverser.h>

#include <typeindex>
#include <unordered_map>

namespace jlm {
namespace aa {

/* points-to graph */

ptg::ptg()
{
	memunknown_ = std::unique_ptr<jlm::aa::ptg::unknown>(new ptg::unknown(this));
}

ptg::iterator
ptg::begin()
{
	return iterator(memnodes_.begin(), memnodes_.end(), regnodes_.begin());
}

ptg::constiterator
ptg::begin() const
{
	return constiterator(memnodes_.begin(), memnodes_.end(), regnodes_.begin());
}

ptg::iterator
ptg::end()
{
	return iterator(memnodes_.end(), memnodes_.end(), regnodes_.end());
}

ptg::constiterator
ptg::end() const
{
	return constiterator(memnodes_.end(), memnodes_.end(), regnodes_.end());
}

ptg::node *
ptg::add(std::unique_ptr<ptg::allocator> node)
{
	auto tmp = node.get();
	memnodes_[node->node()] = std::move(node);

	return tmp;
}

ptg::node *
ptg::add(std::unique_ptr<ptg::regnode> node)
{
	auto tmp = node.get();
	regnodes_[node->output()] = std::move(node);

	return tmp;
}

std::string
ptg::to_dot(const jlm::aa::ptg & ptg)
{
	auto shape = [](const ptg::node & node) {
		static std::unordered_map<std::type_index, std::string> shapes({
		  {typeid(allocator), "box"}
		, {typeid(regnode),   "oval"}
		, {typeid(unknown),   "box"}
		});

		if (shapes.find(typeid(node)) != shapes.end())
			return shapes[typeid(node)];

		throw error("Unknown points-to graph node type.");
	};

	auto nodestring = [&](const ptg::node & node) {
		return strfmt("{ ", (intptr_t)&node, " ["
			, "label = \"", node.debug_string(), "\" "
			, "shape = \"", shape(node), "\"]; }\n");
	};

	auto edgestring = [](const ptg::node & node, const ptg::node & target)
	{
		return strfmt((intptr_t)&node, " -> ", (intptr_t)&target, "\n");
	};

	std::string dot("digraph ptg {\n");
	for (auto & node : ptg) {
		dot += nodestring(node);
		for (auto & target : node)
			dot += edgestring(node, target);
	}
	dot += nodestring(ptg.memunknown());
	dot += "}\n";

	return dot;	
}

/* Points-to graph encoding into RVSDG */

/** FIXME: write documentation
*/
class allocatormap final {
public:
	/* FIXME: delete constructors and assignment operators */

	/* FIXME: Add creator */
	bool
	contains(const ptg::memnode * node) const noexcept
	{
		return states_.find(node) != states_.end();
	}

	jive::output *
	state(const ptg::memnode * node) const noexcept
	{
		JLM_DEBUG_ASSERT(contains(node));
		return states_.at(node);
	}

	std::vector<jive::output*>
	states(const std::vector<const ptg::allocator*> & nodes)
	{
		std::vector<jive::output*> states;
		for (auto & node : nodes)
			states.push_back(state(node));

		return states;
	}

	void
	insert(
		const ptg::allocator * node,
		jive::output * state)
	{
		JLM_DEBUG_ASSERT(!contains(node));
		JLM_DEBUG_ASSERT(is<jive::memtype>(state->type()));

		states_[node] = state;
	}

	void
	insert(
		const std::vector<const ptg::allocator*> & nodes,
		const std::vector<jive::output*> & states)
	{
		JLM_DEBUG_ASSERT(nodes.size() == states.size());

		for (size_t n = 0; n < nodes.size(); n++)
			insert(nodes[n], states[n]);
	}

	void
	replace(
		const ptg::allocator * node,
		jive::output * state)
	{
		JLM_DEBUG_ASSERT(contains(node));
		JLM_DEBUG_ASSERT(is<jive::memtype>(state->type()));

		states_[node] = state;
	}

	void
	replace(
		const std::vector<const ptg::allocator*> nodes,
		const std::vector<jive::output*> & states)
	{
		JLM_DEBUG_ASSERT(nodes.size() == states.size());
		for (size_t n = 0; n < nodes.size(); n++)
			replace(nodes[n], states[n]);
	}

private:
	std::unordered_map<const ptg::memnode*, jive::output*> states_;
};


/** FIXME: write documentation
*/
class statemap final {
public:
	statemap(const jlm::aa::ptg & ptg)
	: ptg_(&ptg)
	{}

	statemap(const statemap&) = delete;

	statemap(statemap&&) = delete;

	statemap &
	operator=(const statemap&) = delete;

	statemap &
	operator=(statemap&&) = delete;

	const jlm::aa::ptg &
	ptg() const noexcept
	{
		return *ptg_;
	}

	void
	insert(
		const ptg::allocator * node,
		jive::output * state)
	{
		allocmap(*state->region()).insert(node, state);
	}

	void
	insert(
		const std::vector<const ptg::allocator*> & nodes,
		const std::vector<jive::output*> & states)
	{
		JLM_DEBUG_ASSERT(nodes.size() == states.size());
		JLM_DEBUG_ASSERT(!nodes.empty());

		allocmap(*states[0]->region()).insert(nodes, states);
	}

	void
	replace(
		const jive::output * output,
		const std::vector<jive::output*> & states)
	{
		auto nodes = memnodes(output);
		allocmap(*output->region()).replace(nodes, states);
	}

	void
	replace(
		const ptg::allocator * node,
		jive::output * state)
	{
		allocmap(*state->region()).replace(node, state);
	}

	std::vector<jive::output*>
	states(const jive::output * output) noexcept
	{
		auto nodes = memnodes(output);
		return states(*output->region(), nodes);
	}

	std::vector<jive::output*>
	states(
		const jive::region & region,
		const std::vector<const ptg::allocator*> & nodes)
	{
		return allocmap(region).states(nodes);
	}

	std::vector<const ptg::allocator*>
	memnodes(const jive::output * output)
	{
		JLM_DEBUG_ASSERT(is<ptrtype>(output->type()));

		if (memnodes_.find(output) != memnodes_.end())
			return memnodes_[output];

		auto & regnode = ptg_->find(output);
		auto nodes = ptg::regnode::allocators(regnode);
		memnodes_[output] = nodes;

		return nodes;
	}

private:
	allocatormap &
	allocmap(const jive::region & region) noexcept
	{
		if (amaps_.find(&region) == amaps_.end())
			amaps_[&region] = allocatormap();

		return amaps_[&region];
	}

	const jlm::aa::ptg * ptg_;
	std::unordered_map<const jive::region*, allocatormap> amaps_;
	std::unordered_map<const jive::output*, std::vector<const ptg::allocator*>> memnodes_;
};


static std::vector<const ptg::allocator*>
lambda_argument_memnodes(
	const lambda_node & lambda,
	statemap & smap)
{
	auto subregion = lambda.subregion();

	std::vector<const ptg::allocator*> memnodes;
	std::unordered_set<const ptg::allocator*> seen;
	/*
		FIXME: I would like to have some proper function argument iterators
	*/
	for (size_t n = 0; n < subregion->narguments(); n++) {
		auto argument = subregion->argument(n);
		if (argument->input() != nullptr)
			continue;

		if (!is<ptrtype>(argument->type()))
			continue;

		auto nodes = smap.memnodes(argument);
		for (auto & node : nodes) {
			if (seen.find(node) == seen.end()) {
				memnodes.push_back(node);
				seen.insert(node);
			}
		}
	}

	return memnodes;
}

static void
encode(jive::region & region, statemap & smap);

static jive::argument *
lambda_memstate_argument(const jlm::lambda_node & lambda)
{
	auto subregion = lambda.subregion();

	/*
		FIXME: This function should be part of the lambda node.
	*/
	for (size_t n = 0; n < subregion->narguments(); n++) {
		auto argument = subregion->argument(n);
		if (is<jive::memtype>(argument->type()))
			return argument;
	}

	JLM_ASSERT(0 && "This should have never happened!");
}

static jive::result *
lambda_memstate_result(const jlm::lambda_node & lambda)
{
	auto subregion = lambda.subregion();

	/*
		FIXME: This function should be part of the lambda node.
	*/
	for (size_t n = 0; n < subregion->nresults(); n++) {
		auto result = subregion->result(n);
		if (is<jive::memtype>(result->type()))
			return result;
	}

	JLM_ASSERT(0 && "This should have never happened!");
}

static void
encode(
	const lambda_node & lambda,
	statemap & smap)
{
	auto handle_lambda_entry = [](const lambda_node & lambda, statemap & smap)
	{
		auto memstate = lambda_memstate_argument(lambda);

		/*
			FIXME: We need to check whether all call sides can be resolved, i.e. all call sides are
			direct calls. We need to be way more conservative with indirect calls.
		*/

		auto memnodes = lambda_argument_memnodes(lambda, smap);
		/*
			The lambda has no pointer arguments. Nothing needs to be done.
		*/
		if (memnodes.empty())
			return;

		auto lambdastates = memstatemux_op::create_split(memstate, memnodes.size());
		smap.insert(memnodes, lambdastates);

		/*
			FIXME: We need to handle context variables.
		*/
		//JLM_DEBUG_ASSERT(lambda.ninputs() == 0);
	};

	auto handle_lambda_exit = [](const lambda_node & lambda, statemap & smap)
	{
		/*
			FIXME: What about lambdas returning pointers.
		*/

		auto memnodes = lambda_argument_memnodes(lambda, smap);
		/*
			The lambda has no pointer arguments. Nothing needs to be done.
		*/
		if (memnodes.empty())
			return;

		auto states = smap.states(*lambda.subregion(), memnodes);
		auto state = memstatemux_op::create_merge(states);

		auto memresult = lambda_memstate_result(lambda);
		memresult->divert_to(state);
	};

	/*
		FIXME: Replace memstatemux with dedicated lambda context end state node
	*/
	handle_lambda_entry(lambda, smap);
	encode(*lambda.subregion(), smap);
	handle_lambda_exit(lambda, smap);
}

static void
encode(jive::structural_node & node, statemap & smap)
{
	static std::unordered_map<
		std::type_index,
		std::function<void(jive::node&, statemap&)>
	> nodes({
		{typeid(lambda_op), [](jive::node& node, statemap & smap){
			encode(*static_cast<jlm::lambda_node*>(&node), smap); }}
	});

	auto & op = node.operation();
	JLM_DEBUG_ASSERT(nodes.find(typeid(op)) != nodes.end());
	nodes[typeid(op)](node, smap);
}

static void
encode_alloca(const jive::simple_node & node, statemap & smap)
{
	JLM_DEBUG_ASSERT(is<alloca_op>(&node));

	/*
		FIXME: I do not like the dynamic_cast here.
	*/
	auto memnode = dynamic_cast<const ptg::allocator*>(&smap.ptg().find(&node));
	smap.insert(memnode, node.output(1));
}

static void
encode_load(
	const jive::simple_node & node,
	statemap & smap)
{
	JLM_DEBUG_ASSERT(is<load_op>(&node));
	auto & op = *static_cast<const load_op*>(&node.operation());

	auto address = node.input(0)->origin();
	auto instates = smap.states(address);

	auto outputs = load_op::create(address, instates, op.alignment());
	node.output(0)->divert_users(outputs[0]);

	smap.replace(address, {std::next(outputs.begin()), outputs.end()});
}

static void
encode_store(
	const jive::simple_node & node,
	statemap & smap)
{
	JLM_DEBUG_ASSERT(is<store_op>(&node));
	auto & op = *static_cast<const store_op*>(&node.operation());

	auto address = node.input(0)->origin();
	auto value = node.input(1)->origin();
	auto instates = smap.states(address);

	auto outstates = store_op::create(address, value, instates, op.alignment());

	smap.replace(address, outstates);
}

static std::unordered_set<const ptg::memnode*>
call_argument_memnodes(
	const jive::simple_node & node,
	const jlm::aa::ptg & ptg)
{
	JLM_DEBUG_ASSERT(is<call_op>(&node));

	std::unordered_set<const ptg::memnode*> memnodes;
	for (size_t n = 0; n < node.ninputs(); n++) {
		auto origin = node.input(n)->origin();

		if (!is<ptrtype>(origin->type()))
			continue;

		auto & regnode = ptg.find(origin);
		/* FIXME: We need to cast the targets to memnodes. Then we can simply to
		insert(regnode.begin(), regnode.end()) */
		for (auto & target : regnode) {
			auto memnode = static_cast<const ptg::memnode*>(&target);
			memnodes.insert(memnode);
		}
	}

	return memnodes;
}

static jive::input *
call_memstate_input(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(is<call_op>(&node));

	/*
		FIXME: This function should be part of the call node.
	*/
	for (size_t n = 0; n < node.ninputs(); n++) {
		auto input = node.input(n);
		if (is<jive::memtype>(input->type()))
			return input;
	}

	JLM_ASSERT(0 && "This should have never happened!");
}

static jive::output *
call_memstate_output(const jive::simple_node & node)
{
	JLM_DEBUG_ASSERT(is<call_op>(&node));

	/*
		FIXME: This function should be part of the call node.
	*/
	for (size_t n = 0; n < node.noutputs(); n++) {
		auto output = node.output(n);
		if (is<jive::memtype>(output->type()))
			return output;
	}

	JLM_ASSERT(0 && "This should have never happened!");
}

static void
encode_call(
	const jive::simple_node & node,
	statemap & smap)
{
	JLM_DEBUG_ASSERT(is<call_op>(&node));
	auto region = node.region();

	auto lambda = is_direct_call(node);
	/*
		FIMXE: We also need to handle indirect calls.
	*/
	JLM_DEBUG_ASSERT(lambda != nullptr);

	auto lambdanodes = lambda_argument_memnodes(*lambda, smap);
	auto callnodes = call_argument_memnodes(node, smap.ptg());

	std::vector<jive::output*> instates;
	for (auto & memnode : lambdanodes) {
		if (callnodes.find(memnode) == callnodes.end()) {
			auto state = undef_constant_op::create(region, jive::memtype::instance());
			instates.push_back(state);
			continue;
		}

		auto state = smap.states(*region, {memnode})[0];
		instates.push_back(state);
	}

	auto state = memstatemux_op::create_merge(instates);
	auto meminput = call_memstate_input(node);
	meminput->divert_to(state);


	auto memoutput = call_memstate_output(node);
	auto outstates = memstatemux_op::create_split(memoutput, lambdanodes.size());

	for (size_t n = 0; n < lambdanodes.size(); n++) {
		auto memnode = lambdanodes[n];

		if (callnodes.find(memnode) == callnodes.end())
			continue;

		smap.replace(memnode, outstates[n]);
	}
}

static void
encode(const jive::simple_node & node, statemap & smap)
{
	static std::unordered_map<
		std::type_index
	, std::function<void(const jive::simple_node&, statemap&)>
	> nodes({
		{typeid(alloca_op), encode_alloca}
		/* FIXME: malloc */
	, {typeid(load_op), encode_load}, {typeid(store_op), encode_store}
	, {typeid(call_op), encode_call}
	});

	auto & op = node.operation();
	if (nodes.find(typeid(op)) == nodes.end())
		return;

	nodes[typeid(op)](node, smap);
}

static void
encode(jive::region & region, statemap & smap)
{
	using namespace jive;

	topdown_traverser traverser(&region);
	for (auto & node : traverser) {
		if (auto simpnode = dynamic_cast<const simple_node*>(node)) {
			encode(*simpnode, smap);
			continue;
		}

		JLM_DEBUG_ASSERT(is<structural_op>(node));
		auto structnode = static_cast<structural_node*>(node);
		encode(*structnode, smap);
	}
}

void
ptg::encode(const jlm::aa::ptg & ptg, rvsdg_module & module)
{
	statemap smap(ptg);
	/*
		FIXME: handle imports
	*/
	jlm::aa::encode(*module.graph()->root(), smap);

	/*
		Remove all nodes that were redenered dead throughout the encoding.
	*/
	jlm::dne dne;
	dne.run(*module.graph()->root());
}

/* points-to graph node */

ptg::node::~node()
{}

ptg::node::iterator
ptg::node::begin()
{
	return iterator(targets_.begin());
}

ptg::node::constiterator
ptg::node::begin() const
{
	return constiterator(targets_.begin());
}

ptg::node::iterator
ptg::node::end()
{
	return iterator(targets_.end());
}

ptg::node::constiterator
ptg::node::end() const
{
	return constiterator(targets_.end());
}

void
ptg::node::add_edge(ptg::node * target)
{
	if (ptg() != target->ptg())
		throw jlm::error("Points-to graph nodes are not in the same graph.");

	targets_.insert(target);
}

/* points-to graph register node */

ptg::regnode::~regnode()
{}

std::string
ptg::regnode::debug_string() const
{
	auto node = output()->node();

	if (node != nullptr)
		return strfmt(node->operation().debug_string(), ":o", output()->index());

	node = output()->region()->node();
	if (node != nullptr)
		return strfmt(node->operation().debug_string(), ":a", output()->index());

	return "REGNODE";
}

std::vector<const ptg::allocator*>
ptg::regnode::allocators(const ptg::regnode & node)
{
	/*
		FIXME: This function currently iterates through all pointstos of the regnode.
		Maybe we can be more efficient?
	*/
	std::vector<const ptg::allocator*> allocators;
	for (auto & target : node) {
		/*
			FIXME: I believe that it is only possible to point to allocator nodes. We could avoid
			this check altogether.
		*/
		if (auto allocator = dynamic_cast<const ptg::allocator*>(&target))
			allocators.push_back(allocator);
	}

	return allocators;
}

/* points-to graph memory node */

ptg::memnode::~memnode()
{}

/* points-to graph allocator node */

ptg::allocator::~allocator()
{}

std::string
ptg::allocator::debug_string() const
{
	return node()->operation().debug_string();
}

/* points-to graph unknown node */

ptg::unknown::~unknown()
{}

std::string
ptg::unknown::debug_string() const
{
	return "Unknown";
}

}}
