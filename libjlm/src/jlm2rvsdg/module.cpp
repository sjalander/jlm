/*
 * Copyright 2015 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include <jlm/jlm2rvsdg/module.hpp>
#include <jlm/jlm2rvsdg/restructuring.hpp>

#include <jlm/ir/aggregation.hpp>
#include <jlm/ir/annotation.hpp>
#include <jlm/ir/basic-block.hpp>
#include <jlm/ir/cfg-structure.hpp>
#include <jlm/ir/ipgraph.hpp>
#include <jlm/ir/ipgraph-module.hpp>
#include <jlm/ir/operators.hpp>
#include <jlm/ir/rvsdg-module.hpp>
#include <jlm/ir/ssa.hpp>
#include <jlm/ir/tac.hpp>

#include <jlm/util/stats.hpp>
#include <jlm/util/time.hpp>

#include <jive/arch/address.hpp>
#include <jive/arch/addresstype.hpp>
#include <jive/arch/dataobject.hpp>
#include <jive/arch/memlayout-simple.hpp>
#include <jive/types/bitstring/constant.hpp>
#include <jive/types/bitstring/type.hpp>
#include <jive/types/float.hpp>
#include <jive/types/function.hpp>
#include <jive/rvsdg/binary.hpp>
#include <jive/rvsdg/control.hpp>
#include <jive/rvsdg/gamma.hpp>
#include <jive/rvsdg/phi.hpp>
#include <jive/rvsdg/region.hpp>
#include <jive/rvsdg/theta.hpp>
#include <jive/rvsdg/type.hpp>

#include <cmath>
#include <stack>

static std::string source_filename;

static inline jive::output *
create_undef_value(jive::region * region, const jive::type & type)
{
	/*
		We currently cannot create an undef_constant_op of control type,
		as the operator expects a valuetype. Control type is a state
		type. Use a control constant as a poor man's replacement instead.
	*/
	if (auto ct = dynamic_cast<const jive::ctltype*>(&type))
		return jive_control_constant(region, ct->nalternatives(), 0);

	JLM_ASSERT(dynamic_cast<const jive::valuetype*>(&type));
	jlm::undef_constant_op op(*static_cast<const jive::valuetype*>(&type));
	return jive::simple_node::create_normalized(region, op, {})[0];
}

namespace jlm {

class cfrstat final : public stat {
public:
	virtual
	~cfrstat()
	{}

	cfrstat(const std::string & filename, const std::string & fctname)
	: nnodes_(0)
	, fctname_(fctname)
	, filename_(filename)
	{}

	void
	start(const jlm::cfg & cfg) noexcept
	{
		nnodes_ = cfg.nnodes();
		timer_.start();
	}

	void
	end() noexcept
	{
		timer_.stop();
	}

	virtual std::string
	to_str() const override
	{
		return strfmt("CFRTIME ", filename_, " ", fctname_, " ", nnodes_, " ", timer_.ns());
	}

private:
	size_t nnodes_;
	jlm::timer timer_;
	std::string fctname_;
	std::string filename_;
};

class aggregation_stat final : public stat {
public:
	virtual
	~aggregation_stat()
	{}

	aggregation_stat(const std::string & filename, const std::string & fctname)
	: nnodes_(0)
	, fctname_(fctname)
	, filename_(filename)
	{}

	void
	start(const jlm::cfg & cfg) noexcept
	{
		nnodes_ = cfg.nnodes();
		timer_.start();
	}

	void
	end() noexcept
	{
		timer_.stop();
	}

	virtual std::string
	to_str() const override
	{
		return strfmt("AGGREGATIONTIME ", filename_, " ", fctname_, " ", nnodes_, " ", timer_.ns());
	}

private:
	size_t nnodes_;
	jlm::timer timer_;
	std::string fctname_;
	std::string filename_;
};

class annotation_stat final : public stat {
public:
	virtual
	~annotation_stat()
	{}

	annotation_stat(const std::string & filename, const std::string & fctname)
	: ntacs_(0)
	, fctname_(fctname)
	, filename_(filename)
	{}

	void
	start(const aggnode & node) noexcept
	{
		ntacs_ = jlm::ntacs(node);
		timer_.start();
	}

	void
	end() noexcept
	{
		timer_.stop();
	}

	virtual std::string
	to_str() const override
	{
		return strfmt("ANNOTATIONTIME ", filename_, " ", fctname_, " ", ntacs_, " ", timer_.ns());
	}

private:
	size_t ntacs_;
	jlm::timer timer_;
	std::string fctname_;
	std::string filename_;
};

class rvsdg_construction_stat final : public stat {
public:
	virtual
	~rvsdg_construction_stat()
	{}

	rvsdg_construction_stat(const jlm::filepath & filename)
	: ntacs_(0)
	, nnodes_(0)
	, filename_(filename)
	{}

	void
	start(const ipgraph_module & im) noexcept
	{
		ntacs_ = jlm::ntacs(im);
		timer_.start();
	}

	void
	end(const jive::graph & graph) noexcept
	{
		timer_.stop();
		nnodes_ = jive::nnodes(graph.root());
	}

	virtual std::string
	to_str() const override
	{
		return strfmt("RVSDGCONSTRUCTION ", filename_.to_str(), " ",
			ntacs_, " ", nnodes_, " ", timer_.ns());
	}

private:
	size_t ntacs_;
	size_t nnodes_;
	jlm::timer timer_;
	jlm::filepath filename_;
};

typedef std::unordered_map<const variable*, jive::output*> vmap;

class scoped_vmap final {
public:
	inline
	~scoped_vmap()
	{
		pop_scope();
		JLM_ASSERT(nscopes() == 0);
	}

	inline
	scoped_vmap(const ipgraph_module & im, jive::region * region)
	: module_(im)
	{
		push_scope(region);
	}

	inline size_t
	nscopes() const noexcept
	{
		JLM_ASSERT(vmaps_.size() == regions_.size());
		return vmaps_.size();
	}

	inline jlm::vmap &
	vmap(size_t n) noexcept
	{
		JLM_ASSERT(n < nscopes());
		return *vmaps_[n];
	}

	inline jlm::vmap &
	vmap() noexcept
	{
		JLM_ASSERT(nscopes() > 0);
		return vmap(nscopes()-1);
	}

	inline jive::region *
	region(size_t n) noexcept
	{
		JLM_ASSERT(n < nscopes());
		return regions_[n];
	}

	inline jive::region *
	region() noexcept
	{
		JLM_ASSERT(nscopes() > 0);
		return region(nscopes()-1);
	}

	inline void
	push_scope(jive::region * region)
	{
		vmaps_.push_back(std::make_unique<jlm::vmap>());
		regions_.push_back(region);
	}

	inline void
	pop_scope()
	{
		vmaps_.pop_back();
		regions_.pop_back();
	}

	const ipgraph_module &
	module() const noexcept
	{
		return module_;
	}

private:
	const ipgraph_module & module_;
	std::vector<std::unique_ptr<jlm::vmap>> vmaps_;
	std::vector<jive::region*> regions_;
};

static void
convert_assignment(const jlm::tac & tac, jive::region * region, jlm::vmap & vmap)
{
	JLM_ASSERT(is<assignment_op>(tac.operation()));
	vmap[tac.operand(0)] = vmap[tac.operand(1)];
}

static void
convert_select(const jlm::tac & tac, jive::region * region, jlm::vmap & vmap)
{
	JLM_ASSERT(is<select_op>(tac.operation()));
	JLM_ASSERT(tac.noperands() == 3 && tac.nresults() == 1);

	auto op = jive::match_op(1, {{1, 1}}, 0, 2);
	auto predicate = jive::simple_node::create_normalized(region, op, {vmap[tac.operand(0)]})[0];

	auto gamma = jive::gamma_node::create(predicate, 2);
	auto ev1 = gamma->add_entryvar(vmap[tac.operand(2)]);
	auto ev2 = gamma->add_entryvar(vmap[tac.operand(1)]);
	auto ex = gamma->add_exitvar({ev1->argument(0), ev2->argument(1)});
	vmap[tac.result(0)] = ex;
}

static void
convert_branch(const jlm::tac & tac, jive::region * region, jlm::vmap & vmap)
{
	JLM_ASSERT(is<branch_op>(tac.operation()));
}

static void
convert_tac(const jlm::tac & tac, jive::region * region, jlm::vmap & vmap)
{
	static std::unordered_map<
		std::type_index,
		std::function<void(const jlm::tac&, jive::region*, jlm::vmap&)>
	> map({
	  {std::type_index(typeid(assignment_op)), convert_assignment}
	, {std::type_index(typeid(select_op)), convert_select}
	, {std::type_index(typeid(branch_op)), convert_branch}
	});

	auto & op = tac.operation();
	if (map.find(typeid(op)) != map.end())
		return map[typeid(op)](tac, region, vmap);

	std::vector<jive::output*> operands;
	for (size_t n = 0; n < tac.noperands(); n++) {
		JLM_ASSERT(vmap.find(tac.operand(n)) != vmap.end());
		operands.push_back(vmap[tac.operand(n)]);
	}

	auto results = jive::simple_node::create_normalized(region, static_cast<const jive::simple_op&>(
		tac.operation()), operands);

	JLM_ASSERT(results.size() == tac.nresults());
	for (size_t n = 0; n < tac.nresults(); n++)
		vmap[tac.result(n)] = results[n];
}

static void
convert_basic_block(const taclist & bb, jive::region * region, jlm::vmap & vmap)
{
	for (const auto & tac: bb)
		convert_tac(*tac, region, vmap);
}

static void
convert_node(
	const aggnode & node,
	const demandmap & dm,
	const jlm::function_node & function,
	lambda::node * lambda,
	scoped_vmap & svmap);

static void
convert_entry_node(
	const aggnode & node,
	const demandmap & dm,
	const jlm::function_node & function,
	lambda::node * lambda,
	scoped_vmap & svmap)
{
	JLM_ASSERT(is<entryaggnode>(&node));
	auto en = static_cast<const entryaggnode*>(&node);
	auto ds = dm.at(&node).get();

	svmap.push_scope(lambda->subregion());

	auto & pvmap = svmap.vmap(svmap.nscopes()-2);
	auto & vmap = svmap.vmap();

	/* add arguments */
	JLM_ASSERT(en->narguments() == lambda->nfctarguments());
	for (size_t n = 0; n < en->narguments(); n++) {
		auto jlmarg = en->argument(n);
		auto fctarg = lambda->fctargument(n);

		vmap[jlmarg] = fctarg;
		fctarg->set_attributes(jlmarg->attributes());
	}

	/* add dependencies and undefined values */
	for (const auto & v : ds->top) {
		if (pvmap.find(v) != pvmap.end()) {
			vmap[v] = lambda->add_ctxvar(pvmap[v]);
		} else {
			auto value = create_undef_value(lambda->subregion(), v->type());
			JLM_ASSERT(value);
			vmap[v] = value;
		}
	}
}

static void
convert_exit_node(
	const aggnode & node,
	const demandmap & dm,
	const jlm::function_node & function,
	lambda::node * lambda,
	scoped_vmap & svmap)
{
	JLM_ASSERT(is<exitaggnode>(&node));
	auto xn = static_cast<const exitaggnode*>(&node);

	std::vector<jive::output*> results;
	for (const auto & result : *xn) {
		JLM_ASSERT(svmap.vmap().find(result) != svmap.vmap().end());
		results.push_back(svmap.vmap()[result]);
	}

	svmap.pop_scope();
	lambda->finalize(results);
}

static void
convert_block_node(
	const aggnode & node,
	const demandmap & dm,
	const jlm::function_node & function,
	lambda::node * lambda,
	scoped_vmap & svmap)
{
	JLM_ASSERT(is<blockaggnode>(&node));
	auto & bb = static_cast<const blockaggnode*>(&node)->tacs();
	convert_basic_block(bb, svmap.region(), svmap.vmap());
}

static void
convert_linear_node(
	const aggnode & node,
	const demandmap & dm,
	const jlm::function_node & function,
	lambda::node * lambda,
	scoped_vmap & svmap)
{
	JLM_ASSERT(is<linearaggnode>(&node));

	for (const auto & child : node)
		convert_node(child, dm, function, lambda, svmap);
}

static void
convert_branch_node(
	const aggnode & node,
	const demandmap & dm,
	const jlm::function_node & function,
	lambda::node * lambda,
	scoped_vmap & svmap)
{
	JLM_ASSERT(is<branchaggnode>(&node));
	JLM_ASSERT(is<linearaggnode>(node.parent()));

	auto split = node.parent()->child(node.index()-1);
	while (!is<blockaggnode>(split))
		split = split->child(split->nchildren()-1);
	auto & sb = dynamic_cast<const blockaggnode*>(split)->tacs();

	JLM_ASSERT(is<branch_op>(sb.last()->operation()));
	auto predicate = svmap.vmap()[sb.last()->operand(0)];
	auto gamma = jive::gamma_node::create(predicate, node.nchildren());

	/* add entry variables */
	auto & ds = dm.at(&node);
	std::unordered_map<const variable*, jive::gamma_input*> evmap;
	for (const auto & v : ds->top) {
		JLM_ASSERT(svmap.vmap().find(v) != svmap.vmap().end());
		evmap[v] = gamma->add_entryvar(svmap.vmap()[v]);
	}
	
	/* convert branch cases */
	std::unordered_map<const variable*, std::vector<jive::output*>> xvmap;
	JLM_ASSERT(gamma->nsubregions() == node.nchildren());
	for (size_t n = 0; n < gamma->nsubregions(); n++) {
		svmap.push_scope(gamma->subregion(n));
		for (const auto & pair : evmap)
			svmap.vmap()[pair.first] = pair.second->argument(n);

		convert_node(*node.child(n), dm, function, lambda, svmap);

		for (const auto & v : ds->bottom) {
			JLM_ASSERT(svmap.vmap().find(v) != svmap.vmap().end());
			xvmap[v].push_back(svmap.vmap()[v]);
		}
		svmap.pop_scope();
	}

	/* add exit variables */
	for (const auto & v : ds->bottom) {
		JLM_ASSERT(xvmap.find(v) != xvmap.end());
		svmap.vmap()[v] = gamma->add_exitvar(xvmap[v]);
	}
}

static void
convert_loop_node(
	const aggnode & node,
	const demandmap & dm,
	const jlm::function_node & function,
	lambda::node * lambda,
	scoped_vmap & svmap)
{
	JIVE_DEBUG_ASSERT(is<loopaggnode>(&node));
	auto parent = svmap.region();

	auto theta = jive::theta_node::create(parent);

	svmap.push_scope(theta->subregion());
	auto & vmap = svmap.vmap();
	auto & pvmap = svmap.vmap(svmap.nscopes()-2);

	/* add loop variables */
	auto ds = dm.at(&node).get();
	JLM_ASSERT(ds->top == ds->bottom);
	std::unordered_map<const variable*, jive::theta_output*> lvmap;
	for (const auto & v : ds->top) {
		jive::output * value = nullptr;
		if (pvmap.find(v) == pvmap.end()) {
			value = create_undef_value(parent, v->type());
			JLM_ASSERT(value);
			pvmap[v] = value;
		} else {
			value = pvmap[v];
		}
		lvmap[v] = theta->add_loopvar(value);
		vmap[v] = lvmap[v]->argument();
	}

	/* convert loop body */
	JLM_ASSERT(node.nchildren() == 1);
	convert_node(*node.child(0), dm, function, lambda, svmap);

	/* update loop variables */
	for (const auto & v : ds->top) {
		JLM_ASSERT(vmap.find(v) != vmap.end());
		JLM_ASSERT(lvmap.find(v) != lvmap.end());
		lvmap[v]->result()->divert_to(vmap[v]);
	}

	/* find predicate */
	auto lblock = node.child(0);
	while (lblock->nchildren() != 0)
		lblock = lblock->child(lblock->nchildren()-1);
	JLM_ASSERT(is<blockaggnode>(lblock));
	auto & bb = static_cast<const blockaggnode*>(lblock)->tacs();
	JLM_ASSERT(is<branch_op>(bb.last()->operation()));
	auto predicate = bb.last()->operand(0);

	/* update variable map */
	JLM_ASSERT(vmap.find(predicate) != vmap.end());
	theta->set_predicate(vmap[predicate]);
	svmap.pop_scope();
	for (const auto & v : ds->bottom) {
		JLM_ASSERT(pvmap.find(v) != pvmap.end());
		pvmap[v] = lvmap[v];
	}
}

static void
convert_node(
	const aggnode & node,
	const demandmap & dm,
	const jlm::function_node & function,
	lambda::node * lambda,
	scoped_vmap & svmap)
{
	static std::unordered_map<
		std::type_index,
		std::function<void(
			const aggnode&,
			const demandmap&,
			const function_node&,
			lambda::node*,
			scoped_vmap&)
		>
	> map ({
	  {typeid(entryaggnode), convert_entry_node}, {typeid(exitaggnode), convert_exit_node}
	, {typeid(blockaggnode), convert_block_node}, {typeid(linearaggnode), convert_linear_node}
	, {typeid(branchaggnode), convert_branch_node}, {typeid(loopaggnode), convert_loop_node}
	});

	JLM_ASSERT(map.find(typeid(node)) != map.end());
	map[typeid(node)](node, dm, function, lambda, svmap);
}

static jive::output *
convert_cfg(
	const jlm::function_node & function,
	jive::region * region,
	scoped_vmap & svmap,
	const stats_descriptor & sd)
{
	auto cfg = function.cfg();

	destruct_ssa(*cfg);
	straighten(*cfg);
	purge(*cfg);

	{
		cfrstat stat(source_filename, function.name());
		stat.start(*cfg);
		restructure(cfg);
		straighten(*cfg);
		stat.end();
		if (sd.print_cfr_time)
			sd.print_stat(stat);
	}

	std::unique_ptr<aggnode> root;
	{
		aggregation_stat stat(source_filename, function.name());
		stat.start(*cfg);
		root = aggregate(*cfg);
//		std::cout << function.name() << " " << root->nnodes() << "\n";
//		jlm::print(*root, stdout);
		aggnode::normalize(*root);
//		std::cout << function.name() << " " << root->nnodes() << "\n";
//		jlm::print(*root, stdout);

		stat.end();
		if (sd.print_aggregation_time)
			sd.print_stat(stat);
	}

	demandmap dm;
	{
		annotation_stat stat(source_filename, function.name());
		stat.start(*root);
//		std::cout << "ANNOTATION " << function.name() << "\n";
		dm = annotate(*root);
//		jlm::print(*root, dm, stdout);
		stat.end();
		if (sd.print_annotation_time)
			sd.print_stat(stat);
	}

	auto & name = function.name();
	auto & fcttype = function.fcttype();
	auto & linkage = function.linkage();
	auto & attributes = function.attributes();
	auto lambda = lambda::node::create(svmap.region(), fcttype, name, linkage, attributes);

//	std::cout << "CONVERSION " << function.name() << "\n";
	convert_node(*root, dm, function, lambda, svmap);

	return lambda->output();
}

static jive::output *
construct_lambda(
	const ipgraph_node * node,
	jive::region * region,
	scoped_vmap & svmap,
	const stats_descriptor & sd)
{
	JLM_ASSERT(dynamic_cast<const function_node*>(node));
	auto & function = *static_cast<const function_node*>(node);

	if (function.cfg() == nullptr) {
		jlm::impport port(function.type(), function.name(), function.linkage());
		return region->graph()->add_import(port);
	}

	return convert_cfg(function, region, svmap, sd);
}

static jive::output *
convert_initialization(const data_node_init & init, jive::region * region, scoped_vmap & svmap)
{
	auto & vmap = svmap.vmap();
	for (const auto & tac : init.tacs())
		convert_tac(*tac, region, vmap);

	return vmap[init.value()];
}

static jive::output *
convert_data_node(
	const jlm::ipgraph_node * node,
	jive::region * region,
	scoped_vmap & svmap,
	const stats_descriptor&)
{
	JLM_ASSERT(dynamic_cast<const data_node*>(node));
	auto n = static_cast<const data_node*>(node);
	auto init = n->initialization();
	auto & m = svmap.module();

	/* data node without initialization */
	if (!init) {
		jlm::impport port(n->type(), n->name(), n->linkage());
		return region->graph()->add_import(port);
	}

	/* data node with initialization */
	jlm::delta_builder db;
	auto r = db.begin(region, n->type(), n->name(), n->linkage(), n->constant());
	auto & pv = svmap.vmap();
	svmap.push_scope(r);

	/* add dependencies */
	for (const auto & dp : *node) {
		auto v = m.variable(dp);
		JLM_ASSERT(pv.find(v) != pv.end());
		auto argument = db.add_dependency(pv[v]);
		svmap.vmap()[v] = argument;
	}

	auto data = db.end(convert_initialization(*init, r, svmap));
	svmap.pop_scope();

	return data;
}

static void
handle_scc(
	const std::unordered_set<const jlm::ipgraph_node*> & scc,
	jive::graph * graph,
	scoped_vmap & svmap,
	const stats_descriptor & sd)
{
	auto & m = svmap.module();

	static std::unordered_map<
		std::type_index,
		std::function<jive::output*(
		  const ipgraph_node*
		, jive::region*
		, scoped_vmap&
		, const stats_descriptor&)>
	> map({
	  {typeid(data_node), convert_data_node}
	, {typeid(function_node), construct_lambda}
	});

	if (scc.size() == 1 && !(*scc.begin())->is_selfrecursive()) {
		auto & node = *scc.begin();
		JLM_ASSERT(map.find(typeid(*node)) != map.end());
		auto output = map[typeid(*node)](node, graph->root(), svmap, sd);

		auto v = m.variable(node);
		JLM_ASSERT(v);
		svmap.vmap()[v] = output;
		if (is_externally_visible(node->linkage()))
			graph->add_export(output, {output->type(), v->name()});
	} else {
		jive::phi::builder pb;
		pb.begin(graph->root());
		svmap.push_scope(pb.subregion());

		auto & pvmap = svmap.vmap(svmap.nscopes()-2);
		auto & vmap = svmap.vmap();

		/* add recursion variables */
		std::unordered_map<const variable*, jive::phi::rvoutput*> recvars;
		for (const auto & node : scc) {
			auto rv = pb.add_recvar(node->type());
			auto v = m.variable(node);
			JLM_ASSERT(v);
			vmap[v] = rv->argument();
			JLM_ASSERT(recvars.find(v) == recvars.end());
			recvars[v] = rv;
		}

		/* add dependencies */
		for (const auto & node : scc) {
			for (const auto & dep : *node) {
				auto v = m.variable(dep);
				JLM_ASSERT(v);
				if (recvars.find(v) == recvars.end())
					vmap[v] = pb.add_ctxvar(pvmap[v]);
			}
		}

		/* convert SCC nodes */
		for (const auto & node : scc) {
			auto output = map[typeid(*node)](node, pb.subregion(), svmap, sd);
			recvars[m.variable(node)]->set_rvorigin(output);
		}

		svmap.pop_scope();
		pb.end();

		/* add phi outputs */
		for (const auto & node : scc) {
			auto v = m.variable(node);
			auto value = recvars[v];
			svmap.vmap()[v] = value;
			if (is_externally_visible(node->linkage()))
				graph->add_export(value, {value->type(), v->name()});
		}
	}
}

static std::unique_ptr<rvsdg_module>
convert_module(const ipgraph_module & im, const stats_descriptor & sd)
{
	auto rm = rvsdg_module::create(im.source_filename(), im.target_triple(), im.data_layout());
	auto graph = rm->graph();

	auto nf = graph->node_normal_form(typeid(jive::operation));
	nf->set_mutable(false);

	/* FIXME: we currently cannot handle flattened_binary_op in jlm2llvm pass */
	jive::binary_op::normal_form(graph)->set_flatten(false);

	scoped_vmap svmap(im, graph->root());

	/* convert ipgraph nodes */
	auto sccs = im.ipgraph().find_sccs();
	for (const auto & scc : sccs)
		handle_scc(scc, graph, svmap, sd);

	return rm;
}

std::unique_ptr<rvsdg_module>
construct_rvsdg(const ipgraph_module & im, const stats_descriptor & sd)
{
	source_filename = im.source_filename().to_str();

	rvsdg_construction_stat stat(im.source_filename());

	stat.start(im);
	auto rm = convert_module(im, sd);
	stat.end(*rm->graph());

	if (sd.print_rvsdg_construction)
		sd.print_stat(stat);

	return rm;
}

}
