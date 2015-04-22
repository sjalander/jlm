/*
 * Copyright 2014 2015 Nico Reißmann <nico.reissmann@gmail.com>
 * See COPYING for terms of redistribution.
 */

#include "test-registry.hpp"

#include <jlm/IR/module.hpp>

#include <jlm/construction/module.hpp>
#include <jlm/destruction/destruction.hpp>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/SourceMgr.h>

#include <assert.h>
#include <memory>
#include <unordered_map>

namespace jlm {

class unit_test {
public:
	unit_test(int(*v)(const jive_graph * graph)) : verify(v) {}
	int (*verify)(const jive_graph * graph);
};

static std::unordered_map<std::string, std::unique_ptr<unit_test>> unit_test_map;

void
register_unit_test(const std::string & name, int (*verify)(const jive_graph * graph))
{
	assert(unit_test_map.find(name) == unit_test_map.end());

	unit_test_map.insert(std::make_pair(name, std::unique_ptr<unit_test>(new unit_test(verify))));
}

int
run_unit_test(const std::string & name)
{
	assert(unit_test_map.find(name) != unit_test_map.end());

	std::string llname("tests/");
	llname.append(name).append(".ll");

	llvm::LLVMContext & context = llvm::getGlobalContext();

	llvm::SMDiagnostic err;
	std::unique_ptr<llvm::Module> module(llvm::ParseIRFile(llname.c_str(), err, context));
	if (!module) {
		err.print(llname.c_str(), llvm::errs());
		assert(0);
	}

	jlm::module m;
	convert_module(*module, m);
	struct jive_graph * graph = jlm::construct_rvsdg(m.clg());

	return unit_test_map[name]->verify(graph);
}

}
