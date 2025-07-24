//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_NOBUILTINSWORLD_H
#define LLVM_TRANSFORMS_UTILS_NOBUILTINSWORLD_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;
class ModuleSummaryIndex;

class NoBuiltinsWorld : public PassInfoMixin<NoBuiltinsWorld> {
public:
  NoBuiltinsWorld() = default;
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_HELLOWORLD_H
