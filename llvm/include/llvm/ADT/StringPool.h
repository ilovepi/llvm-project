//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines a concurrent string pool: a thread-safe, deduplicating
// owner of string data backed by a ConcurrentHashTable and a
// PerThreadBumpPtrAllocator.
//
// The pool is templated on the entry type so consumers that need to attach data
// to each interned string can supply their own entry.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_STRINGPOOL_H
#define LLVM_ADT_STRINGPOOL_H

#include "llvm/ADT/ConcurrentHashtable.h"
#include "llvm/ADT/StringMapEntry.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/PerThreadBumpPtrAllocator.h"

namespace llvm {

/// StringEntry keeps data of the string: the length, external offset and a
/// string body which is placed right after StringEntry.
using StringEntry = StringMapEntry<EmptyStringSetTag>;

template <typename EntryTy = StringEntry> class StringPoolEntryInfo {
public:
  /// \returns Hash value for the specified \p Key.
  static inline uint64_t getHashValue(StringRef Key) {
    return xxh3_64bits(Key);
  }

  /// \returns true if both \p LHS and \p RHS are equal.
  static inline bool isEqual(StringRef LHS, StringRef RHS) {
    return LHS == RHS;
  }

  /// \returns key for the specified \p KeyData.
  static inline StringRef getKey(const EntryTy &KeyData) {
    return KeyData.getKey();
  }

  /// \returns newly created object of EntryTy type.
  static inline EntryTy *
  create(StringRef Key, llvm::parallel::PerThreadBumpPtrAllocator &Allocator) {
    return EntryTy::create(Key, Allocator);
  }
};

/// A concurrent, deduplicating string pool.
template <typename EntryTy = StringEntry,
          typename EntryInfoTy = StringPoolEntryInfo<EntryTy>>
class ConcurrentStringPool
    : public ConcurrentHashTableByPtr<StringRef, EntryTy,
                                      llvm::parallel::PerThreadBumpPtrAllocator,
                                      EntryInfoTy> {
  using BaseTy =
      ConcurrentHashTableByPtr<StringRef, EntryTy,
                               llvm::parallel::PerThreadBumpPtrAllocator,
                               EntryInfoTy>;

public:
  ConcurrentStringPool() : BaseTy(Allocator) {}
  ConcurrentStringPool(size_t InitialSize) : BaseTy(Allocator, InitialSize) {}

  /// Intern \p S and return a stable StringRef owned by the pool.
  StringRef save(StringRef S) { return this->insert(S).first->getKey(); }

  llvm::parallel::PerThreadBumpPtrAllocator &getAllocatorRef() {
    return Allocator;
  }

  void clear() { Allocator.Reset(); }

private:
  llvm::parallel::PerThreadBumpPtrAllocator Allocator;
};

/// An alias for typical usage. Essentially a concurrent UniqueStringSaver.
using StringPool = ConcurrentStringPool<>;

} // end namespace llvm

#endif // LLVM_ADT_STRINGPOOL_H
