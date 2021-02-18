[![Tests](https://github.com/phate/jlm/actions/workflows/tests.yml/badge.svg)](https://github.com/phate/jlm/actions/workflows/tests.yml)
# JLM: An experimental compiler/optimizer for LLVM IR

[![Tests](https://github.com/phate/jlm/actions/workflows/tests.yml/badge.svg)](https://github.com/phate/jlm/actions/workflows/tests.yml)

Jlm is an experimental compiler/optimizer that consumes and produces LLVM IR. It uses the
Regionalized Value State Dependence Graph (RVSDG) as intermediate representation for optimizations.

## Dependencies:
* Clang/LLVM 10

## Bootstrap:
```
export LLVMCONFIG=<path-to-llvm-config>
make submodule
make all
```
Please ensure that `LLVMCONFIG` is set to the correct version of `llvm-config` as stated in
dependencies.
