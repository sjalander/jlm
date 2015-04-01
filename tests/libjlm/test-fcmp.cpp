/*
 * Copyright 2015 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include "test-registry.hpp"

#include <jive/view.h>

#include <assert.h>

static int
verify(const jive_graph * graph)
{
	jive_view(const_cast<jive_graph*>(graph), stdout);

	/* FIXME: add checks and tests for all fcmp operations and types in the test-fcmp.ll */
	return 0;
}

JLM_UNIT_TEST_REGISTER("libjlm/test-fcmp", verify);
