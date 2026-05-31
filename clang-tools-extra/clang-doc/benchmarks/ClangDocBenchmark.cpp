//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains basic benchmarks for clang-doc's implementation and
/// library components.
///
//===----------------------------------------------------------------------===//

#include "BitcodeReader.h"
#include "BitcodeWriter.h"
#include "ClangDoc.h"
#include "Generators.h"
#include "Representation.h"
#include "Serialize.h"
#include "benchmark/benchmark.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Tooling/Execution.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/Parallel.h"
#include "llvm/Support/Threading.h"
#include <string>
#include <vector>

namespace clang {
namespace doc {

class BenchmarkVisitor : public RecursiveASTVisitor<BenchmarkVisitor> {
public:
  explicit BenchmarkVisitor(const FunctionDecl *&Func) : Func(Func) {}

  bool VisitFunctionDecl(const FunctionDecl *D) {
    if (D->getName() == "f") {
      Func = D;
      return false;
    }
    return true;
  }

private:
  const FunctionDecl *&Func;
};

// --- Mapper Benchmarks ---

static void BM_EmitInfoFunction(benchmark::State &State) {
  std::string Code = "void f() {}";
  std::unique_ptr<clang::ASTUnit> AST = clang::tooling::buildASTFromCode(Code);
  const FunctionDecl *Func = nullptr;
  BenchmarkVisitor Visitor(Func);
  Visitor.TraverseDecl(AST->getASTContext().getTranslationUnitDecl());
  assert(Func);

  clang::comments::FullComment *FC = nullptr;
  Location Loc;

  for (auto _ : State) {
    serialize::Serializer Serializer;
    auto Result = Serializer.emitInfo(Func, FC, Loc, /*PublicOnly=*/false);
    benchmark::DoNotOptimize(Result);
  }
}
BENCHMARK(BM_EmitInfoFunction);

static void BM_Mapper_Scale(benchmark::State &State) {
  std::string Code;
  for (int i = 0; i < State.range(0); ++i) {
    Code += "void f" + std::to_string(i) + "() {}\n";
  }

  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  DiagnosticOptions DiagOpts;
  DiagnosticsEngine Diags(DiagID, DiagOpts, new IgnoringDiagConsumer());

  for (auto _ : State) {
    tooling::InMemoryToolResults Results;
    tooling::ExecutionContext ECtx(&Results);
    ClangDocContext CDCtx(&ECtx, "test-project", false, "", "", "", "", "", {},
                          Diags, OutputFormatTy::json, false);
    auto ActionFactory = doc::newMapperActionFactory(CDCtx);
    std::unique_ptr<FrontendAction> Action = ActionFactory->create();
    tooling::runToolOnCode(std::move(Action), Code, "test.cpp");
  }
}
BENCHMARK(BM_Mapper_Scale)->Range(10, 10000);

// --- Reducer Benchmarks ---

static void BM_SerializeFunctionInfo(benchmark::State &State) {
  auto I = allocateTransient<FunctionInfo>();
  I->Name = "f";
  I->DefLoc = Location(0, 0, "test.cpp");
  I->ReturnType = TypeInfo("void");
  I->IT = InfoType::IT_function;

  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  DiagnosticOptions DiagOpts;
  DiagnosticsEngine Diags(DiagID, DiagOpts, new IgnoringDiagConsumer());

  Info *InfoPtr = I;

  for (auto _ : State) {
    auto Result = serialize::serialize(*InfoPtr, Diags);
    benchmark::DoNotOptimize(Result);
  }
}
BENCHMARK(BM_SerializeFunctionInfo);

static void BM_MergeInfos_Scale(benchmark::State &State) {
  SymbolID USR = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                  11, 12, 13, 14, 15, 16, 17, 18, 19, 20};

  for (auto _ : State) {
    State.PauseTiming();
    SmallVector<Info *> Input;
    Input.reserve(State.range(0));
    for (int Idx = 0; Idx < State.range(0); ++Idx) {
      auto *I = allocateTransient<FunctionInfo>();
      I->Name = "f";
      I->USR = USR;
      I->DefLoc = Location(10, Idx, "test.cpp");
      Input.push_back(std::move(I));
    }
    State.ResumeTiming();

    auto Result = doc::mergeInfos(Input);
    if (!Result) {
      State.SkipWithError("mergeInfos failed");
      llvm::consumeError(Result.takeError());
    }
    benchmark::DoNotOptimize(Result);
  }
}
BENCHMARK(BM_MergeInfos_Scale)->Range(2, 10000);

static void BM_BitcodeReader_Scale(benchmark::State &State) {
  int NumRecords = State.range(0);

  SmallString<0> Buffer;
  llvm::BitstreamWriter Stream(Buffer);
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  DiagnosticOptions DiagOpts;
  DiagnosticsEngine Diags(DiagID, DiagOpts, new IgnoringDiagConsumer());

  ClangDocBitcodeWriter Writer(Stream, Diags);
  for (int i = 0; i < NumRecords; ++i) {
    RecordInfo RI;
    RI.Name = internString("Record" + std::to_string(i));
    RI.USR = {(uint8_t)(i & 0xFF)};
    Writer.emitBlock(RI);
  }

  std::string BitcodeData = Buffer.str().str();

  for (auto _ : State) {
    llvm::BitstreamCursor Cursor(llvm::ArrayRef<uint8_t>(
        (const uint8_t *)BitcodeData.data(), BitcodeData.size()));
    ClangDocBitcodeReader Reader(Cursor, Diags);
    auto Result = Reader.readBitcode();
    if (!Result) {
      State.SkipWithError("readBitcode failed");
      llvm::consumeError(Result.takeError());
    }
    benchmark::DoNotOptimize(Result);
  }
}
BENCHMARK(BM_BitcodeReader_Scale)->Range(10, 10000);

// --- Generator Benchmarks ---

static void BM_JSONGenerator_Scale(benchmark::State &State) {
  auto G = doc::findGeneratorByName("json");
  if (!G) {
    State.SkipWithError("JSON Generator not found");
    llvm::consumeError(G.takeError());
    return;
  }
  int NumRecords = State.range(0);
  auto *NI = allocateTransient<NamespaceInfo>();
  NI->Name = "GlobalNamespace";
  for (int i = 0; i < NumRecords; ++i) {
    NI->Children.Records.push_back(*allocateListNodeTransient<Reference>(
        SymbolID{(uint8_t)(i & 0xFF)}, "Record" + std::to_string(i),
        InfoType::IT_record));
  }

  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  DiagnosticOptions DiagOpts;
  DiagnosticsEngine Diags(DiagID, DiagOpts, new IgnoringDiagConsumer());
  ClangDocContext CDCtx(nullptr, "test-project", false, "", "", "", "", "", {},
                        Diags, OutputFormatTy::json, false);

  std::string Output;
  llvm::raw_string_ostream OS(Output);

  for (auto _ : State) {
    Output.clear();
    auto Err = (*G)->generateDocForInfo(NI, OS, CDCtx);
    if (Err) {
      State.SkipWithError("generateDocForInfo failed");
      llvm::consumeError(std::move(Err));
    }
    benchmark::DoNotOptimize(Output);
  }
}
BENCHMARK(BM_JSONGenerator_Scale)->Range(10, 10000);

// --- Index Benchmarks ---

static void BM_Index_Insertion(benchmark::State &State) {
  for (auto _ : State) {
    Index Idx;
    for (int i = 0; i < State.range(0); ++i) {
      RecordInfo I;
      I.Name = internString("Record" + std::to_string(i));
      // Vary USR to ensure unique entries
      I.USR = {(uint8_t)(i & 0xFF), (uint8_t)((i >> 8) & 0xFF)};
      I.Path = internString("path/to/record");
      Generator::addInfoToIndex(Idx, &I);
    }
    benchmark::DoNotOptimize(Idx);
  }
}
BENCHMARK(BM_Index_Insertion)->Range(10, 10000);

// --- String Interning Benchmarks ---

// Build a large, deterministic distribution of distinct strings whose lengths
// vary widely (short identifiers through long qualified names), to exercise the
// global string pool the way mapping does.
static std::vector<std::string> makeInternStrings(size_t N) {
  static const char Charset[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_:";
  std::vector<std::string> Strings;
  Strings.reserve(N);
  for (size_t I = 0; I < N; ++I) {
    size_t Len = 4 + (I * 37) % 509;
    std::string S;
    S.reserve(Len);
    for (size_t J = 0; J < Len; ++J)
      S.push_back(Charset[(I * 131 + J * 31) % (sizeof(Charset) - 1)]);
    Strings.push_back(std::move(S));
  }
  return Strings;
}

// Stress interning throughput across threads. State.range(0) is the intern ops
// per iteration. Runs on llvm::parallel so workers have a valid
// getThreadIndex() for the pool's per-thread allocator.
static void BM_InternStringConcurrent(benchmark::State &State) {
  const size_t NumOps = State.range(0);
  static const std::vector<std::string> Strings = makeInternStrings(1 << 16);

  llvm::parallel::strategy = llvm::hardware_concurrency();

  for (auto _ : State) {
    llvm::parallelFor(0, NumOps, [&](size_t I) {
      benchmark::DoNotOptimize(internString(Strings[I % Strings.size()]));
    });
  }
  State.SetItemsProcessed(int64_t(State.iterations()) * NumOps);
}
BENCHMARK(BM_InternStringConcurrent)
    ->RangeMultiplier(8)
    ->Range(1 << 14, 1 << 20)
    ->UseRealTime();

} // namespace doc
} // namespace clang

BENCHMARK_MAIN();
