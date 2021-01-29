
//===-- UntrustedAlloc.cpp - UntrustedAlloc Infrastructure ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the common initialization infrastructure for the
// DynUntrustedAlloc library.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/IPO/DynUntrustedAllocPost.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"

#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#define DEBUG_TYPE "dyn-untrusted"

using namespace llvm;

namespace {
// Ensure we assign a unique ID to the same number of hooks as we made in the
// Pre pass.
uint64_t total_hooks = 0;
// Count the number of modified Alloc instructions
uint64_t modified_inst_count = 0;

/// A mapping between hook function and the position of the UniqueID argument.
const static std::map<std::string, int> patchArgIndexMap = {
    {"allocHook", 2}, {"reallocHook", 4}, {"deallocHook", 2}};

std::vector<Instruction *> hookList;

class IDGenerator {
  uint64_t id;

public:
  IDGenerator() : id(0) {}

  ConstantInt *getConstID(Module &M) {
    return llvm::ConstantInt::get(IntegerType::getInt64Ty(M.getContext()),
                                  id++);
  }
};

static IDGenerator IDG;

struct FaultingSite {
  uint64_t uniqueID;
  uint32_t pkey;
};

class DynUntrustedAllocPost : public ModulePass {
public:
  static char ID;
  std::string mpk_profile_path;
  bool remove_hooks;

  DynUntrustedAllocPost(std::string mpk_profile_path = "",
                        bool remove_hooks = false)
      : ModulePass(ID), mpk_profile_path(mpk_profile_path),
        remove_hooks(remove_hooks) {
    initializeDynUntrustedAllocPostPass(*PassRegistry::getPassRegistry());
  }
  virtual ~DynUntrustedAllocPost() = default;

  bool runOnModule(Module &M) override {
    // Post inliner pass, iterate over all functions and find hook CallSites.
    // Assign a unique ID in a deterministic pattern to ensure UniqueID is
    // consistent between runs.
    assignUniqueIDs(M);
    if (!mpk_profile_path.empty())
      fixFaultedAllocations(M, getFaultingAllocList());

    if (remove_hooks)
      removeHooks();

    printStats(M);

    return true;
  }

  bool fromJSON(const llvm::json::Value &Alloc, FaultingSite &F) {
    llvm::json::ObjectMapper O(Alloc);

    int64_t temp_id;
    bool temp_id_result = O.map("id", temp_id);
    if (temp_id < 0)
      return false;

    int64_t temp_pkey;
    bool temp_pkey_result = O.map("pkey", temp_pkey);
    if (temp_pkey < 0)
      return false;

    F.uniqueID = static_cast<uint64_t>(temp_id);
    F.pkey = static_cast<uint32_t>(temp_pkey);

    return O && temp_id_result && temp_pkey_result;
  }

  std::vector<FaultingSite> getFaultingAllocList() {
    std::vector<FaultingSite> fault_set;
    // If no path provided, return empty set.
    if (mpk_profile_path.empty())
      return fault_set;

    std::vector<std::string> fault_files;
    if (llvm::sys::fs::is_directory(mpk_profile_path)) {
      std::error_code EC;
      for (llvm::sys::fs::directory_iterator F(mpk_profile_path, EC), E;
           F != E && !EC; F.increment(EC)) {
        auto file_extension = llvm::sys::path::extension(F->path());
        if (StringSwitch<bool>(file_extension.lower())
                .Case(".json", true)
                .Default(false)) {
          fault_files.push_back(F->path());
        }
      }
    } else {
      fault_files.push_back(mpk_profile_path);
    }

    for (std::string file : fault_files) {
      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
          MemoryBuffer::getFile(file);
      std::error_code ec = File.getError();

      if (ec) {
        LLVM_DEBUG(errs() << "File could not be read: " << ec.message()
                          << "\n");
        return fault_set;
      }

      Expected<json::Value> ParseResult =
          json::parse(File.get().get()->getBuffer());
      if (Error E = ParseResult.takeError()) {
        LLVM_DEBUG(errs() << "Failed to Parse JSON array: " << E << "\n");
        consumeError(std::move(E));
        return fault_set;
      }

      if (!ParseResult->getAsArray()) {
        LLVM_DEBUG(errs() << "Failed to get JSON Value as JSON array.\n");
        return fault_set;
      }

      for (const auto &Alloc : *ParseResult->getAsArray()) {
        FaultingSite FS;
        if (fromJSON(Alloc, FS))
          fault_set.push_back(FS);
      }
    }

    LLVM_DEBUG(errs() << "Returning successful fault_set.\n");
    return fault_set;
  }

  static bool funcSort(Function *F1, Function *F2) {
    return F1->getName().str() > F2->getName().str();
  }

  void assignUniqueIDs(Module &M) {
    std::vector<Function *> WorkList;
    for (Function &F : M) {
      if (!F.isDeclaration())
        WorkList.push_back(&F);
    }

    std::sort(WorkList.begin(), WorkList.end(), funcSort);

    LLVM_DEBUG(errs() << "Search for modified functions!\n");

    for (Function *F : WorkList) {
      ReversePostOrderTraversal<Function *> RPOT(F);

      for (BasicBlock *BB : RPOT) {
        for (Instruction &I : *BB) {
          CallSite CS(&I);
          if (!CS) {
            continue;
          }

          Function *hook = CS.getCalledFunction();
          if (!hook)
            continue;

          // Get patch index from map.
          auto index_iter = patchArgIndexMap.find(hook->getName().str());
          if (index_iter == patchArgIndexMap.end())
            continue;

          auto index = index_iter->second;

          auto callInst = CS.getInstruction();
          BasicBlock::iterator iter(callInst);
          auto prev_inst = BB->getInstList().getPrevNode(*callInst);
          auto id = IDG.getConstID(M);

          CS.setArgument(index, id);
          ++total_hooks;
          LLVM_DEBUG(errs() << "modified callsite:\n");
          LLVM_DEBUG(errs() << *CS.getInstruction() << "\n");

          if (!prev_inst)
            continue;

          CallSite CSPrev(prev_inst);
          if (!CSPrev)
            continue;

          LLVM_DEBUG(errs() << "Adding: "
                            << CSPrev.getCalledFunction()->getName().data()
                            << " callsite for allocID: " << id->getZExtValue()
                            << "\n");
          alloc_map.insert(std::pair<uint64_t, Instruction *>(
              id->getZExtValue(), prev_inst));

          // If we are on final instrumentation, add to hookList to remove.
          if (remove_hooks)
            hookList.push_back(callInst);
        }
      }
    }
  }

  void fixFaultedAllocations(Module &M, std::vector<FaultingSite> FS) {
    if (FS.empty()) {
      return;
    }

    // Currently only patching __rust_alloc and __rust_alloc_zeroed
    const std::map<std::string, std::string> AllocReplacementMap = {
      {"__rust_alloc", "__rust_untrusted_alloc"},
      {"__rust_alloc_zeroed", "__rust_untrusted_alloc_zeroed"},
    };

    for (auto fsite : FS) { 
      auto map_iter = alloc_map.find(fsite.uniqueID);
      if (map_iter == alloc_map.end()) {
        LLVM_DEBUG(errs() << "Cannot find unique allocation id: "
                          << fsite.uniqueID << "\n");
        continue;
      }

      Instruction *I = map_iter->second;
      // We have already checked when adding instructions to the Faulting Set that they are all CallSites.
      CallSite AllocInst(I);

      auto F = AllocInst.getCalledFunction();
      if (!F) {
        LLVM_DEBUG(errs() << "CallSite does not contain a valid function call: " << *I << "\n");
        continue;
      }

      std::string ReplacementName;
      auto replIter = AllocReplacementMap.find(F->getName().str());

      if (replIter != AllocReplacementMap.end()) {
        ReplacementName = replIter->second;
      } else {
        continue;
      }

      Function *UntrustedAlloc = M.getFunction(ReplacementName);
      if (!UntrustedAlloc) {
        LLVM_DEBUG(errs() << "ERROR while creating patch: Could not find replacement: "
          << ReplacementName << "\n");
        continue;
      }

      if (CallInst *call = dyn_cast<CallInst>(I)) {
        LLVM_DEBUG(errs() << "Modifying CallInstruction: " << *call << "\n");
        call->setCalledFunction(UntrustedAlloc);
        ++modified_inst_count;
      }
    }
  }

  void removeHooks() {
    for (auto inst : hookList) {
      inst->removeFromParent();
    }
  }

  void printStats(Module &M) {
    std::string TestDirectory = "TestResults";
    if (!llvm::sys::fs::is_directory(TestDirectory))
      llvm::sys::fs::create_directory(TestDirectory);

    llvm::Expected<llvm::sys::fs::TempFile> PreStats =
        llvm::sys::fs::TempFile::create(TestDirectory +
                                        "/static-post-%%%%%%%.stat");
    if (!PreStats) {
      LLVM_DEBUG(errs() << "Error making unique filename: "
                        << llvm::toString(PreStats.takeError()) << "\n");
      return;
    }

    llvm::raw_fd_ostream OS(PreStats->FD, /* shouldClose */ false);
    OS << "Number of alloc instructions modified to unsafe: "
       << modified_inst_count << "\n"
       << "Total number hooks given a UniqueID: " << total_hooks << "\n";
    OS.flush();

    if (auto E = PreStats->keep()) {
      LLVM_DEBUG(errs() << "Error keeping pre-stats file: "
                        << llvm::toString(std::move(E)) << "\n");
      return;
    }
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<CallGraphWrapperPass>();
  }

private:
  std::map<uint64_t, Instruction *> alloc_map;
};

char DynUntrustedAllocPost::ID = 0;

} // namespace

INITIALIZE_PASS_BEGIN(DynUntrustedAllocPost, "dyn-untrusted-post",
                      "DynUntrustedAlloc: Patch allocation sites with dynamic "
                      "function hooks for tracking allocation IDs.",
                      false, false)
INITIALIZE_PASS_DEPENDENCY(CallGraphWrapperPass)
INITIALIZE_PASS_END(DynUntrustedAllocPost, "dyn-untrusted-post",
                    "DynUntrustedAlloc: Patch allocation sites with dynamic "
                    "function hooks for tracking allocation IDs.",
                    false, false)

ModulePass *
llvm::createDynUntrustedAllocPostPass(std::string mpk_profile_path = "",
                                      bool remove_hooks = false) {
  return new DynUntrustedAllocPost(mpk_profile_path, remove_hooks);
}

PreservedAnalyses DynUntrustedAllocPostPass::run(Module &M,
                                                 ModuleAnalysisManager &AM) {
  DynUntrustedAllocPost dyn(MPKProfilePath, RemoveHooks);
  if (!dyn.runOnModule(M)) {
    return PreservedAnalyses::all();
  }

  return PreservedAnalyses::none();
}
