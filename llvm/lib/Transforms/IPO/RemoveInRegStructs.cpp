
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
  std::vector<Type *> ArgTypes;
  SmallVector<AttributeSet, 8> ArgAttrVec;
  const AttributeList &PAL = F->getAttributes();
  Type *NewRetType = F->getReturnType()->getPointerTo();
  ArgTypes.push_back(NewRetType);
  auto RetAttr = PAL.getRetAttributes();
  auto NewRetAttr = RetAttr.addAttribute(F->getContext(), Attribute::StructRet);
  ArgAttrVec.push_back(NewRetAttr);
  auto ArgNum = F->getFunctionType()->getNumParams();
  for (size_t i = 0; i < ArgNum; ++i) {
    ArgAttrVec.push_back(PAL.getParamAttributes(i));
  }
  AttributeList NewPAL;
  NewPAL = AttributeList::get(F->getContext(), PAL.getFnAttributes(), RetAttr,
                              ArgAttrVec);

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
  Function *NewF =
      Function::Create(FTy, F->getLinkage(), F->getAddressSpace(),
                       F->getName() + "__untrusted__", F->getParent());

  // Loop over the arguments, copying the names of the mapped arguments
  // over...
  Function::arg_iterator DestI = NewF->arg_begin();
  DestI++;
  for (const Argument &I : F->args())
    if (VMap.count(&I) == 0) {     // Is this argument preserved?
      DestI->setName(I.getName()); // Copy the name over...
      VMap[&I] = &*DestI++;        // Add mapping to VMap
    }

  SmallVector<ReturnInst *, 8> Returns; // Ignore returns cloned.
  //  CloneFunctionInto(NewF, F, VMap, F->getSubprogram() != nullptr, Returns,
  //  "",
  CloneFunctionInto(NewF, F, VMap, true, Returns, "", nullptr);

  NewF->setAttributes(NewPAL);
  for (auto &BB : *NewF) {
    if (auto TI = BB.getTerminator()) {
      if (auto RetInst = dyn_cast<ReturnInst>(TI)) {
        llvm::IRBuilder<> IRB(&BB);
        auto RetVal = RetInst->getReturnValue();
        auto RetLoc = NewF->arg_begin();
        IRB.CreateStore(RetVal, RetLoc);
        IRB.CreateRetVoid();
        TI->eraseFromParent();
      }
    }
  }

  for (auto Call : F->users()) {
    Call->dump();
    CallSite CS(Call);
    auto BB = CS.getParent();

    llvm::IRBuilder<> IRB(BB);
    auto CallSiteInst = CS.getInstruction();
    // auto allocaInst = IRB.CreateAlloca(CS.getType());
    auto allocaInst =
        new AllocaInst(CS.getType(), 0, "RetValSlot", CallSiteInst);

    SmallVector<Value *, 4> Args;
    Args.push_back(allocaInst);
    for (auto &A : CS.args()) {
      Args.push_back(A);
    }
    // auto newCall = IRB.CreateCall(NewF->getFunctionType(), NewF, Args);
    Instruction *newCall = CallInst::Create(NewF->getFunctionType(), NewF, Args,
                                            None, "", CallSiteInst);

    auto Load = new LoadInst(F->getReturnType(), allocaInst, "LoadedRetVal",
                             CallSiteInst);
    CS.getInstruction()->replaceAllUsesWith(Load);
    BB->getInstList().erase(CallSiteInst);

    //CallSiteInst->removeFromParent();
  }

  // F->dump();
  // llvm::errs() << *NewF << "\n";
  auto M = F->getParent();
  llvm::errs() << "\nThe Function " << F->getName() << " has " << F->getNumUses() << " Uses in the program\n";
  //F->dropAllReferences();
  M->getFunctionList().erase(F);
  //F->removeFromParent();
  // M->dump();

  return true;
}

ModulePass *llvm::createRemoveInRegStructsPass() {
  return new RemoveInRegStructs();
}
