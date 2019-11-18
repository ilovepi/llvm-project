
#include "llvm/Transforms/IPO/RemoveInRegStructs.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

#include <cstring>
#include <string>

using namespace llvm;

namespace {
class RemoveInRegStructs : public ModulePass {
public:
  static char ID;

  RemoveInRegStructs() : ModulePass(ID) {
    // initializeRemoveInRegStructsPass(*PassRegistry::getPassRegistry());
  }

  virtual ~RemoveInRegStructs() = default;

  bool runOnModule(Module &M) override {
    if (skipModule(M))
      return false;

    return UpdateModule(M);
  }

  bool UpdateModule(Module &M) {
    llvm::errs() << "-- RemoveInRegStructs Pass begin -- \n";
    if (!ShouldUpdateModule(M)) {
      llvm::errs() << "No InRegStructs APIs in Module\n";
      return false;
    }
    llvm::errs() << "RemoveInRegStruct Pass found " << AggregateReturns.size()
                 << " Functions to update\n";

    RemoveInRegStructsPass RIRSP;
    ModuleAnalysisManager DummyMAM;
    PreservedAnalyses PA = RIRSP.run(M, DummyMAM, AggregateReturns);
    return !PA.areAllPreserved();
  }

  bool ShouldUpdateModule(Module &M) {
    bool shouldUpdate = false;
    llvm::errs() << "ReplaceInRegStruct Pass Diagnostics\n";
    for (Function &F : M) {
      // Check if the function is 'untrusted'
      if (F.getReturnType()->isAggregateType()) {
        shouldUpdate = true;
        AggregateReturns.push_back(&F);
      }
    }
    if (shouldUpdate)
      llvm::errs() << "Aggregate Return APIs found in module\n";
    else
      llvm::errs() << "No Aggregate Return APIs in Module\n";
    return shouldUpdate;
  }

private:
  SmallVector<Function *, 4> AggregateReturns;
};
} // namespace

char RemoveInRegStructs::ID = 0;

INITIALIZE_PASS(RemoveInRegStructs, "rem-struct", "Remove In Register Structs",
                false, false)
PreservedAnalyses
RemoveInRegStructsPass::run(Module &M, ModuleAnalysisManager &AM,
                            SmallVector<Function *, 4> &AggregateReturns) {
  bool Changed = false;
  AggRets = AggregateReturns;

  for (auto *F : AggregateReturns) {
    Changed |= UpdateFunction(F);
  }
  if (!Changed)
    return PreservedAnalyses::all();
  return PreservedAnalyses::none();
}

bool RemoveInRegStructsPass::UpdateFunction(Function *F) {
  ValueToValueMapTy VMap;
  Type *NewRetType = F->getReturnType()->getPointerElementType();
  //  FunctionType* oldFT =  F->getFunctionType();
  //  auto params = oldFT->params();
  //  FunctionType* newFT = oldFT->get(RetType,params,
  //  oldFT->isVarArg()); auto name = F->getName() + "_untrusted";
  //  auto newFunc =
  //  F->getParent()->getOrInsertFunction(name.str(),newFT,
  const AttributeList &PAL = F->getAttributes();
  // auto newF = CloneFunction(F, VMap);

  // newF->ret

  std::vector<Type *> ArgTypes;
  ArgTypes.push_back(NewRetType);

  SmallVector<AttributeSet, 8> ArgAttrVec;
  auto RetAttr = PAL.getRetAttributes();
  auto NewRetAttr = RetAttr.addAttribute(F->getContext(), Attribute::Returned);
  ArgAttrVec.push_back(NewRetAttr);
  auto ArgNum = F->getFunctionType()->getNumParams();
  for (size_t i = 0; i < ArgNum; ++i) {
    ArgAttrVec.push_back(PAL.getParamAttributes(i));
  }

  // The user might be deleting arguments to the function by specifying
  // them in the VMap.  If so, we need to not add the arguments to the arg
  // ty vector
  //
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0) // Haven't mapped the argument to anything yet?
      ArgTypes.push_back(I.getType());

  // Create a new function type...
  FunctionType *FTy =
      FunctionType::get(Type::getVoidTy(F->getContext()), ArgTypes,
                        F->getFunctionType()->isVarArg());

  // Create the new function...
  Function *NewF = Function::Create(FTy, F->getLinkage(), F->getAddressSpace(),
                                    F->getName(), F->getParent());

  // Loop over the arguments, copying the names of the mapped arguments
  // over...
  Function::arg_iterator DestI = NewF->arg_begin();
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0) {     // Is this argument preserved?
      DestI->setName(I.getName()); // Copy the name over...
      VMap[&I] = &*DestI++;        // Add mapping to VMap
    }

  SmallVector<ReturnInst *, 8> Returns; // Ignore returns cloned.
  CloneFunctionInto(NewF, F, VMap, F->getSubprogram() != nullptr, Returns,
                    "__untrusted__", nullptr);

  for (auto &BB : *NewF) {
    if (auto TI = BB.getTerminator()) {
    }
  }

  //NewF->dump();
  llvm::errs() << *NewF << "\n";

  // return NewF;
  return true;
}

ModulePass *llvm::createRemoveInRegStructsPass() {
  return new RemoveInRegStructs();
}
