//===-- NVPTXInstPrinter.cpp - PTX assembly instruction printing ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Print MCInst instructions to .ptx format.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/NVPTXInstPrinter.h"
#include "NVPTX.h"
#include "NVPTXUtilities.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/NVVMIntrinsicUtils.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include <cctype>
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "NVPTXGenAsmWriter.inc"

NVPTXInstPrinter::NVPTXInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                                   const MCRegisterInfo &MRI)
    : MCInstPrinter(MAI, MII, MRI) {}

void NVPTXInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  // Decode the virtual register
  // Must be kept in sync with NVPTXAsmPrinter::encodeVirtualRegister
  unsigned RCId = (Reg.id() >> 28);
  switch (RCId) {
  default: report_fatal_error("Bad virtual register encoding");
  case 0:
    // This is actually a physical register, so defer to the autogenerated
    // register printer
    OS << getRegisterName(Reg);
    return;
  case 1:
    OS << "%p";
    break;
  case 2:
    OS << "%rs";
    break;
  case 3:
    OS << "%r";
    break;
  case 4:
    OS << "%rd";
    break;
  case 5:
    OS << "%f";
    break;
  case 6:
    OS << "%fd";
    break;
  case 7:
    OS << "%rq";
    break;
  }

  unsigned VReg = Reg.id() & 0x0FFFFFFF;
  OS << VReg;
}

void NVPTXInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                 StringRef Annot, const MCSubtargetInfo &STI,
                                 raw_ostream &OS) {
  printInstruction(MI, Address, OS);

  // Next always print the annotation.
  printAnnotation(OS, Annot);
}

void NVPTXInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                    raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    MCRegister Reg = Op.getReg();
    printRegName(O, Reg);
  } else if (Op.isImm()) {
    markup(O, Markup::Immediate) << formatImm(Op.getImm());
  } else {
    assert(Op.isExpr() && "Unknown operand kind in printOperand");
    MAI.printExpr(O, *Op.getExpr());
  }
}

void NVPTXInstPrinter::printCvtMode(const MCInst *MI, int OpNum, raw_ostream &O,
                                    StringRef Modifier) {
  const MCOperand &MO = MI->getOperand(OpNum);
  int64_t Imm = MO.getImm();

  if (Modifier == "ftz") {
    // FTZ flag
    if (Imm & NVPTX::PTXCvtMode::FTZ_FLAG)
      O << ".ftz";
    return;
  } else if (Modifier == "sat") {
    // SAT flag
    if (Imm & NVPTX::PTXCvtMode::SAT_FLAG)
      O << ".sat";
    return;
  } else if (Modifier == "relu") {
    // RELU flag
    if (Imm & NVPTX::PTXCvtMode::RELU_FLAG)
      O << ".relu";
    return;
  } else if (Modifier == "base") {
    // Default operand
    switch (Imm & NVPTX::PTXCvtMode::BASE_MASK) {
    default:
      return;
    case NVPTX::PTXCvtMode::NONE:
      return;
    case NVPTX::PTXCvtMode::RNI:
      O << ".rni";
      return;
    case NVPTX::PTXCvtMode::RZI:
      O << ".rzi";
      return;
    case NVPTX::PTXCvtMode::RMI:
      O << ".rmi";
      return;
    case NVPTX::PTXCvtMode::RPI:
      O << ".rpi";
      return;
    case NVPTX::PTXCvtMode::RN:
      O << ".rn";
      return;
    case NVPTX::PTXCvtMode::RZ:
      O << ".rz";
      return;
    case NVPTX::PTXCvtMode::RM:
      O << ".rm";
      return;
    case NVPTX::PTXCvtMode::RP:
      O << ".rp";
      return;
    case NVPTX::PTXCvtMode::RNA:
      O << ".rna";
      return;
    }
  }
  llvm_unreachable("Invalid conversion modifier");
}

void NVPTXInstPrinter::printFTZFlag(const MCInst *MI, int OpNum,
                                    raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  const int Imm = MO.getImm();
  if (Imm)
    O << ".ftz";
}

void NVPTXInstPrinter::printCmpMode(const MCInst *MI, int OpNum, raw_ostream &O,
                                    StringRef Modifier) {
  const MCOperand &MO = MI->getOperand(OpNum);
  int64_t Imm = MO.getImm();

  if (Modifier == "FCmp") {
    switch (Imm) {
    default:
      return;
    case NVPTX::PTXCmpMode::EQ:
      O << "eq";
      return;
    case NVPTX::PTXCmpMode::NE:
      O << "ne";
      return;
    case NVPTX::PTXCmpMode::LT:
      O << "lt";
      return;
    case NVPTX::PTXCmpMode::LE:
      O << "le";
      return;
    case NVPTX::PTXCmpMode::GT:
      O << "gt";
      return;
    case NVPTX::PTXCmpMode::GE:
      O << "ge";
      return;
    case NVPTX::PTXCmpMode::EQU:
      O << "equ";
      return;
    case NVPTX::PTXCmpMode::NEU:
      O << "neu";
      return;
    case NVPTX::PTXCmpMode::LTU:
      O << "ltu";
      return;
    case NVPTX::PTXCmpMode::LEU:
      O << "leu";
      return;
    case NVPTX::PTXCmpMode::GTU:
      O << "gtu";
      return;
    case NVPTX::PTXCmpMode::GEU:
      O << "geu";
      return;
    case NVPTX::PTXCmpMode::NUM:
      O << "num";
      return;
    case NVPTX::PTXCmpMode::NotANumber:
      O << "nan";
      return;
    }
  }
  if (Modifier == "ICmp") {
    switch (Imm) {
    default:
      llvm_unreachable("Invalid ICmp mode");
    case NVPTX::PTXCmpMode::EQ:
      O << "eq";
      return;
    case NVPTX::PTXCmpMode::NE:
      O << "ne";
      return;
    case NVPTX::PTXCmpMode::LT:
    case NVPTX::PTXCmpMode::LTU:
      O << "lt";
      return;
    case NVPTX::PTXCmpMode::LE:
    case NVPTX::PTXCmpMode::LEU:
      O << "le";
      return;
    case NVPTX::PTXCmpMode::GT:
    case NVPTX::PTXCmpMode::GTU:
      O << "gt";
      return;
    case NVPTX::PTXCmpMode::GE:
    case NVPTX::PTXCmpMode::GEU:
      O << "ge";
      return;
    }
  }
  if (Modifier == "IType") {
    switch (Imm) {
    default:
      llvm_unreachable("Invalid IType");
    case NVPTX::PTXCmpMode::EQ:
    case NVPTX::PTXCmpMode::NE:
      O << "b";
      return;
    case NVPTX::PTXCmpMode::LT:
    case NVPTX::PTXCmpMode::LE:
    case NVPTX::PTXCmpMode::GT:
    case NVPTX::PTXCmpMode::GE:
      O << "s";
      return;
    case NVPTX::PTXCmpMode::LTU:
    case NVPTX::PTXCmpMode::LEU:
    case NVPTX::PTXCmpMode::GTU:
    case NVPTX::PTXCmpMode::GEU:
      O << "u";
      return;
    }
  }
  llvm_unreachable("Empty Modifier");
}

void NVPTXInstPrinter::printAtomicCode(const MCInst *MI, int OpNum,
                                       raw_ostream &O, StringRef Modifier) {
  const MCOperand &MO = MI->getOperand(OpNum);
  int Imm = (int)MO.getImm();
  if (Modifier == "sem") {
    auto Ordering = NVPTX::Ordering(Imm);
    switch (Ordering) {
    case NVPTX::Ordering::NotAtomic:
      return;
    case NVPTX::Ordering::Relaxed:
      O << ".relaxed";
      return;
    case NVPTX::Ordering::Acquire:
      O << ".acquire";
      return;
    case NVPTX::Ordering::Release:
      O << ".release";
      return;
    case NVPTX::Ordering::AcquireRelease:
      O << ".acq_rel";
      return;
    case NVPTX::Ordering::SequentiallyConsistent:
      O << ".seq_cst";
      return;
    case NVPTX::Ordering::Volatile:
      O << ".volatile";
      return;
    case NVPTX::Ordering::RelaxedMMIO:
      O << ".mmio.relaxed";
      return;
    }
  } else if (Modifier == "scope") {
    auto S = NVPTX::Scope(Imm);
    switch (S) {
    case NVPTX::Scope::Thread:
    case NVPTX::Scope::DefaultDevice:
      return;
    case NVPTX::Scope::System:
      O << ".sys";
      return;
    case NVPTX::Scope::Block:
      O << ".cta";
      return;
    case NVPTX::Scope::Cluster:
      O << ".cluster";
      return;
    case NVPTX::Scope::Device:
      O << ".gpu";
      return;
    }
    report_fatal_error(formatv(
        "NVPTX AtomicCode Printer does not support \"{}\" scope modifier.",
        ScopeToString(S)));
  } else if (Modifier == "addsp") {
    auto A = NVPTX::AddressSpace(Imm);
    switch (A) {
    case NVPTX::AddressSpace::Generic:
      return;
    case NVPTX::AddressSpace::Global:
    case NVPTX::AddressSpace::Const:
    case NVPTX::AddressSpace::Shared:
    case NVPTX::AddressSpace::SharedCluster:
    case NVPTX::AddressSpace::Param:
    case NVPTX::AddressSpace::Local:
      O << "." << A;
      return;
    }
    report_fatal_error(formatv(
        "NVPTX AtomicCode Printer does not support \"{}\" addsp modifier.",
        AddressSpaceToString(A)));
  } else if (Modifier == "sign") {
    switch (Imm) {
    case NVPTX::PTXLdStInstCode::Signed:
      O << "s";
      return;
    case NVPTX::PTXLdStInstCode::Unsigned:
      O << "u";
      return;
    case NVPTX::PTXLdStInstCode::Untyped:
      O << "b";
      return;
    case NVPTX::PTXLdStInstCode::Float:
      O << "f";
      return;
    default:
      llvm_unreachable("Unknown register type");
    }
  }
  llvm_unreachable(formatv("Unknown Modifier: {}", Modifier).str().c_str());
}

void NVPTXInstPrinter::printMmaCode(const MCInst *MI, int OpNum, raw_ostream &O,
                                    StringRef Modifier) {
  const MCOperand &MO = MI->getOperand(OpNum);
  int Imm = (int)MO.getImm();
  if (Modifier.empty() || Modifier == "version") {
    O << Imm; // Just print out PTX version
    return;
  } else if (Modifier == "aligned") {
    // PTX63 requires '.aligned' in the name of the instruction.
    if (Imm >= 63)
      O << ".aligned";
    return;
  }
  llvm_unreachable("Unknown Modifier");
}

void NVPTXInstPrinter::printMemOperand(const MCInst *MI, int OpNum,
                                       raw_ostream &O, StringRef Modifier) {
  printOperand(MI, OpNum, O);

  if (Modifier == "add") {
    O << ", ";
    printOperand(MI, OpNum + 1, O);
  } else {
    if (MI->getOperand(OpNum + 1).isImm() &&
        MI->getOperand(OpNum + 1).getImm() == 0)
      return; // don't print ',0' or '+0'
    O << "+";
    printOperand(MI, OpNum + 1, O);
  }
}

void NVPTXInstPrinter::printHexu32imm(const MCInst *MI, int OpNum,
                                      raw_ostream &O) {
  int64_t Imm = MI->getOperand(OpNum).getImm();
  O << formatHex(Imm) << "U";
}

void NVPTXInstPrinter::printProtoIdent(const MCInst *MI, int OpNum,
                                       raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNum);
  assert(Op.isExpr() && "Call prototype is not an MCExpr?");
  const MCExpr *Expr = Op.getExpr();
  const MCSymbol &Sym = cast<MCSymbolRefExpr>(Expr)->getSymbol();
  O << Sym.getName();
}

void NVPTXInstPrinter::printPrmtMode(const MCInst *MI, int OpNum,
                                     raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  int64_t Imm = MO.getImm();

  switch (Imm) {
  default:
    return;
  case NVPTX::PTXPrmtMode::NONE:
    return;
  case NVPTX::PTXPrmtMode::F4E:
    O << ".f4e";
    return;
  case NVPTX::PTXPrmtMode::B4E:
    O << ".b4e";
    return;
  case NVPTX::PTXPrmtMode::RC8:
    O << ".rc8";
    return;
  case NVPTX::PTXPrmtMode::ECL:
    O << ".ecl";
    return;
  case NVPTX::PTXPrmtMode::ECR:
    O << ".ecr";
    return;
  case NVPTX::PTXPrmtMode::RC16:
    O << ".rc16";
    return;
  }
}

void NVPTXInstPrinter::printTmaReductionMode(const MCInst *MI, int OpNum,
                                             raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  using RedTy = nvvm::TMAReductionOp;

  switch (static_cast<RedTy>(MO.getImm())) {
  case RedTy::ADD:
    O << ".add";
    return;
  case RedTy::MIN:
    O << ".min";
    return;
  case RedTy::MAX:
    O << ".max";
    return;
  case RedTy::INC:
    O << ".inc";
    return;
  case RedTy::DEC:
    O << ".dec";
    return;
  case RedTy::AND:
    O << ".and";
    return;
  case RedTy::OR:
    O << ".or";
    return;
  case RedTy::XOR:
    O << ".xor";
    return;
  }
  llvm_unreachable(
      "Invalid Reduction Op in printCpAsyncBulkTensorReductionMode");
}

void NVPTXInstPrinter::printCTAGroup(const MCInst *MI, int OpNum,
                                     raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  using CGTy = nvvm::CTAGroupKind;

  switch (static_cast<CGTy>(MO.getImm())) {
  case CGTy::CG_NONE:
    O << "";
    return;
  case CGTy::CG_1:
    O << ".cta_group::1";
    return;
  case CGTy::CG_2:
    O << ".cta_group::2";
    return;
  }
  llvm_unreachable("Invalid cta_group in printCTAGroup");
}

void NVPTXInstPrinter::printCallOperand(const MCInst *MI, int OpNum,
                                        raw_ostream &O, StringRef Modifier) {
  const MCOperand &MO = MI->getOperand(OpNum);
  assert(MO.isImm() && "Invalid operand");
  const auto Imm = MO.getImm();

  if (Modifier == "RetList") {
    assert((Imm == 1 || Imm == 0) && "Invalid return list");
    if (Imm)
      O << " (retval0),";
    return;
  }

  if (Modifier == "ParamList") {
    assert(Imm >= 0 && "Invalid parameter list");
    interleaveComma(llvm::seq(Imm), O,
                    [&](const auto &I) { O << "param" << I; });
    return;
  }
  llvm_unreachable("Invalid modifier");
}
