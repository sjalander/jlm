/*
 * Copyright 2014 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include "test-registry.hpp"

#include <jlm/IR/basic_block.hpp>
#include <jlm/IR/clg.hpp>
#include <jlm/IR/tac/operators.hpp>
#include <jlm/IR/tac/tac.hpp>

#include <jive/arch/addresstype.h>
#include <jive/arch/memorytype.h>
#include <jive/arch/store.h>
#include <jive/types/bitstring/type.h>

#include <assert.h>

static int
verify(jlm::frontend::clg & clg)
{
	jlm::frontend::clg_node * node = clg.lookup_function("test_store");
	assert(node != nullptr);

	jlm::frontend::cfg * cfg = node->cfg();
//	jive_cfg_view(cfg);

	assert(cfg->nnodes() == 3);
	assert(cfg->is_linear());

	jlm::frontend::basic_block * bb = dynamic_cast<jlm::frontend::basic_block*>(
		cfg->enter()->outedges()[0]->sink());
	assert(bb != nullptr);

	const std::list<const jlm::frontend::tac*> & tacs = bb->tacs();
	assert(tacs.size() >= 2);

	jive::addr::type addrtype;
	jive::bits::type datatype(32);
	std::vector<std::unique_ptr<jive::state::type>> state_type;
	state_type.emplace_back(std::unique_ptr<jive::state::type>(new jive::mem::type()));
	jive::store_op op(addrtype, state_type, datatype);
	assert((*tacs.begin())->operation() == op);
	assert((*(std::next(tacs.begin())))->operation() == jlm::frontend::assignment_op(jive::mem::type()));

	return 0;
}

JLM_UNIT_TEST_REGISTER("libjlm/instructions/test-store", verify)