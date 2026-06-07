//===--  BitcodeReader.cpp - ClangDoc Bitcode Reader ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BitcodeReader.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <utility>

namespace clang {
namespace doc {

static llvm::ExitOnError ExitOnErr("clang-doc error: ");

using Record = llvm::SmallVector<uint64_t, 1024>;

// This implements decode for SmallString.
static llvm::Error decodeRecord(const Record &R,
                                llvm::SmallVectorImpl<char> &Field,
                                llvm::StringRef Blob) {
  Field.assign(Blob.begin(), Blob.end());
  return llvm::Error::success();
}

static llvm::Error decodeRecord(const Record &R, llvm::StringRef &Field,
                                llvm::StringRef Blob) {
  Field = internString(Blob);
  return llvm::Error::success();
}

static llvm::Error decodeRecord(const Record &R, SymbolID &Field,
                                llvm::StringRef Blob) {
  if (R.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "empty record for SymbolID");
  if (R[0] != BitCodeConstants::USRHashSize)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "incorrect USR size");
  if (R.size() < R[0] + 1)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "record too short for SymbolID");

  // First position in the record is the length of the following array, so we
  // copy the following elements to the field.
  for (int I = 0, E = R[0]; I < E; ++I)
    Field[I] = R[I + 1];
  return llvm::Error::success();
}

static llvm::Error decodeRecord(const Record &R, bool &Field,
                                llvm::StringRef Blob) {
  if (R.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "empty record for bool");
  Field = R[0] != 0;
  return llvm::Error::success();
}

static llvm::Error decodeRecord(const Record &R, AccessSpecifier &Field,
                                llvm::StringRef Blob) {
  if (R.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "empty record for AccessSpecifier");
  switch (R[0]) {
  case AS_public:
  case AS_private:
  case AS_protected:
  case AS_none:
    Field = (AccessSpecifier)R[0];
    return llvm::Error::success();
  }
  llvm_unreachable("invalid value for AccessSpecifier");
}

static llvm::Error decodeRecord(const Record &R, TagTypeKind &Field,
                                llvm::StringRef Blob) {
  if (R.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "empty record for TagTypeKind");
  switch (static_cast<TagTypeKind>(R[0])) {
  case TagTypeKind::Struct:
  case TagTypeKind::Interface:
  case TagTypeKind::Union:
  case TagTypeKind::Class:
  case TagTypeKind::Enum:
    Field = static_cast<TagTypeKind>(R[0]);
    return llvm::Error::success();
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "invalid value for TagTypeKind");
}

static llvm::Error decodeRecord(const Record &R, std::optional<Location> &Field,
                                llvm::StringRef Blob) {
  if (R.size() < 3)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "record too short for Location");
  if (R[0] > INT_MAX)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "integer too large to parse");
  Field.emplace(static_cast<int>(R[0]), static_cast<int>(R[1]), Blob,
                static_cast<bool>(R[2]));
  return llvm::Error::success();
}

static llvm::Error decodeRecord(const Record &R, InfoType &Field,
                                llvm::StringRef Blob) {
  if (R.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "empty record for InfoType");
  switch (auto IT = static_cast<InfoType>(R[0])) {
  case InfoType::IT_namespace:
  case InfoType::IT_record:
  case InfoType::IT_function:
  case InfoType::IT_default:
  case InfoType::IT_enum:
  case InfoType::IT_typedef:
  case InfoType::IT_concept:
  case InfoType::IT_variable:
  case InfoType::IT_friend:
    Field = IT;
    return llvm::Error::success();
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "invalid value for InfoType");
}

static llvm::Error decodeRecord(const Record &R, FieldId &Field,
                                llvm::StringRef Blob) {
  if (R.empty())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "empty record for FieldId");
  switch (auto F = static_cast<FieldId>(R[0])) {
  case FieldId::F_namespace:
  case FieldId::F_parent:
  case FieldId::F_vparent:
  case FieldId::F_type:
  case FieldId::F_child_namespace:
  case FieldId::F_child_record:
  case FieldId::F_concept:
  case FieldId::F_friend:
  case FieldId::F_default:
    Field = F;
    return llvm::Error::success();
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "invalid value for FieldId");
}

static llvm::Error decodeRecord(const Record &R, DocList<Location> &Field,
                                llvm::StringRef Blob) {
  if (R.size() < 3)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "record too short for Location");
  if (R[0] > INT_MAX)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "integer too large to parse");

  Field.push_back(*allocateListNodeTransient<Location>(
      static_cast<int>(R[0]), static_cast<int>(R[1]), Blob,
      static_cast<bool>(R[2])));
  return llvm::Error::success();
}

static llvm::Error parseRecord(const Record &R, unsigned ID,
                               llvm::StringRef Blob, const unsigned VersionNo) {
  if (ID == VERSION && R[0] == VersionNo)
    return llvm::Error::success();
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "mismatched bitcode version number");
}

// Generate a parseRecord() overload for each block in ClangDocRecords.def.
#define CLANG_DOC_RECORD_READER
#include "ClangDocRecords.def"

static llvm::Error parseRecord(const Record &R, unsigned ID,
                               llvm::StringRef Blob, CommentInfo *I,
                               llvm::SmallVectorImpl<StringRef> &AttrKeys,
                               llvm::SmallVectorImpl<StringRef> &AttrValues,
                               llvm::SmallVectorImpl<StringRef> &Args) {
  llvm::SmallString<16> KindStr;
  switch (ID) {
  case COMMENT_KIND:
    if (llvm::Error Err = decodeRecord(R, KindStr, Blob))
      return Err;
    I->Kind = stringToCommentKind(KindStr);
    return llvm::Error::success();
  case COMMENT_TEXT:
    return decodeRecord(R, I->Text, Blob);
  case COMMENT_NAME:
    return decodeRecord(R, I->Name, Blob);
  case COMMENT_DIRECTION:
    return decodeRecord(R, I->Direction, Blob);
  case COMMENT_PARAMNAME:
    return decodeRecord(R, I->ParamName, Blob);
  case COMMENT_CLOSENAME:
    return decodeRecord(R, I->CloseName, Blob);
  case COMMENT_ATTRKEY:
    AttrKeys.push_back(internString(Blob));
    return llvm::Error::success();
  case COMMENT_ATTRVAL:
    AttrValues.push_back(internString(Blob));
    return llvm::Error::success();
  case COMMENT_ARG:
    Args.push_back(internString(Blob));
    return llvm::Error::success();
  case COMMENT_SELFCLOSING:
    return decodeRecord(R, I->SelfClosing, Blob);
  case COMMENT_EXPLICIT:
    return decodeRecord(R, I->Explicit, Blob);
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid field for CommentInfo");
  }
}

template <typename T, typename BlockBeginHandler, typename BlockEndHandler,
          typename RecordHandler>
llvm::Error
ClangDocBitcodeReader::parseBlock(unsigned ID, T I, BlockBeginHandler &&BBH,
                                  BlockEndHandler &&BEH, RecordHandler &&RH) {
  llvm::TimeTraceScope("Reducing infos", "readBlock");
  if (llvm::Error Err = Stream.EnterSubBlock(ID))
    return Err;

  while (true) {
    unsigned BlockOrCode = 0;
    llvm::Expected<Cursor> C = skipUntilRecordOrBlock(BlockOrCode);
    if (!C)
      return C.takeError();

    switch (*C) {
    case Cursor::BadBlock:
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "bad block found");
    case Cursor::BlockEnd:
      if (llvm::Error Err = BEH())
        return Err;
      return llvm::Error::success();
    case Cursor::BlockBegin: {
      llvm::Expected<bool> Handled = BBH(BlockOrCode);
      if (!Handled)
        return Handled.takeError();
      if (*Handled)
        continue;

      if (llvm::Error Err = readSubBlock(BlockOrCode, I)) {
        if (llvm::Error Skipped = Stream.SkipBlock())
          return joinErrors(std::move(Err), std::move(Skipped));
        return Err;
      }
      continue;
    }
    case Cursor::Record:
      break;
    }

    if (llvm::Error Err = RH(BlockOrCode))
      return Err;
  }
}

template <typename T, typename BlockBeginHandler, typename BlockEndHandler>
llvm::Error ClangDocBitcodeReader::parseBlock(unsigned ID, T I,
                                              BlockBeginHandler &&BBH,
                                              BlockEndHandler &&BEH) {
  return parseBlock(ID, I, std::forward<BlockBeginHandler>(BBH),
                    std::forward<BlockEndHandler>(BEH),
                    [&](unsigned Code) { return readRecord(Code, I); });
}

template <typename ChildType>
llvm::Expected<bool> ClangDocBitcodeReader::readSubBlockIfMatch(
    unsigned ID, unsigned TargetID, llvm::SmallVectorImpl<ChildType> &V) {
  if (ID != TargetID)
    return false;
  ChildType Val;
  if (auto Err = readBlock(ID, &Val))
    return std::move(Err);
  V.push_back(std::move(Val));
  return true;
}

template <typename T>
static llvm::Error addReference(T I, Reference &&R, FieldId F);

template <> llvm::Error addReference(VarInfo *I, Reference &&R, FieldId F);
template <> llvm::Error addReference(TypeInfo *I, Reference &&R, FieldId F);
template <>
llvm::Error addReference(FieldTypeInfo *I, Reference &&R, FieldId F);
template <>
llvm::Error addReference(MemberTypeInfo *I, Reference &&R, FieldId F);
template <> llvm::Error addReference(EnumInfo *I, Reference &&R, FieldId F);
template <> llvm::Error addReference(TypedefInfo *I, Reference &&R, FieldId F);
template <>
llvm::Error addReference(NamespaceInfo *I, Reference &&R, FieldId F);
template <> llvm::Error addReference(FunctionInfo *I, Reference &&R, FieldId F);
template <> llvm::Error addReference(RecordInfo *I, Reference &&R, FieldId F);
template <>
llvm::Error addReference(ConstraintInfo *I, Reference &&R, FieldId F);
template <>
llvm::Error addReference(FriendInfo *Friend, Reference &&R, FieldId F);

template <typename InfoT>
llvm::Expected<bool> ClangDocBitcodeReader::routeReferenceBlock(
    unsigned ID, llvm::SmallVectorImpl<Reference> &Namespaces, InfoT *I,
    std::initializer_list<ReferenceMap> Mappings) {
  if (ID != BI_REFERENCE_BLOCK_ID)
    return false;
  Reference R;
  if (auto Err = readBlock(ID, &R))
    return std::move(Err);

  for (const auto &Map : Mappings) {
    if (CurrentReferenceField == Map.Field) {
      Map.Vec->push_back(std::move(R));
      return true;
    }
  }

  if (CurrentReferenceField == FieldId::F_namespace) {
    Namespaces.push_back(std::move(R));
    return true;
  }

  if (auto Err = addReference(I, std::move(R), CurrentReferenceField))
    return std::move(Err);

  return true;
}

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, CommentInfo *I);
template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, FunctionInfo *I);
template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, EnumInfo *I);
template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, BaseRecordInfo *I);
template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, RecordInfo *I);
template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, TemplateInfo *I);
template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID,
                                             TemplateSpecializationInfo *I);
template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, VarInfo *I);
template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, TypedefInfo *I);
template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, NamespaceInfo *I);
template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, FriendInfo *I);

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, CommentInfo *I) {
  llvm::SmallVector<CommentInfo> LocalChildren;
  llvm::SmallVector<StringRef> AttrKeys;
  llvm::SmallVector<StringRef> AttrValues;
  llvm::SmallVector<StringRef> Args;

  return parseBlock(
      ID, I,
      [&](unsigned BlockOrCode) -> llvm::Expected<bool> {
        if (BlockOrCode == BI_COMMENT_BLOCK_ID) {
          CommentInfo Child;
          if (llvm::Error Err = readBlock(BlockOrCode, &Child))
            return std::move(Err);
          LocalChildren.push_back(std::move(Child));
          return true;
        }
        return false;
      },
      [&]() -> llvm::Error {
        if (!LocalChildren.empty())
          I->Children =
              allocateArray<CommentInfo>(LocalChildren, getTransientArena());
        if (!AttrKeys.empty())
          I->AttrKeys = allocateArray(AttrKeys, getTransientArena());
        if (!AttrValues.empty())
          I->AttrValues = allocateArray(AttrValues, getTransientArena());
        if (!Args.empty())
          I->Args = allocateArray(Args, getTransientArena());

        return llvm::Error::success();
      },
      [&](unsigned BlockOrCode) -> llvm::Error {
        Record R;
        llvm::StringRef Blob;
        llvm::Expected<unsigned> MaybeRecID =
            Stream.readRecord(BlockOrCode, R, &Blob);
        if (!MaybeRecID)
          return MaybeRecID.takeError();
        return parseRecord(R, MaybeRecID.get(), Blob, I, AttrKeys, AttrValues,
                           Args);
      });
}

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, FunctionInfo *I) {
  llvm::SmallVector<FieldTypeInfo, 4> LocalParams;
  llvm::SmallVector<Reference> LocalNamespaces;

  return parseBlock(
      ID, I,
      [&](unsigned BlockOrCode) -> llvm::Expected<bool> {
        auto B = readSubBlockIfMatch(BlockOrCode, BI_FIELD_TYPE_BLOCK_ID,
                                     LocalParams);
        if (!B)
          return B.takeError();
        if (*B)
          return true;
        return routeReferenceBlock(BlockOrCode, LocalNamespaces, I);
      },
      [&]() -> llvm::Error {
        I->Params = allocateArray(LocalParams, getTransientArena());
        if (!LocalNamespaces.empty())
          I->Namespace = allocateArray(LocalNamespaces, getTransientArena());
        return llvm::Error::success();
      });
}

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, EnumInfo *I) {
  llvm::SmallVector<EnumValueInfo, 4> LocalMembers;
  llvm::SmallVector<Reference> LocalNamespaces;

  return parseBlock(
      ID, I,
      [&](unsigned BlockOrCode) -> llvm::Expected<bool> {
        auto B = readSubBlockIfMatch(BlockOrCode, BI_ENUM_VALUE_BLOCK_ID,
                                     LocalMembers);
        if (!B)
          return B.takeError();
        if (*B)
          return true;
        return routeReferenceBlock(BlockOrCode, LocalNamespaces, I);
      },
      [&]() -> llvm::Error {
        I->Members = allocateArray(LocalMembers, getTransientArena());
        if (!LocalNamespaces.empty())
          I->Namespace = allocateArray(LocalNamespaces, getTransientArena());
        return llvm::Error::success();
      });
}

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, BaseRecordInfo *I) {
  // BaseRecordInfo and FriendInfo are over 256 bytes and require a size.
  llvm::SmallVector<BaseRecordInfo, 4> LocalBases;
  llvm::SmallVector<FriendInfo, 4> LocalFriends;
  llvm::SmallVector<MemberTypeInfo> LocalMembers;
  llvm::SmallVector<Reference> LocalParents;
  llvm::SmallVector<Reference> LocalVirtualParents;

  return parseBlock(
      ID, I,
      [&](unsigned BlockOrCode) -> llvm::Expected<bool> {
        auto B = readSubBlockIfMatch(BlockOrCode, BI_MEMBER_TYPE_BLOCK_ID,
                                     LocalMembers);
        if (!B)
          return B.takeError();
        if (*B)
          return true;

        B = readSubBlockIfMatch(BlockOrCode, BI_BASE_RECORD_BLOCK_ID,
                                LocalBases);
        if (!B)
          return B.takeError();
        if (*B)
          return true;

        B = readSubBlockIfMatch(BlockOrCode, BI_FRIEND_BLOCK_ID, LocalFriends);
        if (!B)
          return B.takeError();
        if (*B)
          return true;

        llvm::SmallVector<Reference> Dummy;
        return routeReferenceBlock(
            BlockOrCode, Dummy, I,
            {{FieldId::F_parent, &LocalParents},
             {FieldId::F_vparent, &LocalVirtualParents}});
      },
      [&]() -> llvm::Error {
        if (!LocalMembers.empty())
          I->Members = allocateArray(LocalMembers, getTransientArena());
        if (!LocalParents.empty())
          I->Parents = allocateArray(LocalParents, getTransientArena());
        if (!LocalVirtualParents.empty())
          I->VirtualParents =
              allocateArray(LocalVirtualParents, getTransientArena());
        I->Bases = allocateArray(LocalBases, getTransientArena());
        I->Friends = allocateArray(LocalFriends, getTransientArena());
        return llvm::Error::success();
      });
}

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, RecordInfo *I) {
  llvm::SmallVector<BaseRecordInfo, 4> LocalBases;
  llvm::SmallVector<FriendInfo, 4> LocalFriends;
  llvm::SmallVector<MemberTypeInfo> LocalMembers;
  llvm::SmallVector<Reference> LocalParents;
  llvm::SmallVector<Reference> LocalVirtualParents;
  llvm::SmallVector<Reference> LocalNamespaces;

  return parseBlock(
      ID, I,
      [&](unsigned BlockOrCode) -> llvm::Expected<bool> {
        auto B = readSubBlockIfMatch(BlockOrCode, BI_MEMBER_TYPE_BLOCK_ID,
                                     LocalMembers);
        if (!B)
          return B.takeError();
        if (*B)
          return true;

        B = readSubBlockIfMatch(BlockOrCode, BI_BASE_RECORD_BLOCK_ID,
                                LocalBases);
        if (!B)
          return B.takeError();
        if (*B)
          return true;

        B = readSubBlockIfMatch(BlockOrCode, BI_FRIEND_BLOCK_ID, LocalFriends);
        if (!B)
          return B.takeError();
        if (*B)
          return true;

        return routeReferenceBlock(
            BlockOrCode, LocalNamespaces, I,
            {{FieldId::F_parent, &LocalParents},
             {FieldId::F_vparent, &LocalVirtualParents}});
      },
      [&]() -> llvm::Error {
        if (!LocalMembers.empty())
          I->Members = allocateArray(LocalMembers, getTransientArena());
        if (!LocalParents.empty())
          I->Parents = allocateArray(LocalParents, getTransientArena());
        if (!LocalVirtualParents.empty())
          I->VirtualParents =
              allocateArray(LocalVirtualParents, getTransientArena());
        if (!LocalNamespaces.empty())
          I->Namespace = allocateArray(LocalNamespaces, getTransientArena());
        I->Bases = allocateArray(LocalBases, getTransientArena());
        I->Friends = allocateArray(LocalFriends, getTransientArena());
        return llvm::Error::success();
      });
}

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, TemplateInfo *I) {
  llvm::SmallVector<TemplateParamInfo> LocalParams;
  llvm::SmallVector<ConstraintInfo> LocalConstraints;

  return parseBlock(
      ID, I,
      [&](unsigned BlockOrCode) -> llvm::Expected<bool> {
        auto B = readSubBlockIfMatch(BlockOrCode, BI_TEMPLATE_PARAM_BLOCK_ID,
                                     LocalParams);
        if (!B)
          return B.takeError();
        if (*B)
          return true;

        B = readSubBlockIfMatch(BlockOrCode, BI_CONSTRAINT_BLOCK_ID,
                                LocalConstraints);
        if (!B)
          return B.takeError();
        if (*B)
          return true;

        return false;
      },
      [&]() -> llvm::Error {
        I->Params = allocateArray(LocalParams, getTransientArena());
        I->Constraints = allocateArray(LocalConstraints, getTransientArena());
        return llvm::Error::success();
      },
      [&](unsigned BlockOrCode) -> llvm::Error {
        return readRecord(BlockOrCode, I);
      });
}

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID,
                                             TemplateSpecializationInfo *I) {
  llvm::SmallVector<TemplateParamInfo> LocalParams;

  return parseBlock(
      ID, I,
      [&](unsigned BlockOrCode) -> llvm::Expected<bool> {
        auto B = readSubBlockIfMatch(BlockOrCode, BI_TEMPLATE_PARAM_BLOCK_ID,
                                     LocalParams);
        if (!B)
          return B.takeError();
        if (*B)
          return true;

        return false;
      },
      [&]() -> llvm::Error {
        I->Params = allocateArray(LocalParams, getTransientArena());
        return llvm::Error::success();
      },
      [&](unsigned BlockOrCode) -> llvm::Error {
        return readRecord(BlockOrCode, I);
      });
}

static llvm::Error parseRecord(const Record &R, unsigned ID,
                               llvm::StringRef Blob, Reference *I, FieldId &F) {
  switch (ID) {
  case REFERENCE_USR:
    return decodeRecord(R, I->USR, Blob);
  case REFERENCE_NAME:
    return decodeRecord(R, I->Name, Blob);
  case REFERENCE_QUAL_NAME:
    return decodeRecord(R, I->QualName, Blob);
  case REFERENCE_TYPE:
    return decodeRecord(R, I->RefType, Blob);
  case REFERENCE_PATH:
    return decodeRecord(R, I->Path, Blob);
  case REFERENCE_FIELD:
    return decodeRecord(R, F, Blob);
  case REFERENCE_FILE:
    return decodeRecord(R, I->DocumentationFileName, Blob);
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid field for Reference");
  }
}

static llvm::Error parseRecord(const Record &R, unsigned ID,
                               llvm::StringRef Blob, TemplateInfo *I) {
  // Currently there are no child records of TemplateInfo (only child blocks).
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "invalid field for TemplateInfo");
}

template <typename, typename = void>
struct has_description : std::false_type {};
template <typename T>
struct has_description<T, std::void_t<decltype(std::declval<T>().Description)>>
    : std::true_type {};

template <typename T> static llvm::Expected<CommentInfo *> getCommentInfo(T I) {
  if constexpr (std::is_pointer_v<T>) {
    using Pointee = std::remove_pointer_t<T>;
    if constexpr (has_description<Pointee>::value) {
      auto *NewComment = allocateListNodeTransient<CommentInfo>();
      I->Description.push_back(*NewComment);
      return NewComment->Ptr;
    }
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "invalid type cannot contain CommentInfo");
}

// When readSubBlock encounters a TypeInfo sub-block, it calls addTypeInfo on
// the parent block to set it. The template specializations define what to do
// for each supported parent block.
template <typename T, typename TTypeInfo>
static llvm::Error addTypeInfo(T I, TTypeInfo &&TI) {
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "invalid type cannot contain TypeInfo");
}

template <> llvm::Error addTypeInfo(FunctionInfo *I, TypeInfo &&T) {
  I->ReturnType = std::move(T);
  return llvm::Error::success();
}

template <> llvm::Error addTypeInfo(FriendInfo *I, TypeInfo &&T) {
  I->ReturnType.emplace(std::move(T));
  return llvm::Error::success();
}

template <> llvm::Error addTypeInfo(EnumInfo *I, TypeInfo &&T) {
  I->BaseType = std::move(T);
  return llvm::Error::success();
}

template <> llvm::Error addTypeInfo(TypedefInfo *I, TypeInfo &&T) {
  I->Underlying = std::move(T);
  return llvm::Error::success();
}

template <> llvm::Error addTypeInfo(VarInfo *I, TypeInfo &&T) {
  I->Type = std::move(T);
  return llvm::Error::success();
}

template <typename T>
static llvm::Error addReference(T I, Reference &&R, FieldId F) {
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "invalid type cannot contain Reference");
}

template <> llvm::Error addReference(VarInfo *I, Reference &&R, FieldId F) {
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "VarInfo cannot contain this Reference");
}

template <> llvm::Error addReference(TypeInfo *I, Reference &&R, FieldId F) {
  switch (F) {
  case FieldId::F_type:
    I->Type = std::move(R);
    return llvm::Error::success();
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid type cannot contain Reference");
  }
}

template <>
llvm::Error addReference(FieldTypeInfo *I, Reference &&R, FieldId F) {
  switch (F) {
  case FieldId::F_type:
    I->Type = std::move(R);
    return llvm::Error::success();
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid type cannot contain Reference");
  }
}

template <>
llvm::Error addReference(MemberTypeInfo *I, Reference &&R, FieldId F) {
  switch (F) {
  case FieldId::F_type:
    I->Type = std::move(R);
    return llvm::Error::success();
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid type cannot contain Reference");
  }
}

template <> llvm::Error addReference(EnumInfo *I, Reference &&R, FieldId F) {
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "invalid type cannot contain Reference");
}

template <> llvm::Error addReference(TypedefInfo *I, Reference &&R, FieldId F) {
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "invalid type cannot contain Reference");
}

template <>
llvm::Error addReference(NamespaceInfo *I, Reference &&R, FieldId F) {
  switch (F) {
  case FieldId::F_child_namespace:
    I->Children.Namespaces.push_back(
        *allocateListNodeTransient<Reference>(std::move(R)));
    return llvm::Error::success();
  case FieldId::F_child_record:
    I->Children.Records.push_back(
        *allocateListNodeTransient<Reference>(std::move(R)));
    return llvm::Error::success();
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid type cannot contain Reference");
  }
}

template <>
llvm::Error addReference(FunctionInfo *I, Reference &&R, FieldId F) {
  switch (F) {
  case FieldId::F_parent:
    I->Parent = std::move(R);
    return llvm::Error::success();
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid type cannot contain Reference");
  }
}

template <> llvm::Error addReference(RecordInfo *I, Reference &&R, FieldId F) {
  switch (F) {
  case FieldId::F_child_record:
    I->Children.Records.push_back(
        *allocateListNodeTransient<Reference>(std::move(R)));
    return llvm::Error::success();
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid type cannot contain Reference");
  }
}

template <>
llvm::Error addReference(ConstraintInfo *I, Reference &&R, FieldId F) {
  if (F == FieldId::F_concept) {
    I->ConceptRef = std::move(R);
    return llvm::Error::success();
  }
  return llvm::createStringError(
      llvm::inconvertibleErrorCode(),
      "ConstraintInfo cannot contain this Reference");
}

template <>
llvm::Error addReference(FriendInfo *Friend, Reference &&R, FieldId F) {
  if (F == FieldId::F_friend) {
    Friend->Ref = std::move(R);
    return llvm::Error::success();
  }
  return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                 "Friend cannot contain this Reference");
}

static auto &getList(ScopeChildren &C, FunctionInfo *) { return C.Functions; }
static auto &getList(ScopeChildren &C, EnumInfo *) { return C.Enums; }
static auto &getList(ScopeChildren &C, TypedefInfo *) { return C.Typedefs; }
static auto &getList(ScopeChildren &C, ConceptInfo *) { return C.Concepts; }
static auto &getList(ScopeChildren &C, VarInfo *) { return C.Variables; }

template <typename T, typename = void> struct has_children : std::false_type {};
template <typename T>
struct has_children<T, std::void_t<decltype(std::declval<T>().Children)>>
    : std::is_same<decltype(std::declval<T>().Children), ScopeChildren> {};

template <typename TargetChild, typename = void>
struct is_valid_child : std::false_type {};
template <typename TargetChild>
struct is_valid_child<
    TargetChild, std::void_t<decltype(getList(std::declval<ScopeChildren &>(),
                                              std::declval<TargetChild *>()))>>
    : std::true_type {};

template <typename Target, typename Child>
static void addChild(Target I, Child &&R) {
  if constexpr (std::is_pointer_v<Target>) {
    using Pointee = std::remove_pointer_t<Target>;
    if constexpr (has_children<Pointee>::value) {
      using BareChild = std::remove_cv_t<std::remove_reference_t<Child>>;
      if constexpr (is_valid_child<BareChild>::value) {
        auto *Node =
            allocateListNodeTransient<BareChild>(std::forward<Child>(R));
        getList(I->Children, Node->Ptr).push_back(*Node);
        return;
      }
    }
  }
  ExitOnErr(llvm::createStringError(llvm::inconvertibleErrorCode(),
                                    "invalid child type for info"));
}

template <typename Target, typename Child>
static void addChildPtr(Target I, Child *Node) {
  if constexpr (std::is_pointer_v<Target>) {
    using Pointee = std::remove_pointer_t<Target>;
    if constexpr (has_children<Pointee>::value &&
                  is_valid_child<Child>::value) {
      getList(I->Children, Node).push_back(*allocateListNodeTransient(Node));
      return;
    }
  }
  ExitOnErr(llvm::createStringError(llvm::inconvertibleErrorCode(),
                                    "invalid child type for info"));
}

// TemplateParam children. These go into either a TemplateInfo (for template
// parameters) or TemplateSpecializationInfo (for the specialization's
// parameters).
template <typename T> static void addTemplateParam(T I, TemplateParamInfo &&P) {
  ExitOnErr(
      llvm::createStringError(llvm::inconvertibleErrorCode(),
                              "invalid container for template parameter"));
}

// Template info. These apply to either records or functions.
template <typename T> static void addTemplate(T I, TemplateInfo &&P) {
  ExitOnErr(llvm::createStringError(llvm::inconvertibleErrorCode(),
                                    "invalid container for template info"));
}
template <> void addTemplate(RecordInfo *I, TemplateInfo &&P) {
  I->Template.emplace(std::move(P));
}
template <> void addTemplate(FunctionInfo *I, TemplateInfo &&P) {
  I->Template.emplace(std::move(P));
}
template <> void addTemplate(ConceptInfo *I, TemplateInfo &&P) {
  I->Template = std::move(P);
}
template <> void addTemplate(FriendInfo *I, TemplateInfo &&P) {
  I->Template.emplace(std::move(P));
}
template <> void addTemplate(TypedefInfo *I, TemplateInfo &&P) {
  I->Template.emplace(std::move(P));
}

// Template specializations go only into template records.
template <typename T>
static void addTemplateSpecialization(T I, TemplateSpecializationInfo &&TSI) {
  ExitOnErr(llvm::createStringError(
      llvm::inconvertibleErrorCode(),
      "invalid container for template specialization info"));
}
template <>
void addTemplateSpecialization(TemplateInfo *I,
                               TemplateSpecializationInfo &&TSI) {
  I->Specialization.emplace(std::move(TSI));
}

template <typename T> static void addConstraint(T I, ConstraintInfo &&C) {
  ExitOnErr(llvm::createStringError(llvm::inconvertibleErrorCode(),
                                    "invalid container for constraint info"));
}

// Read records from bitcode into a given info.
template <typename T>
llvm::Error ClangDocBitcodeReader::readRecord(unsigned ID, T I) {
  Record R;
  llvm::StringRef Blob;
  llvm::Expected<unsigned> MaybeRecID = Stream.readRecord(ID, R, &Blob);
  if (!MaybeRecID)
    return MaybeRecID.takeError();
  return parseRecord(R, MaybeRecID.get(), Blob, I);
}

template <>
llvm::Error ClangDocBitcodeReader::readRecord(unsigned ID, Reference *I) {
  llvm::TimeTraceScope("Reducing infos", "readRecord");
  Record R;
  llvm::StringRef Blob;
  llvm::Expected<unsigned> MaybeRecID = Stream.readRecord(ID, R, &Blob);
  if (!MaybeRecID)
    return MaybeRecID.takeError();
  return parseRecord(R, MaybeRecID.get(), Blob, I, CurrentReferenceField);
}

// Read a block of records into a single info.

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, VarInfo *I) {
  return readBlockWithNamespace(ID, I);
}

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, TypedefInfo *I) {
  return readBlockWithNamespace(ID, I);
}

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, NamespaceInfo *I) {
  return readBlockWithNamespace(ID, I);
}

template <typename T>
llvm::Error ClangDocBitcodeReader::readBlockWithNamespace(unsigned ID, T I) {
  llvm::SmallVector<Reference> LocalNamespaces;
  return parseBlock(
      ID, I,
      [&](unsigned BlockOrCode) -> llvm::Expected<bool> {
        return routeReferenceBlock(BlockOrCode, LocalNamespaces, I);
      },
      [&]() -> llvm::Error {
        if (!LocalNamespaces.empty())
          I->Namespace = allocateArray(LocalNamespaces, getTransientArena());
        return llvm::Error::success();
      });
}

template <typename T>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, T I) {
  return parseBlock(
      ID, I, [](unsigned BlockOrCode) -> llvm::Expected<bool> { return false; },
      []() -> llvm::Error { return llvm::Error::success(); },
      [&](unsigned BlockOrCode) -> llvm::Error {
        return readRecord(BlockOrCode, I);
      });
}

template <>
llvm::Error ClangDocBitcodeReader::readBlock(unsigned ID, FriendInfo *I) {
  llvm::SmallVector<FieldTypeInfo, 4> LocalParams;

  return parseBlock(
      ID, I,
      [&](unsigned BlockOrCode) -> llvm::Expected<bool> {
        auto B = readSubBlockIfMatch(BlockOrCode, BI_FIELD_TYPE_BLOCK_ID,
                                     LocalParams);
        if (!B)
          return B.takeError();
        if (*B)
          return true;

        return false;
      },
      [&]() -> llvm::Error {
        if (!LocalParams.empty())
          I->Params =
              allocateArray<FieldTypeInfo>(LocalParams, getTransientArena());
        return llvm::Error::success();
      },
      [&](unsigned BlockOrCode) -> llvm::Error {
        return readRecord(BlockOrCode, I);
      });
}

template <typename InfoType, typename T, typename Callback>
llvm::Error ClangDocBitcodeReader::handleSubBlock(unsigned ID, T Parent,
                                                  Callback Function) {
  InfoType Info;
  if (auto Err = readBlock(ID, &Info))
    return Err;
  if constexpr (std::is_void_v<
                    std::invoke_result_t<Callback, T, InfoType &&>>) {
    Function(Parent, std::move(Info));
    return llvm::Error::success();
  } else {
    return Function(Parent, std::move(Info));
  }
}

template <typename InfoType, typename T>
llvm::Error ClangDocBitcodeReader::handleSubBlock(unsigned ID, T Parent) {
  InfoType *Info = allocateTransient<InfoType>();
  if (auto Err = readBlock(ID, Info))
    return Err;
  addChildPtr(Parent, Info);
  return llvm::Error::success();
}

template <typename T>
llvm::Error ClangDocBitcodeReader::readSubBlock(unsigned ID, T I) {
  llvm::TimeTraceScope("Reducing infos", "readSubBlock");

  static auto CreateAddFunc = [](auto AddFunc) {
    return [AddFunc](auto Parent, auto Child) {
      return AddFunc(Parent, std::move(Child));
    };
  };

  switch (ID) {
  // Blocks can only have certain types of sub blocks.
  case BI_COMMENT_BLOCK_ID: {
    auto Comment = getCommentInfo(I);
    if (!Comment)
      return Comment.takeError();
    if (auto Err = readBlock(ID, Comment.get()))
      return Err;
    return llvm::Error::success();
  }
  case BI_TYPE_BLOCK_ID: {
    return handleSubBlock<TypeInfo>(ID, I,
                                    CreateAddFunc(addTypeInfo<T, TypeInfo>));
  }
  case BI_FIELD_TYPE_BLOCK_ID: {
    return handleSubBlock<FieldTypeInfo>(
        ID, I, CreateAddFunc(addTypeInfo<T, FieldTypeInfo>));
  }
  case BI_MEMBER_TYPE_BLOCK_ID: {
    return handleSubBlock<MemberTypeInfo>(
        ID, I, CreateAddFunc(addTypeInfo<T, MemberTypeInfo>));
  }
  case BI_REFERENCE_BLOCK_ID: {
    Reference R;
    if (auto Err = readBlock(ID, &R))
      return Err;
    if (auto Err = addReference(I, std::move(R), CurrentReferenceField))
      return Err;
    return llvm::Error::success();
  }
  case BI_FUNCTION_BLOCK_ID: {
    return handleSubBlock<FunctionInfo>(ID, I);
  }
  case BI_BASE_RECORD_BLOCK_ID: {
    return handleSubBlock<BaseRecordInfo>(
        ID, I, CreateAddFunc(addChild<T, BaseRecordInfo>));
  }
  case BI_ENUM_BLOCK_ID: {
    return handleSubBlock<EnumInfo>(ID, I);
  }
  case BI_ENUM_VALUE_BLOCK_ID: {
    return handleSubBlock<EnumValueInfo>(
        ID, I, CreateAddFunc(addChild<T, EnumValueInfo>));
  }
  case BI_TEMPLATE_BLOCK_ID: {
    return handleSubBlock<TemplateInfo>(ID, I, CreateAddFunc(addTemplate<T>));
  }
  case BI_TEMPLATE_SPECIALIZATION_BLOCK_ID: {
    return handleSubBlock<TemplateSpecializationInfo>(
        ID, I, CreateAddFunc(addTemplateSpecialization<T>));
  }
  case BI_TEMPLATE_PARAM_BLOCK_ID: {
    return handleSubBlock<TemplateParamInfo>(
        ID, I, CreateAddFunc(addTemplateParam<T>));
  }
  case BI_TYPEDEF_BLOCK_ID: {
    return handleSubBlock<TypedefInfo>(ID, I);
  }
  case BI_CONSTRAINT_BLOCK_ID: {
    return handleSubBlock<ConstraintInfo>(ID, I,
                                          CreateAddFunc(addConstraint<T>));
  }
  case BI_CONCEPT_BLOCK_ID: {
    return handleSubBlock<ConceptInfo>(ID, I);
  }
  case BI_VAR_BLOCK_ID: {
    return handleSubBlock<VarInfo>(ID, I);
  }
  case BI_FRIEND_BLOCK_ID: {
    return handleSubBlock<FriendInfo>(ID, I,
                                      CreateAddFunc(addChild<T, FriendInfo>));
  }
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "invalid subblock type");
  }
}

llvm::Expected<ClangDocBitcodeReader::Cursor>
ClangDocBitcodeReader::skipUntilRecordOrBlock(unsigned &BlockOrRecordID) {
  llvm::TimeTraceScope("Reducing infos", "skipUntilRecordOrBlock");
  BlockOrRecordID = 0;

  while (!Stream.AtEndOfStream()) {
    Expected<unsigned> Code = Stream.ReadCode();
    if (!Code)
      return Code.takeError();

    if (*Code >= static_cast<unsigned>(llvm::bitc::FIRST_APPLICATION_ABBREV)) {
      BlockOrRecordID = *Code;
      return Cursor::Record;
    }
    switch (static_cast<llvm::bitc::FixedAbbrevIDs>(*Code)) {
    case llvm::bitc::ENTER_SUBBLOCK:
      if (Expected<unsigned> MaybeID = Stream.ReadSubBlockID())
        BlockOrRecordID = MaybeID.get();
      else
        return MaybeID.takeError();
      return Cursor::BlockBegin;
    case llvm::bitc::END_BLOCK:
      if (Stream.ReadBlockEnd())
        return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                       "error at end of block");
      return Cursor::BlockEnd;
    case llvm::bitc::DEFINE_ABBREV:
      if (llvm::Error Err = Stream.ReadAbbrevRecord())
        return std::move(Err);
      continue;
    case llvm::bitc::UNABBREV_RECORD:
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "found unabbreviated record");
    case llvm::bitc::FIRST_APPLICATION_ABBREV:
      llvm_unreachable("Unexpected abbrev id.");
    }
  }
  llvm_unreachable("Premature stream end.");
}

llvm::Error ClangDocBitcodeReader::validateStream() {
  if (Stream.AtEndOfStream())
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "premature end of stream");

  // Sniff for the signature.
  for (int Idx = 0; Idx != 4; ++Idx) {
    Expected<llvm::SimpleBitstreamCursor::word_t> MaybeRead = Stream.Read(8);
    if (!MaybeRead)
      return MaybeRead.takeError();
    if (MaybeRead.get() != BitCodeConstants::Signature[Idx])
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "invalid bitcode signature");
  }
  return llvm::Error::success();
}

llvm::Error ClangDocBitcodeReader::readBlockInfoBlock() {
  llvm::TimeTraceScope("Reducing infos", "readBlockInfoBlock");
  Expected<std::optional<llvm::BitstreamBlockInfo>> MaybeBlockInfo =
      Stream.ReadBlockInfoBlock();
  if (!MaybeBlockInfo)
    return MaybeBlockInfo.takeError();
  BlockInfo = MaybeBlockInfo.get();
  if (!BlockInfo)
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "unable to parse BlockInfoBlock");
  Stream.setBlockInfo(&*BlockInfo);
  return llvm::Error::success();
}

template <typename T>
llvm::Expected<Info *> ClangDocBitcodeReader::createInfo(unsigned ID) {
  llvm::TimeTraceScope("Reducing infos", "createInfo");
  auto *I = doc::allocateTransient<T>();
  if (auto Err = readBlock(ID, I))
    return std::move(Err);
  return I;
}

llvm::Expected<Info *> ClangDocBitcodeReader::readBlockToInfo(unsigned ID) {
  llvm::TimeTraceScope("Reducing infos", "readBlockToInfo");
  switch (ID) {
  case BI_NAMESPACE_BLOCK_ID:
    return createInfo<NamespaceInfo>(ID);
  case BI_RECORD_BLOCK_ID:
    return createInfo<RecordInfo>(ID);
  case BI_ENUM_BLOCK_ID:
    return createInfo<EnumInfo>(ID);
  case BI_TYPEDEF_BLOCK_ID:
    return createInfo<TypedefInfo>(ID);
  case BI_CONCEPT_BLOCK_ID:
    return createInfo<ConceptInfo>(ID);
  case BI_FUNCTION_BLOCK_ID:
    return createInfo<FunctionInfo>(ID);
  case BI_VAR_BLOCK_ID:
    return createInfo<VarInfo>(ID);
  case BI_FRIEND_BLOCK_ID:
    return createInfo<FriendInfo>(ID);
  default:
    return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                   "cannot create info");
  }
}

// Entry point
llvm::Expected<std::vector<Info *>> ClangDocBitcodeReader::readBitcode() {
  std::vector<Info *> Infos;
  if (auto Err = validateStream())
    return std::move(Err);

  // Read the top level blocks.
  while (!Stream.AtEndOfStream()) {
    Expected<unsigned> MaybeCode = Stream.ReadCode();
    if (!MaybeCode)
      return MaybeCode.takeError();
    if (MaybeCode.get() != llvm::bitc::ENTER_SUBBLOCK)
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "no blocks in input");
    Expected<unsigned> MaybeID = Stream.ReadSubBlockID();
    if (!MaybeID)
      return MaybeID.takeError();
    unsigned ID = MaybeID.get();
    switch (ID) {
    // NamedType and Comment blocks should not appear at the top level
    case BI_TYPE_BLOCK_ID:
    case BI_FIELD_TYPE_BLOCK_ID:
    case BI_MEMBER_TYPE_BLOCK_ID:
    case BI_COMMENT_BLOCK_ID:
    case BI_REFERENCE_BLOCK_ID:
      return llvm::createStringError(llvm::inconvertibleErrorCode(),
                                     "invalid top level block");
    case BI_NAMESPACE_BLOCK_ID:
    case BI_RECORD_BLOCK_ID:
    case BI_ENUM_BLOCK_ID:
    case BI_TYPEDEF_BLOCK_ID:
    case BI_CONCEPT_BLOCK_ID:
    case BI_VAR_BLOCK_ID:
    case BI_FRIEND_BLOCK_ID:
    case BI_FUNCTION_BLOCK_ID: {
      auto InfoOrErr = readBlockToInfo(ID);
      if (!InfoOrErr)
        return InfoOrErr.takeError();
      Infos.emplace_back(std::move(InfoOrErr.get()));
      continue;
    }
    case BI_VERSION_BLOCK_ID:
      if (auto Err = readBlock(ID, VersionNumber))
        return std::move(Err);
      continue;
    case llvm::bitc::BLOCKINFO_BLOCK_ID:
      if (auto Err = readBlockInfoBlock())
        return std::move(Err);
      continue;
    default:
      if (llvm::Error Err = Stream.SkipBlock())
        return std::move(Err);
      continue;
    }
  }
  return std::move(Infos);
}

} // namespace doc
} // namespace clang
