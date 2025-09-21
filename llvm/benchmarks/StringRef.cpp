//===- StringRef.cpp - StringRef benchmarks ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "benchmark/benchmark.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

using namespace llvm;

// This helper function creates a long string of a given size and a smaller
// "needle" string. It then places the needle at a specified offset within the
// larger string. The data is made less uniform to make the benchmark more
// realistic.
static std::string createHaystack(size_t HaystackSize, size_t NeedleSize,
                                  size_t NeedleOffset,
                                  std::string &Needle) {
  assert(NeedleSize + NeedleOffset <= HaystackSize);
  std::string Haystack;
  Haystack.reserve(HaystackSize);
  for (size_t i = 0; i < HaystackSize; ++i) {
    Haystack += (i % 2 == 0) ? 'a' : 'b';
  }

  Needle.reserve(NeedleSize);
  for (size_t i = 0; i < NeedleSize; ++i) {
    Needle += (i % 2 == 0) ? 'c' : 'd';
  }

  Haystack.replace(NeedleOffset, NeedleSize, Needle);
  return Haystack;
}

constexpr int WorkFactor = 10;

// Benchmark finding a needle at the beginning of a haystack.
static void BM_StringRefFind_First(benchmark::State &state) {
  size_t HaystackSize = state.range(0);
  size_t NeedleSize = state.range(1);
  std::string Needle;
  std::string HaystackStr =
      createHaystack(HaystackSize, NeedleSize, 0, Needle);
  StringRef Haystack(HaystackStr);

  for (auto _ : state) {
    for (int i = 0; i < WorkFactor; ++i)
      benchmark::DoNotOptimize(Haystack.find(Needle));
  }
  state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(HaystackSize) * WorkFactor);
}

// Benchmark finding a needle in the middle of a haystack.
static void BM_StringRefFind_Middle(benchmark::State &state) {
  size_t HaystackSize = state.range(0);
  size_t NeedleSize = state.range(1);
  std::string Needle;
  size_t NeedleOffset = HaystackSize / 2 - NeedleSize / 2;
  std::string HaystackStr =
      createHaystack(HaystackSize, NeedleSize, NeedleOffset, Needle);
  StringRef Haystack(HaystackStr);

  for (auto _ : state) {
    for (int i = 0; i < WorkFactor; ++i)
      benchmark::DoNotOptimize(Haystack.find(Needle));
  }
  state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(HaystackSize) * WorkFactor);
}

// Benchmark finding a needle at the end of a haystack.
static void BM_StringRefFind_Last(benchmark::State &state) {
  size_t HaystackSize = state.range(0);
  size_t NeedleSize = state.range(1);
  std::string Needle;
  size_t NeedleOffset = HaystackSize - NeedleSize;
  std::string HaystackStr =
      createHaystack(HaystackSize, NeedleSize, NeedleOffset, Needle);
  StringRef Haystack(HaystackStr);

  for (auto _ : state) {
    for (int i = 0; i < WorkFactor; ++i)
      benchmark::DoNotOptimize(Haystack.find(Needle));
  }
  state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(HaystackSize) * WorkFactor);
}

// Benchmark the case where the needle is not found.
static void BM_StringRefFind_NotFound(benchmark::State &state) {
  size_t HaystackSize = state.range(0);
  size_t NeedleSize = state.range(1);
  std::string Haystack;
  Haystack.reserve(HaystackSize);
  for (size_t i = 0; i < HaystackSize; ++i) {
    Haystack += (i % 2 == 0) ? 'a' : 'b';
  }
  std::string Needle(NeedleSize, 'c');
  StringRef HaystackRef(Haystack);

  for (auto _ : state) {
    for (int i = 0; i < WorkFactor; ++i)
      benchmark::DoNotOptimize(HaystackRef.find(Needle));
  }
  state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(HaystackSize) * WorkFactor);
}

static void CustomArguments(benchmark::internal::Benchmark* b) {
  const int NeedleSizes[] = {1, 2, 3, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
  for (int i = 1 << 6; i <= 1 << 13; i *= 2) {
    for (int j : NeedleSizes) {
      if (j < i) {
        b->Args({i, j});
      }
    }
  }
}

BENCHMARK(BM_StringRefFind_First)->Apply(CustomArguments);
BENCHMARK(BM_StringRefFind_Middle)->Apply(CustomArguments);
BENCHMARK(BM_StringRefFind_Last)->Apply(CustomArguments);
BENCHMARK(BM_StringRefFind_NotFound)->Apply(CustomArguments);

BENCHMARK_MAIN();
