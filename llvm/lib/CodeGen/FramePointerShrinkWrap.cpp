//===-- MachineFunctionPrinterPass.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// MachineFunctionPrinterPass implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/GlobalISel/MachineIRBuilder.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/CodeGen/SlotIndexes.h"
#include "llvm/CodeGen/TargetInstrInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/PrintPasses.h"
#include "llvm/InitializePasses.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

namespace {
/// FramePointerShrinkWrapPass - Pass marks the frame pointer as a live register
///
struct FramePointerShrinkWrapPass : public MachineFunctionPass {
  static char ID;

  FramePointerShrinkWrapPass() : MachineFunctionPass(ID) {}

  StringRef getPassName() const override {
    return "Frame Pointer Shrink Wrappng Pass";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    AU.addUsedIfAvailable<SlotIndexes>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  // TODO: don't copy these functions from target/x86

  bool hasFP(const MachineFunction &MF) const {
    const MachineFrameInfo &MFI = MF.getFrameInfo();
    auto &STI = MF.getSubtarget();
    auto *TRI = STI.getRegisterInfo();
    return (MF.getTarget().Options.DisableFramePointerElim(MF) ||
            TRI->hasStackRealignment(MF) || MFI.hasVarSizedObjects() ||
            MFI.isFrameAddressTaken() || MFI.hasOpaqueSPAdjustment() ||
            MF.callsUnwindInit() || MF.hasEHFunclets() || MF.callsEHReturn() ||
            MFI.hasStackMap() || MFI.hasPatchPoint() ||
            (MF.getTarget().getMCAsmInfo()->usesWindowsCFI() &&
             MFI.hasCopyImplyingStackAdjustment()));
  }

  bool hasFPShrinkWrap(const MachineFunction &MF) const {
    if (hasFP(MF))
      return false;
    const Function &F = MF.getFunction();
    if (!F.hasFnAttribute("frame-pointer"))
      return false;
    StringRef FP = F.getFnAttribute("frame-pointer").getValueAsString();
    return FP == "shrink-wrap";
  }

  bool runOnMachineFunction(MachineFunction &MF) override {
    // only modify functions when shrink warp is enabled
    if (!hasFPShrinkWrap(MF))
      return false;

    auto &STI = MF.getSubtarget();
    auto *TII = STI.getInstrInfo();
    auto *TRI = STI.getRegisterInfo();
    MachineRegisterInfo &MRI = MF.getRegInfo();
    Register FramePtr = TRI->getFPReg(MF);

    // TODO: Rematerialize FP instead.

    // add virtual reg in entry block, this is going to hold FP
    const TargetRegisterClass *RegClass = TRI->getPointerRegClass(MF);
    auto VReg = MRI.createVirtualRegister(RegClass);
    auto &Entry = MF.front();
    auto I = Entry.begin();

    Entry.addLiveIn(FramePtr);

    auto DL = I->getDebugLoc();
    BuildMI(Entry, Entry.begin(), DL, TII->get(TargetOpcode::COPY), VReg)
        .addReg(FramePtr);

    // iterate over blocks
    for (auto &MBB : MF) {
      // iterate over instructions
      for (MachineBasicBlock::iterator MBBI = MBB.begin(); MBBI != MBB.end();
           ++MBBI) {
        if (MBBI->isCall()) {
          DL = MBBI->getDebugLoc();
          MBBI = BuildMI(MBB, *MBBI, DL, TII->get(TargetOpcode::COPY), FramePtr)
                     .addReg(VReg);
          MBBI++;
        }
      }
    }

    return true;
  }
};

char FramePointerShrinkWrapPass::ID = 0;
} // namespace

char &llvm::FramePointerShrinkWrapPassID = FramePointerShrinkWrapPass::ID;
INITIALIZE_PASS(FramePointerShrinkWrapPass, "frame-pointer-shrink-wrap",
                "Frame Pointer Shrink Wrap", false, false)

namespace llvm {
/// Returns a newly-created MachineFunction Printer pass. The
/// default banner is empty.
///
MachineFunctionPass *createFramePointerShrinkWrapPass() {
  return new FramePointerShrinkWrapPass();
}

} // namespace llvm
