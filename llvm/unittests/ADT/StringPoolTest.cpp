//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/StringPool.h"
#include "llvm/Support/Parallel.h"
#include "gtest/gtest.h"
#include <string>

using namespace llvm;

namespace {

TEST(StringPoolTest, Insert) {
  StringPool Strings;

  // ConcurrentStringPool uses PerThreadBumpPtrAllocator, which must be accessed
  // from threads created by ThreadPoolExecutor. Use TaskGroup to run on
  // ThreadPoolExecutor threads.
  parallel::TaskGroup TG;

  TG.spawn([&]() {
    std::pair<StringEntry *, bool> Entry = Strings.insert("test");
    EXPECT_TRUE(Entry.second);
    EXPECT_EQ(Entry.first->getKey(), "test");

    StringEntry *EntryPtr = Entry.first;

    Entry = Strings.insert("test");
    EXPECT_FALSE(Entry.second);
    EXPECT_EQ(Entry.first->getKey(), "test");
    EXPECT_EQ(EntryPtr, Entry.first);

    Entry = Strings.insert("test2");
    EXPECT_TRUE(Entry.second);
    EXPECT_EQ(Entry.first->getKey(), "test2");
    EXPECT_NE(EntryPtr, Entry.first);
  });
}

TEST(StringPoolTest, Save) {
  StringPool Strings;

  parallel::TaskGroup TG;
  TG.spawn([&]() {
    StringRef A = Strings.save("hello");
    EXPECT_EQ(A, "hello");

    StringRef B = Strings.save("hello");
    EXPECT_EQ(B, "hello");
    EXPECT_EQ(A.data(), B.data());

    std::string Source = "world";
    StringRef C = Strings.save(Source);
    Source = "clobbered";
    EXPECT_EQ(C, "world");

    EXPECT_EQ(C.data(), Strings.insert("world").first->getKey().data());
  });
}

TEST(StringPoolTest, Parallel) {
  StringPool Strings;

  // Insert from many threads.
  parallelFor(0, 1000, [&](size_t Idx) {
    std::pair<StringEntry *, bool> Entry = Strings.insert(std::to_string(Idx));
    EXPECT_TRUE(Entry.second);
    EXPECT_EQ(Entry.first->getKey(), std::to_string(Idx));
  });

  parallelFor(0, 1000, [&](size_t Idx) {
    std::pair<StringEntry *, bool> Entry = Strings.insert(std::to_string(Idx));
    EXPECT_FALSE(Entry.second);
    EXPECT_EQ(Entry.first->getKey(), std::to_string(Idx));
  });
}

using TaggedEntry = StringMapEntry<unsigned>;

TEST(StringPoolTest, CustomEntry) {
  ConcurrentStringPool<TaggedEntry, StringPoolEntryInfo<TaggedEntry>> Strings;

  parallel::TaskGroup TG;
  TG.spawn([&]() {
    std::pair<TaggedEntry *, bool> Entry = Strings.insert("k");
    EXPECT_TRUE(Entry.second);
    EXPECT_EQ(Entry.first->getKey(), "k");
    Entry.first->setValue(42);

    std::pair<TaggedEntry *, bool> Again = Strings.insert("k");
    EXPECT_FALSE(Again.second);
    EXPECT_EQ(Again.first, Entry.first);
    EXPECT_EQ(Again.first->getValue(), 42u);
  });
}

} // anonymous namespace
