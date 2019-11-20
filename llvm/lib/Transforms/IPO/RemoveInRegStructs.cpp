
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

#define DEBUG_TYPE "rem-struct"

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
    LLVM_DEBUG(llvm::dbgs() << "-- RemoveInRegStructs Pass begin -- \n";);

    if (!ShouldUpdateModule(M)) {
      LLVM_DEBUG(llvm::dbgs() << "No InRegStructs APIs in Module\n";);
      return false;
    }
    LLVM_DEBUG(llvm::dbgs()
               << "RemoveInRegStruct Pass found " << AggregateReturns.size()
               << " Functions to update\n");
    LLVM_DEBUG(llvm::dbgs() << "-----------------------------------\n";);

    RemoveInRegStructsPass RIRSP;
    ModuleAnalysisManager DummyMAM;
    PreservedAnalyses PA = RIRSP.run(M, DummyMAM, AggregateReturns);
    return !PA.areAllPreserved();
  }

  bool ShouldUpdateModule(Module &M) {
    bool shouldUpdate = false;
    LLVM_DEBUG(llvm::dbgs() << "ReplaceInRegStruct Pass Diagnostics\n";);
    for (Function &F : M) {
      // Check if the function is 'untrusted'
      if (F.getReturnType()->isAggregateType()) {
        shouldUpdate = true;
        AggregateReturns.push_back(&F);
      }
    }
    LLVM_DEBUG(llvm::dbgs() << (shouldUpdate ? "" : "No ")
                            << "Aggregate Return APIs found in module\n");
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

  RemoveExtractValueInst(M);

  return PreservedAnalyses::none();
}

void RemoveInRegStructsPass::RemoveExtractValueInst(Module &M) {
  SmallVector<Instruction *, 8> ToRemove;
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &I : BB) {
        if (ExtractValueInst *EV = dyn_cast<ExtractValueInst>(&I)) {
          auto OP = EV->getAggregateOperand();
          if (auto Load = dyn_cast<LoadInst>(OP)) {
            auto Slot = Load->getPointerOperand();
            SmallVector<Value *, 4> Idx;
            Idx.push_back(llvm::ConstantInt::get(
                F.getContext(),
                llvm::APInt(32, EV->getAggregateOperandIndex(), false)));

            for (auto id : EV->indices()) {
              Idx.push_back(llvm::ConstantInt::get(F.getContext(),
                                                   llvm::APInt(32, id, false)));
            }

            auto newGep = GetElementPtrInst::CreateInBounds(
                OP->getType(), Slot, Idx, "StructGep", EV);

            LLVM_DEBUG(llvm::dbgs()
                       << "New Gep created:\n"
                       << *newGep << "\n"
                       << "New Gep Type: " << *newGep->getType() << "\n");

            auto GepLoad =
                new LoadInst(EV->getType(), newGep, "LoadedGepVal", EV);

            LLVM_DEBUG(llvm::dbgs()
                       << "New Load created:\n"
                       << *GepLoad << "\n"
                       << "New Loaded Type: " << *GepLoad->getType() << "\n");

            EV->replaceAllUsesWith(GepLoad);
            ToRemove.push_back(EV);
          } // if LoadInst
        }   // if ExtractValueInst
      }     // for I in BB
    }       // for BB in F
  }         // for F in Module

  for (auto *I : ToRemove) {
    I->eraseFromParent();
  }
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
    LLVM_DEBUG(llvm::dbgs() << "Updating call site:\n" << *Call << "\n");
    CallSite CS(Call);
    auto BB = CS.getParent();

    llvm::IRBuilder<> IRB(BB);
    auto CallSiteInst = CS.getInstruction();
    auto allocaInst =
        new AllocaInst(CS.getType(), 0, "RetValSlot", CallSiteInst);

    SmallVector<Value *, 4> Args;
    Args.push_back(allocaInst);
    for (auto &A : CS.args()) {
      Args.push_back(A);
    }
    Instruction *newCall = CallInst::Create(NewF->getFunctionType(), NewF, Args,
                                            None, "", CallSiteInst);

    auto Load = new LoadInst(F->getReturnType(), allocaInst, "LoadedRetVal",
                             CallSiteInst);
    CS.getInstruction()->replaceAllUsesWith(Load);
    BB->getInstList().erase(CallSiteInst);
  }

  auto M = F->getParent();
  LLVM_DEBUG(llvm::dbgs() << "After updating Function \"" << F->getName() << "\" has "
                          << F->getNumUses() << " uses\n");
  M->getFunctionList().erase(F);
  return true;
}

ModulePass *llvm::createRemoveInRegStructsPass() {
  return new RemoveInRegStructs();
}
