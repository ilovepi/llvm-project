//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Changes every function in the module to be "no-builtins", marking the end, of
// the compiler treating the C standard library (libc) as an abstract interface.
// After this point in the pipeline, everything in the module is treated as IR.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Utils/NoBuiltinsWorld.h"
#include "llvm/IR/Module.h"

using namespace llvm;

#define DEBUG_TYPE "no-builtins-world"

PreservedAnalyses NoBuiltinsWorld::run(Module &M, ModuleAnalysisManager &AM) {
  for (auto &F : M) {
    // Do we need to check if the attr exists?
    F.addFnAttr("no-builtins");
  }

  // TODO: Do we need a module flag for this?

  return PreservedAnalyses::all();
}
