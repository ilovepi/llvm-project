//===- FileCheckBM.cpp - FileCheck matching benchmark ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "benchmark/benchmark.h"
#include "../lib/FileCheck/FileCheckImpl.h"
#include "llvm/FileCheck/FileCheck.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/Regex.h"
#include <string>
#include <vector>

using namespace llvm;
using namespace std;

// ... (BM_LineSplitting and BM_Fingerprinting remain the same) ...
// Benchmark line splitting
static void BM_LineSplitting(benchmark::State &state) {
  size_t HaystackSize = state.range(0);
  string Haystack(HaystackSize, 'a');
  for (size_t i = 0; i < HaystackSize; i += 80) {
    Haystack[i] = '\n';
  }
  StringRef HaystackRef(Haystack);

  for (auto _ : state) {
    auto Indices = buildLineIndexSIMD(HaystackRef);
    benchmark::DoNotOptimize(Indices);
  }
}
BENCHMARK(BM_LineSplitting)->RangeMultiplier(10)->Range(1000, 1000000);

// Benchmark fingerprinting
static void BM_Fingerprinting(benchmark::State &state) {
  size_t HaystackSize = state.range(0);
  string Haystack(HaystackSize, 'a');
  for (size_t i = 0; i < HaystackSize; i += 80) {
    Haystack[i] = '\n';
  }
  StringRef HaystackRef(Haystack);
  auto Indices = buildLineIndexSIMD(HaystackRef);

  for (auto _ : state) {
    auto Fingerprints = buildFingerprints(HaystackRef, Indices);
    benchmark::DoNotOptimize(Fingerprints);
  }
}
BENCHMARK(BM_Fingerprinting)->RangeMultiplier(10)->Range(1000, 1000000);

// Generic full file processing benchmark
static void BM_FileCheckFull(benchmark::State &state) {
  FileCheckRequest Req;
  Req.MatcherMode = static_cast<FileCheckMatcherMode>(state.range(0));
  Req.EnableSIMDLineSplitting = state.range(1) != 0;
  Req.EnableFingerprinting = state.range(2) != 0;
  bool IsDAG = state.range(3) != 0;
  
  size_t NumChecks = state.range(4);
  size_t HaystackSize = state.range(5);

  FileCheck FC(Req);
  SourceMgr SM;

  string CheckStr;
  string Prefix = IsDAG ? "CHECK-DAG: " : "CHECK: ";
  for (size_t i = 0; i < NumChecks; i++) {
    CheckStr += Prefix + "target_string_" + to_string(i) + "\n";
  }
  
  unsigned CheckBufferID = SM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(CheckStr, "CheckFile"), SMLoc());
  
  StringRef CheckBufferRef = SM.getMemoryBuffer(CheckBufferID)->getBuffer();
  FC.readCheckFile(SM, CheckBufferRef);

  string Haystack(HaystackSize, 'a');
  size_t Step = HaystackSize / (NumChecks + 1);
  for (size_t i = 0; i < NumChecks; i++) {
    size_t Pos = (i + 1) * Step;
    string Target = "target_string_" + to_string(i);
    if (Pos + Target.size() < HaystackSize) {
      Haystack.replace(Pos, Target.size(), Target);
    }
  }
  
  StringRef HaystackRef(Haystack);

  for (auto _ : state) {
    bool Success = FC.checkInput(SM, HaystackRef);
    benchmark::DoNotOptimize(Success);
  }
}

// Scenario 1: Fingerprinting shines (Many checks that FAIL to match)
static void BM_FingerprintShines(benchmark::State &state) {
  FileCheckRequest Req;
  Req.MatcherMode = static_cast<FileCheckMatcherMode>(state.range(0));
  Req.EnableSIMDLineSplitting = state.range(1) != 0;
  Req.EnableFingerprinting = state.range(2) != 0;
  
  size_t NumChecks = 100;
  size_t HaystackSize = 100000;

  FileCheck FC(Req);
  SourceMgr SM;

  // Checks that WILL NOT match
  string CheckStr;
  for (size_t i = 0; i < NumChecks; i++) {
    CheckStr += "CHECK: non_existent_string_" + to_string(i) + "\n";
  }
  
  unsigned CheckBufferID = SM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(CheckStr, "CheckFile"), SMLoc());
  
  StringRef CheckBufferRef = SM.getMemoryBuffer(CheckBufferID)->getBuffer();
  FC.readCheckFile(SM, CheckBufferRef);

  // Haystack without those strings
  string Haystack(HaystackSize, 'a');
  for (size_t i = 0; i < HaystackSize; i += 80) {
    Haystack[i] = '\n';
  }
  
  unsigned HaystackBufferID = SM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(Haystack, "InputFile"), SMLoc());
  StringRef HaystackRef = SM.getMemoryBuffer(HaystackBufferID)->getBuffer();

  std::vector<FileCheckDiag> Diags;
  for (auto _ : state) {
    FC.checkInput(SM, HaystackRef, &Diags);
    Diags.clear();
  }
}

// Scenario 2: Baseline stands out (False positives for SIMD, long needles)
static void BM_BaselineStandsOut(benchmark::State &state) {
  FileCheckRequest Req;
  Req.MatcherMode = static_cast<FileCheckMatcherMode>(state.range(0));
  Req.EnableSIMDLineSplitting = state.range(1) != 0;
  Req.EnableFingerprinting = state.range(2) != 0;
  
  size_t HaystackSize = 100000;
  // Long needle
  string Needle = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"; // 64 bytes
  
  FileCheck FC(Req);
  SourceMgr SM;

  string CheckStr = "CHECK: " + Needle + "\n";
  unsigned CheckBufferID = SM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(CheckStr, "CheckFile"), SMLoc());
  
  StringRef CheckBufferRef = SM.getMemoryBuffer(CheckBufferID)->getBuffer();
  FC.readCheckFile(SM, CheckBufferRef);

  // Haystack with frequent partial matches ("bb") but no full match
  string Haystack;
  Haystack.reserve(HaystackSize);
  while (Haystack.size() < HaystackSize) {
    Haystack += "bb";
    Haystack += string(62, 'a');
  }
  
  unsigned HaystackBufferID = SM.AddNewSourceBuffer(
      MemoryBuffer::getMemBuffer(Haystack, "InputFile"), SMLoc());
  StringRef HaystackRef = SM.getMemoryBuffer(HaystackBufferID)->getBuffer();

  std::vector<FileCheckDiag> Diags;
  for (auto _ : state) {
    FC.checkInput(SM, HaystackRef, &Diags);
    Diags.clear();
  }
}

BENCHMARK(BM_FileCheckFull)
  ->Args({0, 0, 0, 0, 1, 100000})
  ->Args({1, 0, 0, 0, 1, 100000})
  ->Args({0, 0, 0, 0, 500, 100000})
  ->Args({1, 0, 0, 0, 500, 100000})
  ->Args({1, 1, 1, 0, 500, 100000}) // SIMD + Fingerprinting
  ->Args({0, 0, 0, 1, 500, 100000})
  ->Args({1, 0, 0, 1, 500, 100000})
  ->Args({1, 1, 1, 1, 500, 100000}); // SIMD + Fingerprinting

BENCHMARK(BM_FingerprintShines)
  ->Args({0, 0, 0}) // Standard
  ->Args({1, 0, 0}) // SIMD only
  ->Args({1, 1, 1}); // SIMD + Fingerprinting

BENCHMARK(BM_BaselineStandsOut)
  ->Args({0, 0, 0}) // Standard
  ->Args({1, 0, 0}) // SIMD only
  ->Args({1, 1, 1}); // SIMD + Fingerprinting

BENCHMARK_MAIN();
