/*
 * Copyright 2020 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include "aa-tests.hpp"

#include <test-registry.hpp>

#include <jive/view.h>

#include <jlm/opt/alias-analyses/pointsto-graph.hpp>
#include <jlm/opt/alias-analyses/steensgaard.hpp>
#include <jlm/util/stats.hpp>

#include <iostream>

static std::unique_ptr<jlm::aa::ptg>
run_steensgaard(jlm::rvsdg_module & module)
{
	using namespace jlm;

	aa::steensgaard stgd;
	return stgd.run(module);
}

static void
assert_targets(
	const jlm::aa::ptg::node & node,
	const std::unordered_set<const jlm::aa::ptg::node*> & targets)
{
	using namespace jlm::aa;

	assert(node.ntargets() == targets.size());

	std::unordered_set<const ptg::node*> node_targets;
	for (auto & target : node)
		node_targets.insert(&target);

	assert(targets == node_targets);
}

static void
test_store1()
{
	auto validate_ptg = [](const jlm::aa::ptg & ptg, const store_test1 & test)
	{
		assert(ptg.nmemnodes() == 5);
		assert(ptg.nregnodes() == 5);

		auto & alloca_a = ptg.find(test.alloca_a);
		auto & alloca_b = ptg.find(test.alloca_b);
		auto & alloca_c = ptg.find(test.alloca_c);
		auto & alloca_d = ptg.find(test.alloca_d);

		auto & palloca_a = ptg.find(test.alloca_a->output(0));
		auto & palloca_b = ptg.find(test.alloca_b->output(0));
		auto & palloca_c = ptg.find(test.alloca_c->output(0));
		auto & palloca_d = ptg.find(test.alloca_d->output(0));

		auto & lambda = ptg.find(test.lambda);
		auto & plambda = ptg.find(test.lambda->output(0));

		assert_targets(alloca_a, {&alloca_b});
		assert_targets(alloca_b, {&alloca_c});
		assert_targets(alloca_c, {&alloca_d});
		assert_targets(alloca_d, {});

		assert_targets(palloca_a, {&alloca_a});
		assert_targets(palloca_b, {&alloca_b});
		assert_targets(palloca_c, {&alloca_c});
		assert_targets(palloca_d, {&alloca_d});

		assert_targets(lambda, {});
		assert_targets(plambda, {&lambda});
	};

	auto validate_rvsdg = [](const store_test1 & test)
	{
		using namespace jlm;

		auto & graph = *test.module().graph();

		assert(nnodes(graph.root()) == 10);

		auto mux = test.lambda->subregion()->result(0)->origin()->node();
		assert(jive::is<memstatemux_op>(mux));
		assert(mux->ninputs() == 4);

		assert(test.alloca_a->output(1)->nusers() == 1);
		auto store = (*test.alloca_a->output(1)->begin())->node();
		assert(jive::is<store_op>(store));
		assert((*store->output(0)->begin())->node() == mux);

		assert(test.alloca_b->output(1)->nusers() == 1);
		store = (*test.alloca_b->output(1)->begin())->node();
		assert(jive::is<store_op>(store));
		assert((*store->output(0)->begin())->node() == mux);

		assert(test.alloca_c->output(1)->nusers() == 1);
		store = (*test.alloca_c->output(1)->begin())->node();
		assert(jive::is<store_op>(store));
		assert((*store->output(0)->begin())->node() == mux);

		assert(test.alloca_d->output(1)->nusers() == 1);
		assert((*test.alloca_d->output(1)->begin())->node() == mux);
	};

	store_test1 test;
//	jive::view(test.graph().root(), stdout);

	auto ptg = run_steensgaard(test.module());
//	std::cout << jlm::aa::ptg::to_dot(*ptg);
	validate_ptg(*ptg, test);

	jlm::aa::ptg::encode(*ptg, test.module());
//	jive::view(test.graph().root(), stdout);
	validate_rvsdg(test);
}

static void
test_store2()
{
	auto validate_ptg = [](const jlm::aa::ptg & ptg, const store_test2 & test)
	{
		assert(ptg.nmemnodes() == 6);
		assert(ptg.nregnodes() == 6);

		auto & alloca_a = ptg.find(test.alloca_a);
		auto & alloca_b = ptg.find(test.alloca_b);
		auto & alloca_x = ptg.find(test.alloca_x);
		auto & alloca_y = ptg.find(test.alloca_y);
		auto & alloca_p = ptg.find(test.alloca_p);

		auto & palloca_a = ptg.find(test.alloca_a->output(0));
		auto & palloca_b = ptg.find(test.alloca_b->output(0));
		auto & palloca_x = ptg.find(test.alloca_x->output(0));
		auto & palloca_y = ptg.find(test.alloca_y->output(0));
		auto & palloca_p = ptg.find(test.alloca_p->output(0));

		auto & lambda = ptg.find(test.lambda);
		auto & plambda = ptg.find(test.lambda->output(0));

		assert_targets(alloca_a, {});
		assert_targets(alloca_b, {});
		assert_targets(alloca_x, {&alloca_a, &alloca_b});
		assert_targets(alloca_y, {&alloca_a, &alloca_b});
		assert_targets(alloca_p, {&alloca_x, &alloca_y});

		assert_targets(palloca_a, {&alloca_a, &alloca_b});
		assert_targets(palloca_b, {&alloca_a, &alloca_b});
		assert_targets(palloca_x, {&alloca_x, &alloca_y});
		assert_targets(palloca_y, {&alloca_x, &alloca_y});
		assert_targets(palloca_p, {&alloca_p});

		assert_targets(lambda, {});
		assert_targets(plambda, {&lambda});
	};

	auto validate_rvsdg = [](const store_test2 & test)
	{
		using namespace jlm;

		auto & graph = *test.module().graph();

		assert(nnodes(graph.root()) == 12);

		auto mux = test.lambda->subregion()->result(0)->origin()->node();
		assert(jive::is<memstatemux_op>(mux));
		assert(mux->ninputs() == 5);

		assert(test.alloca_a->output(1)->nusers() == 1);
		assert((*test.alloca_a->output(1)->begin())->node() == mux);

		assert(test.alloca_b->output(1)->nusers() == 1);
		assert((*test.alloca_b->output(1)->begin())->node() == mux);

		assert(test.alloca_x->output(1)->nusers() == 1);
		auto store = (*test.alloca_x->output(1)->begin())->node();
		assert(jive::is<store_op>(store) && store->noutputs() == 2);
		store = (*store->output(0)->begin())->node();
		assert(jive::is<store_op>(store) && store->noutputs() == 2);
		assert((*store->output(0)->begin())->node() == mux);

		assert(test.alloca_y->output(1)->nusers() == 1);
		store = (*test.alloca_y->output(1)->begin())->node();
		assert(jive::is<store_op>(store) && store->noutputs() == 2);
		store = (*store->output(0)->begin())->node();
		assert(jive::is<store_op>(store) && store->noutputs() == 2);
		assert((*store->output(0)->begin())->node() == mux);

		assert(test.alloca_p->output(1)->nusers() == 1);
		store = (*test.alloca_p->output(1)->begin())->node();
		assert(jive::is<store_op>(store) && store->noutputs() == 1);
		store = (*store->output(0)->begin())->node();
		assert(jive::is<store_op>(store) && store->noutputs() == 1);
		assert((*store->output(0)->begin())->node() == mux);
	};

	store_test2 test;
//	jive::view(test.graph().root(), stdout);

	auto ptg = run_steensgaard(test.module());
//	std::cout << jlm::aa::ptg::to_dot(*ptg);
	validate_ptg(*ptg, test);

	jlm::aa::ptg::encode(*ptg, test.module());
//	jive::view(test.graph().root(), stdout);
	validate_rvsdg(test);
}

static void
test_load1()
{
	auto validate_ptg = [](const jlm::aa::ptg & ptg, const load_test1 & test)
	{
		assert(ptg.nmemnodes() == 1);
		assert(ptg.nregnodes() == 3);

		auto & pload_p = ptg.find(test.load_p->output(0));

		auto & lambda = ptg.find(test.lambda);
		auto & plambda = ptg.find(test.lambda->output(0));
		auto & lambarg0 = ptg.find(test.lambda->subregion()->argument(0));

		assert_targets(pload_p, {&ptg.memunknown()});

		assert_targets(plambda, {&lambda});
		assert_targets(lambarg0, {&ptg.memunknown()});
	};

	load_test1 test;
//	jive::view(test.graph()->root(), stdout);
	auto ptg = run_steensgaard(test.module());
//	std::cout << jlm::aa::ptg::to_dot(*ptg);

	validate_ptg(*ptg, test);

	/* FIXME: validate RVSDG encoding of aa results */
}

static void
test_load2()
{
	auto validate_ptg = [](const jlm::aa::ptg & ptg, const load_test2 & test)
	{
		assert(ptg.nmemnodes() == 6);
		assert(ptg.nregnodes() == 8);

		/*
			We only care about the loads in this test, skipping the validation
			for all other nodes.
		*/
		auto & alloca_a = ptg.find(test.alloca_a);
		auto & alloca_b = ptg.find(test.alloca_b);
		auto & alloca_x = ptg.find(test.alloca_x);
		auto & alloca_y = ptg.find(test.alloca_y);

		auto & pload_x = ptg.find(test.load_x->output(0));
		auto & pload_a = ptg.find(test.load_a->output(0));

		assert_targets(pload_x, {&alloca_x, &alloca_y});
		assert_targets(pload_a, {&alloca_a, &alloca_b});
	};

	load_test2 test;
//	jive::view(test.graph()->root(), stdout);
	auto ptg = run_steensgaard(test.module());
	std::cout << jlm::aa::ptg::to_dot(*ptg);

	validate_ptg(*ptg, test);

	/* FIXME: validate RVSDG encoding of aa results */
}

static void
test_getelementptr()
{
	using namespace jlm;

	auto module = setup_getelementptr_test();
	auto graph = module->graph();
//	jive::view(graph->root(), stdout);

	run_steensgaard(*module);
	/**
	* FIXME: Add assertions
	* {p, p->x, p->y} -> {ld(p->x), ld(p->y)}
	*/
}

static void
test_bitcast()
{
	using namespace jlm;

	auto module = setup_bitcast_test();
	auto graph = module->graph();
//	jive::view(graph->root(), stdout);

	run_steensgaard(*module);
	/**
	* FIXME: Add assertions
	* {Lambda:arg, bitcast result}
	*/
}

static void
test_null()
{
	using namespace jlm;

	auto module = setup_null_test();
	auto graph = module->graph();
//	jive::view(graph->root(), stdout);

	run_steensgaard(*module);
	/**
	* FIXME: Add assertions
	*/
}

static void
test_call1()
{
	auto validate_ptg = [](const jlm::aa::ptg & ptg, const call_test1 & test)
	{
		assert(ptg.nmemnodes() == 6);
		assert(ptg.nregnodes() == 12);

		auto & alloca_x = ptg.find(test.alloca_x);
		auto & alloca_y = ptg.find(test.alloca_y);
		auto & alloca_z = ptg.find(test.alloca_z);

		auto & palloca_x = ptg.find(test.alloca_x->output(0));
		auto & palloca_y = ptg.find(test.alloca_y->output(0));
		auto & palloca_z = ptg.find(test.alloca_z->output(0));

		auto & lambda_f = ptg.find(test.lambda_f);
		auto & lambda_g = ptg.find(test.lambda_g);
		auto & lambda_h = ptg.find(test.lambda_h);

		auto & plambda_f = ptg.find(test.lambda_f->output(0));
		auto & plambda_g = ptg.find(test.lambda_g->output(0));
		auto & plambda_h = ptg.find(test.lambda_h->output(0));

		auto & lambda_f_arg0 = ptg.find(test.lambda_f->subregion()->argument(0));
		auto & lambda_f_arg1 = ptg.find(test.lambda_f->subregion()->argument(1));

		auto & lambda_g_arg0 = ptg.find(test.lambda_g->subregion()->argument(0));
		auto & lambda_g_arg1 = ptg.find(test.lambda_g->subregion()->argument(1));

		auto & lambda_h_cv0 = ptg.find(test.lambda_h->subregion()->argument(1));
		auto & lambda_h_cv1 = ptg.find(test.lambda_h->subregion()->argument(2));

		assert_targets(palloca_x, {&alloca_x});
		assert_targets(palloca_y, {&alloca_y});
		assert_targets(palloca_z, {&alloca_z});

		assert_targets(plambda_f, {&lambda_f});
		assert_targets(plambda_g, {&lambda_g});
		assert_targets(plambda_h, {&lambda_h});

		assert_targets(lambda_f_arg0, {&alloca_x});
		assert_targets(lambda_f_arg1, {&alloca_y});

		assert_targets(lambda_g_arg0, {&alloca_z});
		assert_targets(lambda_g_arg1, {&alloca_z});

		assert_targets(lambda_h_cv0, {&lambda_f});
		assert_targets(lambda_h_cv1, {&lambda_g});
	};

	auto validate_rvsdg = [](const call_test1 & test)
	{
		using namespace jlm;

		auto & graph = *test.module().graph();

		/* validate f */
		{
			auto mux_exit = test.lambda_f->subregion()->result(1)->origin()->node();
			assert(jive::is<memstatemux_op>(mux_exit));
			assert(mux_exit->ninputs() == 2);

			auto load1 = mux_exit->input(0)->origin()->node();
			auto load2 = mux_exit->input(1)->origin()->node();
			assert(jive::is<load_op>(load1) && jive::is<load_op>(load2));
			assert(load1 != load2);

			auto mux_entry = (*test.lambda_f->subregion()->argument(2)->begin())->node();
			assert(jive::is<memstatemux_op>(mux_entry));
			assert(mux_entry->noutputs() == 2);
			assert((*mux_entry->output(0)->begin())->node() == load1);
			assert((*mux_entry->output(1)->begin())->node() == load2);
		}

		/* validate g */
		{
			auto mux_exit = test.lambda_g->subregion()->result(1)->origin()->node();
			assert(jive::is<memstatemux_op>(mux_exit));
			assert(mux_exit->ninputs() == 1);

			auto load = mux_exit->input(0)->origin()->node();
			assert(jive::is<load_op>(load));
			assert(load->ninputs() == 2);

			load = load->input(1)->origin()->node();
			assert(jive::is<load_op>(load));
			assert(load->ninputs() == 2);

			auto mux_entry = load->input(1)->origin()->node();
			assert(jive::is<memstatemux_op>(mux_entry));
			assert(mux_entry->noutputs() == 1);
		}

		/* validate h */
		{
			auto store_x = (*test.alloca_x->output(1)->begin())->node();
			auto store_y = (*test.alloca_y->output(1)->begin())->node();
			auto store_z = (*test.alloca_z->output(1)->begin())->node();
			assert(store_x != store_y && store_y != store_z);

			auto mux_callf1 = (*store_x->output(0)->begin())->node();
			auto mux_callf2 = (*store_y->output(0)->begin())->node();
			assert(mux_callf1 == mux_callf2);

			auto mux_callg = (*store_z->output(0)->begin())->node();
			assert(mux_callg != mux_callf1);
		}
	};

	call_test1 test;
	jive::view(test.graph().root(), stdout);

	auto ptg = run_steensgaard(test.module());
//	std::cout << jlm::aa::ptg::to_dot(*ptg);
	validate_ptg(*ptg, test);

	jlm::aa::ptg::encode(*ptg, test.module());
//	jive::view(test.graph().root(), stdout);
	validate_rvsdg(test);
}

static void
test_call2()
{
	using namespace jlm;

	auto module = setup_call_test2();
	auto graph = module->graph();
//	jive::view(graph->root(), stdout);

	run_steensgaard(*module);
	/**
	* FIXME: Add assertions
	*/
}

static void
test_gamma()
{
	using namespace jlm;

	auto module = setup_gamma_test();
	auto graph = module->graph();
//	jive::view(graph->root(), stdout);

	run_steensgaard(*module);
	/**
	* FIXME: Add assertions
	*/
}

static void
test_theta()
{
	using namespace jlm;

	auto module = setup_theta_test();
	auto graph = module->graph();
//	jive::view(graph->root(), stdout);

	run_steensgaard(*module);
	/**
	* FIXME: Add assertions
	*/
}

static void
test_delta()
{
	using namespace jlm;

	auto module = setup_delta_test();
	auto graph = module->graph();
//	jive::view(graph->root(), stdout);

	run_steensgaard(*module);
	/**
	* FIXME: Add assertions
	*/
}

static void
test_alloc_store_load()
{
	using namespace jlm;

	auto module = setup_alloc_store_load_test();
	auto graph = module->graph();
//	jive::view(graph->root(), stdout);

	aa::steensgaard stgd;
	stats_descriptor sd;
	stgd.run(*module, sd);

	//FIXME: add assertions
}

static int
test()
{
//	test_store1();
//	test_store2();
//	test_load1();
//		test_load2();
//	test_getelementptr();
//	test_bitcast();
//	test_null();
	test_call1();
//	test_call2();

//	test_gamma();
//	test_theta();
//	test_delta();

//	test_alloc_store_load();

	return 0;
}

JLM_UNIT_TEST_REGISTER("libjlm/opt/alias-analyses/test-steensgaard", test)
