//===------ LazyReexports.h -- Utilities for lazy reexports -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lazy re-exports are similar to normal re-exports, except that for callable
// symbols the definitions are replaced with trampolines that will look up and
// call through to the re-exported symbol at runtime. This can be used to
// enable lazy compilation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LAZYREEXPORTS_H
#define LLVM_EXECUTIONENGINE_ORC_LAZYREEXPORTS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/RedirectionManager.h"
#include "llvm/ExecutionEngine/Orc/Speculation.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Triple;

namespace orc {

/// Manages a set of 'lazy call-through' trampolines. These are compiler
/// re-entry trampolines that are pre-bound to look up a given symbol in a given
/// JITDylib, then jump to that address. Since compilation of symbols is
/// triggered on first lookup, these call-through trampolines can be used to
/// implement lazy compilation.
///
/// The easiest way to construct these call-throughs is using the lazyReexport
/// function.
class LazyCallThroughManager {
public:
  using NotifyResolvedFunction =
      unique_function<Error(ExecutorAddr ResolvedAddr)>;

  LLVM_ABI LazyCallThroughManager(ExecutionSession &ES,
                                  ExecutorAddr ErrorHandlerAddr,
                                  TrampolinePool *TP);

  // Return a free call-through trampoline and bind it to look up and call
  // through to the given symbol.
  LLVM_ABI Expected<ExecutorAddr>
  getCallThroughTrampoline(JITDylib &SourceJD, SymbolStringPtr SymbolName,
                           NotifyResolvedFunction NotifyResolved);

  LLVM_ABI void resolveTrampolineLandingAddress(
      ExecutorAddr TrampolineAddr,
      TrampolinePool::NotifyLandingResolvedFunction NotifyLandingResolved);

  virtual ~LazyCallThroughManager() = default;

protected:
  using NotifyLandingResolvedFunction =
      TrampolinePool::NotifyLandingResolvedFunction;

  struct ReexportsEntry {
    JITDylib *SourceJD;
    SymbolStringPtr SymbolName;
  };

  LLVM_ABI ExecutorAddr reportCallThroughError(Error Err);
  LLVM_ABI Expected<ReexportsEntry> findReexport(ExecutorAddr TrampolineAddr);
  LLVM_ABI Error notifyResolved(ExecutorAddr TrampolineAddr,
                                ExecutorAddr ResolvedAddr);
  void setTrampolinePool(TrampolinePool &TP) { this->TP = &TP; }

private:
  using ReexportsMap = std::map<ExecutorAddr, ReexportsEntry>;

  using NotifiersMap = std::map<ExecutorAddr, NotifyResolvedFunction>;

  std::mutex LCTMMutex;
  ExecutionSession &ES;
  ExecutorAddr ErrorHandlerAddr;
  TrampolinePool *TP = nullptr;
  ReexportsMap Reexports;
  NotifiersMap Notifiers;
};

/// A lazy call-through manager that builds trampolines in the current process.
class LocalLazyCallThroughManager : public LazyCallThroughManager {
private:
  using NotifyTargetResolved = unique_function<void(ExecutorAddr)>;

  LocalLazyCallThroughManager(ExecutionSession &ES,
                              ExecutorAddr ErrorHandlerAddr)
      : LazyCallThroughManager(ES, ErrorHandlerAddr, nullptr) {}

  template <typename ORCABI> Error init() {
    auto TP = LocalTrampolinePool<ORCABI>::Create(
        [this](ExecutorAddr TrampolineAddr,
               TrampolinePool::NotifyLandingResolvedFunction
                   NotifyLandingResolved) {
          resolveTrampolineLandingAddress(TrampolineAddr,
                                          std::move(NotifyLandingResolved));
        });

    if (!TP)
      return TP.takeError();

    this->TP = std::move(*TP);
    setTrampolinePool(*this->TP);
    return Error::success();
  }

  std::unique_ptr<TrampolinePool> TP;

public:
  /// Create a LocalLazyCallThroughManager using the given ABI. See
  /// createLocalLazyCallThroughManager.
  template <typename ORCABI>
  static Expected<std::unique_ptr<LocalLazyCallThroughManager>>
  Create(ExecutionSession &ES, ExecutorAddr ErrorHandlerAddr) {
    auto LLCTM = std::unique_ptr<LocalLazyCallThroughManager>(
        new LocalLazyCallThroughManager(ES, ErrorHandlerAddr));

    if (auto Err = LLCTM->init<ORCABI>())
      return std::move(Err);

    return std::move(LLCTM);
  }
};

/// Create a LocalLazyCallThroughManager from the given triple and execution
/// session.
LLVM_ABI Expected<std::unique_ptr<LazyCallThroughManager>>
createLocalLazyCallThroughManager(const Triple &T, ExecutionSession &ES,
                                  ExecutorAddr ErrorHandlerAddr);

/// A materialization unit that builds lazy re-exports. These are callable
/// entry points that call through to the given symbols.
/// Unlike a 'true' re-export, the address of the lazy re-export will not
/// match the address of the re-exported symbol, but calling it will behave
/// the same as calling the re-exported symbol.
class LLVM_ABI LazyReexportsMaterializationUnit : public MaterializationUnit {
public:
  LazyReexportsMaterializationUnit(LazyCallThroughManager &LCTManager,
                                   RedirectableSymbolManager &RSManager,
                                   JITDylib &SourceJD,
                                   SymbolAliasMap CallableAliases,
                                   ImplSymbolMap *SrcJDLoc);

  StringRef getName() const override;

private:
  void materialize(std::unique_ptr<MaterializationResponsibility> R) override;
  void discard(const JITDylib &JD, const SymbolStringPtr &Name) override;
  static MaterializationUnit::Interface
  extractFlags(const SymbolAliasMap &Aliases);

  LazyCallThroughManager &LCTManager;
  RedirectableSymbolManager &RSManager;
  JITDylib &SourceJD;
  SymbolAliasMap CallableAliases;
  ImplSymbolMap *AliaseeTable;
};

/// Define lazy-reexports based on the given SymbolAliasMap. Each lazy re-export
/// is a callable symbol that will look up and dispatch to the given aliasee on
/// first call. All subsequent calls will go directly to the aliasee.
inline std::unique_ptr<LazyReexportsMaterializationUnit>
lazyReexports(LazyCallThroughManager &LCTManager,
              RedirectableSymbolManager &RSManager, JITDylib &SourceJD,
              SymbolAliasMap CallableAliases,
              ImplSymbolMap *SrcJDLoc = nullptr) {
  return std::make_unique<LazyReexportsMaterializationUnit>(
      LCTManager, RSManager, SourceJD, std::move(CallableAliases), SrcJDLoc);
}

class LLVM_ABI LazyReexportsManager : public ResourceManager {

  friend std::unique_ptr<MaterializationUnit>
  lazyReexports(LazyReexportsManager &, SymbolAliasMap);

public:
  struct CallThroughInfo {
    JITDylibSP JD;
    SymbolStringPtr Name;
    SymbolStringPtr BodyName;
  };

  class LLVM_ABI Listener {
  public:
    using CallThroughInfo = LazyReexportsManager::CallThroughInfo;

    virtual ~Listener();

    /// Called under the session lock when new lazy reexports are created.
    virtual void onLazyReexportsCreated(JITDylib &JD, ResourceKey K,
                                        const SymbolAliasMap &Reexports) = 0;

    /// Called under the session lock when lazy reexports have their ownership
    /// transferred to a new ResourceKey.
    virtual void onLazyReexportsTransfered(JITDylib &JD, ResourceKey DstK,
                                           ResourceKey SrcK) = 0;

    /// Called under the session lock when lazy reexports are removed.
    virtual Error onLazyReexportsRemoved(JITDylib &JD, ResourceKey K) = 0;

    /// Called outside the session lock when a lazy reexport is called.
    /// NOTE: Since this is called outside the session lock there is a chance
    ///       that the reexport referred to has already been removed. Listeners
    ///       must be prepared to handle requests for stale reexports.
    virtual void onLazyReexportCalled(const CallThroughInfo &CTI) = 0;
  };

  using OnTrampolinesReadyFn = unique_function<void(
      Expected<std::vector<ExecutorSymbolDef>> EntryAddrs)>;
  using EmitTrampolinesFn =
      unique_function<void(ResourceTrackerSP RT, size_t NumTrampolines,
                           OnTrampolinesReadyFn OnTrampolinesReady)>;

  /// Create a LazyReexportsManager that uses the ORC runtime for reentry.
  /// This will work both in-process and out-of-process.
  static Expected<std::unique_ptr<LazyReexportsManager>>
  Create(EmitTrampolinesFn EmitTrampolines, RedirectableSymbolManager &RSMgr,
         JITDylib &PlatformJD, Listener *L = nullptr);

  LazyReexportsManager(LazyReexportsManager &&) = delete;
  LazyReexportsManager &operator=(LazyReexportsManager &&) = delete;

  Error handleRemoveResources(JITDylib &JD, ResourceKey K) override;
  void handleTransferResources(JITDylib &JD, ResourceKey DstK,
                               ResourceKey SrcK) override;

private:
  class MU;
  class Plugin;

  using ResolveSendResultFn =
      unique_function<void(Expected<ExecutorSymbolDef>)>;

  LazyReexportsManager(EmitTrampolinesFn EmitTrampolines,
                       RedirectableSymbolManager &RSMgr, JITDylib &PlatformJD,
                       Listener *L, Error &Err);

  std::unique_ptr<MaterializationUnit>
  createLazyReexports(SymbolAliasMap Reexports);

  void emitReentryTrampolines(std::unique_ptr<MaterializationResponsibility> MR,
                              SymbolAliasMap Reexports);
  void emitRedirectableSymbols(
      std::unique_ptr<MaterializationResponsibility> MR,
      SymbolAliasMap Reexports,
      Expected<std::vector<ExecutorSymbolDef>> ReentryPoints);
  void resolve(ResolveSendResultFn SendResult, ExecutorAddr ReentryStubAddr);

  ExecutionSession &ES;
  EmitTrampolinesFn EmitTrampolines;
  RedirectableSymbolManager &RSMgr;
  Listener *L;

  DenseMap<ResourceKey, std::vector<ExecutorAddr>> KeyToReentryAddrs;
  DenseMap<ExecutorAddr, CallThroughInfo> CallThroughs;
};

/// Define lazy-reexports based on the given SymbolAliasMap. Each lazy re-export
/// is a callable symbol that will look up and dispatch to the given aliasee on
/// first call. All subsequent calls will go directly to the aliasee.
inline std::unique_ptr<MaterializationUnit>
lazyReexports(LazyReexportsManager &LRM, SymbolAliasMap Reexports) {
  return LRM.createLazyReexports(std::move(Reexports));
}

class LLVM_ABI SimpleLazyReexportsSpeculator
    : public LazyReexportsManager::Listener {
public:
  using RecordExecutionFunction =
      unique_function<void(const CallThroughInfo &CTI)>;

  static std::shared_ptr<SimpleLazyReexportsSpeculator>
  Create(ExecutionSession &ES, RecordExecutionFunction RecordExec = {}) {
    class make_shared_helper : public SimpleLazyReexportsSpeculator {
    public:
      make_shared_helper(ExecutionSession &ES,
                         RecordExecutionFunction RecordExec)
          : SimpleLazyReexportsSpeculator(ES, std::move(RecordExec)) {}
    };

    auto Instance =
        std::make_shared<make_shared_helper>(ES, std::move(RecordExec));
    Instance->WeakThis = Instance;
    return Instance;
  }

  SimpleLazyReexportsSpeculator(SimpleLazyReexportsSpeculator &&) = delete;
  SimpleLazyReexportsSpeculator &
  operator=(SimpleLazyReexportsSpeculator &&) = delete;
  ~SimpleLazyReexportsSpeculator() override;

  void onLazyReexportsCreated(JITDylib &JD, ResourceKey K,
                              const SymbolAliasMap &Reexports) override;

  void onLazyReexportsTransfered(JITDylib &JD, ResourceKey DstK,
                                 ResourceKey SrcK) override;

  Error onLazyReexportsRemoved(JITDylib &JD, ResourceKey K) override;

  void onLazyReexportCalled(const CallThroughInfo &CTI) override;

  void addSpeculationSuggestions(
      std::vector<std::pair<std::string, SymbolStringPtr>> NewSuggestions);

private:
  SimpleLazyReexportsSpeculator(ExecutionSession &ES,
                                RecordExecutionFunction RecordExec)
      : ES(ES), RecordExec(std::move(RecordExec)) {}

  bool doNextSpeculativeLookup();

  class SpeculateTask;

  using KeyToFunctionBodiesMap =
      DenseMap<ResourceKey, std::vector<SymbolStringPtr>>;

  ExecutionSession &ES;
  RecordExecutionFunction RecordExec;
  std::weak_ptr<SimpleLazyReexportsSpeculator> WeakThis;
  DenseMap<JITDylib *, KeyToFunctionBodiesMap> LazyReexports;
  std::deque<std::pair<std::string, SymbolStringPtr>> SpeculateSuggestions;
  bool SpeculateTaskActive = false;
};

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LAZYREEXPORTS_H
