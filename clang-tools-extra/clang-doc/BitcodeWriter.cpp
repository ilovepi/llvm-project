//===--  BitcodeWriter.cpp - ClangDoc Bitcode Writer ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "BitcodeWriter.h"
#include "llvm/ADT/IndexedMap.h"
#include <initializer_list>
#include <iterator>

using namespace clang;
using namespace clang::doc;
using namespace llvm;

namespace {

// Empty SymbolID for comparison, so we don't have to construct one every time.
static const SymbolID EmptySID = SymbolID();

// Since id enums are not zero-indexed, we need to transform the given id into
// its associated index.
struct BlockIdToIndexFunctor {
  using argument_type = unsigned;
  unsigned operator()(unsigned ID) const { return ID - BI_FIRST; }
};

struct RecordIdToIndexFunctor {
  using argument_type = unsigned;
  unsigned operator()(unsigned ID) const { return ID - RI_FIRST; }
};

using AbbrevDsc = void (*)(std::shared_ptr<BitCodeAbbrev> &Abbrev);
} // namespace

static void generateAbbrev(std::shared_ptr<BitCodeAbbrev> &Abbrev,
                           const std::initializer_list<BitCodeAbbrevOp> Ops) {
  for (const auto &Op : Ops)
    Abbrev->Add(Op);
}

static void genBoolAbbrev(std::shared_ptr<BitCodeAbbrev> &Abbrev) {
  generateAbbrev(
      Abbrev,
      {// 0. Boolean
       BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, BitCodeConstants::BoolSize)});
}

static void genIntAbbrev(std::shared_ptr<BitCodeAbbrev> &Abbrev) {
  generateAbbrev(
      Abbrev,
      {// 0. Fixed-size integer
       BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, BitCodeConstants::IntSize)});
}

static void genSymbolIdAbbrev(std::shared_ptr<BitCodeAbbrev> &Abbrev) {
  generateAbbrev(
      Abbrev,
      {// 0. Fixed-size integer (length of the sha1'd USR)
       BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, BitCodeConstants::USRLengthSize),
       // 1. Fixed-size array of Char6 (USR)
       BitCodeAbbrevOp(BitCodeAbbrevOp::Array),
       BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                       BitCodeConstants::USRBitLengthSize)});
}

static void genStringAbbrev(std::shared_ptr<BitCodeAbbrev> &Abbrev) {
  generateAbbrev(Abbrev,
                 {// 0. Fixed-size integer (length of the following string)
                  BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                                  BitCodeConstants::StringLengthSize),
                  // 1. The string blob
                  BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)});
}

// Assumes that the file will not have more than 65535 lines.
static void genLocationAbbrev(std::shared_ptr<BitCodeAbbrev> &Abbrev) {
  generateAbbrev(
      Abbrev,
      {// 0. Fixed-size integer (line number)
       BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                       BitCodeConstants::LineNumberSize),
       // 1. Fixed-size integer (start line number)
       BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                       BitCodeConstants::LineNumberSize),
       // 2. Boolean (IsFileInRootDir)
       BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed, BitCodeConstants::BoolSize),
       // 3. Fixed-size integer (length of the following string (filename))
       BitCodeAbbrevOp(BitCodeAbbrevOp::Fixed,
                       BitCodeConstants::StringLengthSize),
       // 4. The string blob
       BitCodeAbbrevOp(BitCodeAbbrevOp::Blob)});
}

namespace {
struct RecordIdDsc {
  RecordIdDsc() = default;
  constexpr RecordIdDsc(StringRef Name, AbbrevDsc Abbrev)
      : Name(Name), Abbrev(Abbrev) {}

  // Is this 'description' valid?
  explicit operator bool() const {
    return Abbrev != nullptr && Name.data() != nullptr && !Name.empty();
  }

  StringRef Name;
  AbbrevDsc Abbrev = nullptr;
};

static const IndexedMap<StringRef, BlockIdToIndexFunctor> BlockIdNameMap =
    []() {
      IndexedMap<StringRef, BlockIdToIndexFunctor> BlockIdNameMap;
      BlockIdNameMap.resize(BlockIdCount);

      static constexpr std::pair<BlockId, StringRef> IdToName[] = {
#define CLANG_DOC_BLOCK_NAMES
#include "ClangDocRecords.def"
      };
      static_assert(std::size(IdToName) == BlockIdCount,
                    "ClangDocRecords.def out of sync with the BlockId enum");
      ArrayRef<std::pair<BlockId, StringRef>> Inits(IdToName);
      for (const auto &Init : Inits)
        BlockIdNameMap[Init.first] = Init.second;
      assert(BlockIdNameMap.size() == BlockIdCount);
      return BlockIdNameMap;
    }();

static const IndexedMap<RecordIdDsc, RecordIdToIndexFunctor> RecordIdNameMap =
    []() {
      IndexedMap<RecordIdDsc, RecordIdToIndexFunctor> RecordIdNameMap;
      RecordIdNameMap.resize(RecordIdCount);

      static constexpr std::pair<RecordId, RecordIdDsc> IdToFunc[] = {
#define CLANG_DOC_RECORD_DESCS
#include "ClangDocRecords.def"
      };
      static_assert(std::size(IdToFunc) == RecordIdCount,
                    "ClangDocRecords.def out of sync with the RecordId enum");
      ArrayRef<std::pair<RecordId, RecordIdDsc>> Inits(IdToFunc);
      for (const auto &Init : Inits) {
        RecordIdNameMap[Init.first] = Init.second;
        assert((Init.second.Name.size() + 1) <= BitCodeConstants::RecordSize);
      }
      assert(RecordIdNameMap.size() == RecordIdCount);
      return RecordIdNameMap;
    }();

struct BlockToIdList {
  BlockId BID;
  std::initializer_list<RecordId> RIDs;
};

static const BlockToIdList RecordsByBlock[] = {
#define CLANG_DOC_RECORDS_BY_BLOCK
#include "ClangDocRecords.def"
};
static_assert(std::size(RecordsByBlock) == BlockIdCount,
              "ClangDocRecords.def out of sync with the BlockId enum");

} // namespace

namespace clang {
namespace doc {

// AbbreviationMap

void ClangDocBitcodeWriter::AbbreviationMap::add(RecordId RID,
                                                 unsigned AbbrevID) {
  assert(RecordIdNameMap[RID] && "Unknown RecordId.");
  assert(!Abbrevs.contains(RID) && "Abbreviation already added.");
  Abbrevs[RID] = AbbrevID;
}

unsigned ClangDocBitcodeWriter::AbbreviationMap::get(RecordId RID) const {
  assert(RecordIdNameMap[RID] && "Unknown RecordId.");
  assert(Abbrevs.contains(RID) && "Unknown abbreviation.");
  return Abbrevs.lookup(RID);
}

// Validation and Overview Blocks

/// Emits the magic number header to check that its the right format,
/// in this case, 'DOCS'.
void ClangDocBitcodeWriter::emitHeader() {
  for (char C : BitCodeConstants::Signature)
    Stream.Emit((unsigned)C, BitCodeConstants::SignatureBitSize);
}

void ClangDocBitcodeWriter::emitVersionBlock() {
  StreamSubBlockGuard Block(Stream, BI_VERSION_BLOCK_ID);
  emitRecord(VersionNumber, VERSION);
}

/// Emits a block ID and the block name to the BLOCKINFO block.
void ClangDocBitcodeWriter::emitBlockID(BlockId BID) {
  const auto &BlockIdName = BlockIdNameMap[BID];
  assert(BlockIdName.data() && BlockIdName.size() && "Unknown BlockId.");

  Record.clear();
  Record.push_back(BID);
  Stream.EmitRecord(bitc::BLOCKINFO_CODE_SETBID, Record);
  Stream.EmitRecord(bitc::BLOCKINFO_CODE_BLOCKNAME,
                    ArrayRef<unsigned char>(BlockIdName.bytes_begin(),
                                            BlockIdName.bytes_end()));
}

/// Emits a record name to the BLOCKINFO block.
void ClangDocBitcodeWriter::emitRecordID(RecordId ID) {
  assert(RecordIdNameMap[ID] && "Unknown RecordId.");
  prepRecordData(ID);
  Record.append(RecordIdNameMap[ID].Name.begin(),
                RecordIdNameMap[ID].Name.end());
  Stream.EmitRecord(bitc::BLOCKINFO_CODE_SETRECORDNAME, Record);
}

// Abbreviations

void ClangDocBitcodeWriter::emitAbbrev(RecordId ID, BlockId Block) {
  assert(RecordIdNameMap[ID] && "Unknown abbreviation.");
  auto Abbrev = std::make_shared<BitCodeAbbrev>();
  Abbrev->Add(BitCodeAbbrevOp(ID));
  RecordIdNameMap[ID].Abbrev(Abbrev);
  Abbrevs.add(ID, Stream.EmitBlockInfoAbbrev(Block, std::move(Abbrev)));
}

// Records

void ClangDocBitcodeWriter::emitRecord(const SymbolID &Sym, RecordId ID) {
  assert(RecordIdNameMap[ID] && "Unknown RecordId.");
  assert(RecordIdNameMap[ID].Abbrev == &genSymbolIdAbbrev &&
         "Abbrev type mismatch.");
  if (!prepRecordData(ID, Sym != EmptySID))
    return;
  assert(Sym.size() == 20);
  Record.push_back(Sym.size());
  Record.append(Sym.begin(), Sym.end());
  Stream.EmitRecordWithAbbrev(Abbrevs.get(ID), Record);
}

void ClangDocBitcodeWriter::emitRecord(StringRef Str, RecordId ID) {
  assert(RecordIdNameMap[ID] && "Unknown RecordId.");
  assert(RecordIdNameMap[ID].Abbrev == &genStringAbbrev &&
         "Abbrev type mismatch.");
  if (!prepRecordData(ID, !Str.empty()))
    return;
  assert(Str.size() < (1U << BitCodeConstants::StringLengthSize));
  Record.push_back(Str.size());
  Stream.EmitRecordWithBlob(Abbrevs.get(ID), Record, Str);
}

void ClangDocBitcodeWriter::emitRecord(const Location &Loc, RecordId ID) {
  assert(RecordIdNameMap[ID] && "Unknown RecordId.");
  assert(RecordIdNameMap[ID].Abbrev == &genLocationAbbrev &&
         "Abbrev type mismatch.");
  if (!prepRecordData(ID, true))
    return;
  // FIXME: Assert that the line number is of the appropriate size.
  Record.push_back(Loc.StartLineNumber);
  Record.push_back(Loc.EndLineNumber);
  assert(Loc.Filename.size() < (1U << BitCodeConstants::StringLengthSize));
  Record.push_back(Loc.IsFileInRootDir);
  Record.push_back(Loc.Filename.size());
  Stream.EmitRecordWithBlob(Abbrevs.get(ID), Record, Loc.Filename);
}

void ClangDocBitcodeWriter::emitRecord(bool Val, RecordId ID) {
  assert(RecordIdNameMap[ID] && "Unknown RecordId.");
  assert(RecordIdNameMap[ID].Abbrev == &genBoolAbbrev &&
         "Abbrev type mismatch.");
  if (!prepRecordData(ID, Val))
    return;
  Record.push_back(Val);
  Stream.EmitRecordWithAbbrev(Abbrevs.get(ID), Record);
}

void ClangDocBitcodeWriter::emitRecord(int Val, RecordId ID) {
  assert(RecordIdNameMap[ID] && "Unknown RecordId.");
  assert(RecordIdNameMap[ID].Abbrev == &genIntAbbrev &&
         "Abbrev type mismatch.");
  if (!prepRecordData(ID, Val))
    return;
  // FIXME: Assert that the integer is of the appropriate size.
  Record.push_back(Val);
  Stream.EmitRecordWithAbbrev(Abbrevs.get(ID), Record);
}

void ClangDocBitcodeWriter::emitRecord(unsigned Val, RecordId ID) {
  assert(RecordIdNameMap[ID] && "Unknown RecordId.");
  assert(RecordIdNameMap[ID].Abbrev == &genIntAbbrev &&
         "Abbrev type mismatch.");
  if (!prepRecordData(ID, Val))
    return;
  assert(Val < (1U << BitCodeConstants::IntSize));
  Record.push_back(Val);
  Stream.EmitRecordWithAbbrev(Abbrevs.get(ID), Record);
}

void ClangDocBitcodeWriter::emitRecord(const TemplateInfo &Templ) {}

bool ClangDocBitcodeWriter::prepRecordData(RecordId ID, bool ShouldEmit) {
  assert(RecordIdNameMap[ID] && "Unknown RecordId.");
  if (!ShouldEmit)
    return false;
  Record.clear();
  Record.push_back(ID);
  return true;
}

// BlockInfo Block

void ClangDocBitcodeWriter::emitBlockInfoBlock() {
  Stream.EnterBlockInfoBlock();
  for (const auto &Block : RecordsByBlock) {
    assert(Block.RIDs.size() < (1U << BitCodeConstants::SubblockIDSize));
    emitBlockInfo(Block.BID, Block.RIDs);
  }
  Stream.ExitBlock();
}

void ClangDocBitcodeWriter::emitBlockInfo(BlockId BID,
                                          ArrayRef<RecordId> RIDs) {
  assert(RIDs.size() < (1U << BitCodeConstants::SubblockIDSize));
  emitBlockID(BID);
  for (RecordId RID : RIDs) {
    emitRecordID(RID);
    emitAbbrev(RID, BID);
  }
}

// Block emission

// Generate ClangDocBitcodeWriter::emitScalars() for each block; every emitBlock
// writes its scalar records through the matching overload.
#define CLANG_DOC_RECORD_WRITER
#include "ClangDocRecords.def"

void ClangDocBitcodeWriter::emitBlock(const Reference &R, FieldId Field) {
  if (R.USR == EmptySID && R.Name.empty())
    return;
  StreamSubBlockGuard Block(Stream, BI_REFERENCE_BLOCK_ID);
  emitRecord(R.USR, REFERENCE_USR);
  emitRecord(R.Name, REFERENCE_NAME);
  emitRecord(R.QualName, REFERENCE_QUAL_NAME);
  emitRecord((unsigned)R.RefType, REFERENCE_TYPE);
  emitRecord(R.Path, REFERENCE_PATH);
  emitRecord((unsigned)Field, REFERENCE_FIELD);
  emitRecord(R.DocumentationFileName, REFERENCE_FILE);
}

void ClangDocBitcodeWriter::emitBlock(const FriendInfo &R) {
  StreamSubBlockGuard Block(Stream, BI_FRIEND_BLOCK_ID);
  emitBlock(R.Ref, FieldId::F_friend);
  emitScalars(R);
  if (R.Template)
    emitBlock(*R.Template);
  for (const auto &P : R.Params)
    emitBlock(P);
  if (R.ReturnType)
    emitBlock(*R.ReturnType);
  for (const auto &CI : R.Description)
    emitBlock(*CI.Ptr);
}

void ClangDocBitcodeWriter::emitBlock(const TypeInfo &T) {
  StreamSubBlockGuard Block(Stream, BI_TYPE_BLOCK_ID);
  emitBlock(T.Type, FieldId::F_type);
  emitScalars(T);
  for (const auto &Arg : T.TemplateArgs)
    emitBlock(Arg);
}

void ClangDocBitcodeWriter::emitBlock(const TypedefInfo &T) {
  StreamSubBlockGuard Block(Stream, BI_TYPEDEF_BLOCK_ID);
  emitScalars(T);
  for (const auto &N : T.Namespace)
    emitBlock(N, FieldId::F_namespace);
  for (const auto &CI : T.Description)
    emitBlock(*CI.Ptr);
  if (T.Template)
    emitBlock(*T.Template);
  emitBlock(T.Underlying);
}

void ClangDocBitcodeWriter::emitBlock(const FieldTypeInfo &T) {
  StreamSubBlockGuard Block(Stream, BI_FIELD_TYPE_BLOCK_ID);
  emitBlock(T.Type, FieldId::F_type);
  emitScalars(T);
  for (const auto &Arg : T.TemplateArgs)
    emitBlock(Arg);
}

void ClangDocBitcodeWriter::emitBlock(const MemberTypeInfo &T) {
  StreamSubBlockGuard Block(Stream, BI_MEMBER_TYPE_BLOCK_ID);
  emitBlock(T.Type, FieldId::F_type);
  emitScalars(T);
  for (const auto &Arg : T.TemplateArgs)
    emitBlock(Arg);
  for (const auto &CI : T.Description)
    emitBlock(*CI.Ptr);
}

void ClangDocBitcodeWriter::emitBlock(const CommentInfo &I) {
  StreamSubBlockGuard Block(Stream, BI_COMMENT_BLOCK_ID);
  // Handle Kind (enum) separately, since it is not a string.
  emitRecord(commentKindToString(I.Kind), COMMENT_KIND);
  for (const auto &L : std::vector<std::pair<StringRef, RecordId>>{
           {I.Text, COMMENT_TEXT},
           {I.Name, COMMENT_NAME},
           {I.Direction, COMMENT_DIRECTION},
           {I.ParamName, COMMENT_PARAMNAME},
           {I.CloseName, COMMENT_CLOSENAME}})
    emitRecord(L.first, L.second);
  emitRecord(I.SelfClosing, COMMENT_SELFCLOSING);
  emitRecord(I.Explicit, COMMENT_EXPLICIT);
  for (const auto &A : I.AttrKeys)
    emitRecord(A, COMMENT_ATTRKEY);
  for (const auto &A : I.AttrValues)
    emitRecord(A, COMMENT_ATTRVAL);
  for (const auto &A : I.Args)
    emitRecord(A, COMMENT_ARG);
  for (const auto &C : I.Children)
    emitBlock(C);
}

void ClangDocBitcodeWriter::emitBlock(const NamespaceInfo &I) {
  StreamSubBlockGuard Block(Stream, BI_NAMESPACE_BLOCK_ID);
  emitScalars(I);
  for (const auto &N : I.Namespace)
    emitBlock(N, FieldId::F_namespace);
  for (const auto &CI : I.Description)
    emitBlock(*CI.Ptr);
  for (const auto &C : I.Children.Namespaces)
    emitBlock(C, FieldId::F_child_namespace);
  for (const auto &C : I.Children.Records)
    emitBlock(C, FieldId::F_child_record);
  for (const auto &C : I.Children.Functions)
    emitBlock(C);
  for (const auto &C : I.Children.Enums)
    emitBlock(C);
  for (const auto &C : I.Children.Typedefs)
    emitBlock(C);
  for (const auto &C : I.Children.Concepts)
    emitBlock(C);
  for (const auto &C : I.Children.Variables)
    emitBlock(C);
}

void ClangDocBitcodeWriter::emitBlock(const EnumInfo &I) {
  StreamSubBlockGuard Block(Stream, BI_ENUM_BLOCK_ID);
  emitScalars(I);
  for (const auto &N : I.Namespace)
    emitBlock(N, FieldId::F_namespace);
  for (const auto &CI : I.Description)
    emitBlock(*CI.Ptr);
  if (I.BaseType)
    emitBlock(*I.BaseType);
  for (const auto &N : I.Members)
    emitBlock(N);
}

void ClangDocBitcodeWriter::emitBlock(const EnumValueInfo &I) {
  StreamSubBlockGuard Block(Stream, BI_ENUM_VALUE_BLOCK_ID);
  emitScalars(I);
  for (const auto &CI : I.Description)
    emitBlock(*CI.Ptr);
}

void ClangDocBitcodeWriter::emitBlock(const RecordInfo &I) {
  StreamSubBlockGuard Block(Stream, BI_RECORD_BLOCK_ID);
  emitScalars(I);
  for (const auto &N : I.Namespace)
    emitBlock(N, FieldId::F_namespace);
  for (const auto &CI : I.Description)
    emitBlock(*CI.Ptr);
  for (const auto &N : I.Members)
    emitBlock(N);
  for (const auto &P : I.Parents)
    emitBlock(P, FieldId::F_parent);
  for (const auto &P : I.VirtualParents)
    emitBlock(P, FieldId::F_vparent);
  for (const auto &PB : I.Bases)
    emitBlock(PB);
  for (const auto &C : I.Children.Records)
    emitBlock(C, FieldId::F_child_record);
  for (const auto &C : I.Children.Functions)
    emitBlock(C);
  for (const auto &C : I.Children.Enums)
    emitBlock(C);
  for (const auto &C : I.Children.Typedefs)
    emitBlock(C);
  if (I.Template)
    emitBlock(*I.Template);
  for (const auto &C : I.Friends)
    emitBlock(C);
}

void ClangDocBitcodeWriter::emitBlock(const BaseRecordInfo &I) {
  StreamSubBlockGuard Block(Stream, BI_BASE_RECORD_BLOCK_ID);
  emitScalars(I);
  for (const auto &M : I.Members)
    emitBlock(M);
  for (const auto &C : I.Children.Functions)
    emitBlock(C);
}

void ClangDocBitcodeWriter::emitBlock(const FunctionInfo &I) {
  StreamSubBlockGuard Block(Stream, BI_FUNCTION_BLOCK_ID);
  emitScalars(I);
  for (const auto &N : I.Namespace)
    emitBlock(N, FieldId::F_namespace);
  for (const auto &CI : I.Description)
    emitBlock(*CI.Ptr);
  emitBlock(I.Parent, FieldId::F_parent);
  emitBlock(I.ReturnType);
  for (const auto &N : I.Params)
    emitBlock(N);
  if (I.Template)
    emitBlock(*I.Template);
}

void ClangDocBitcodeWriter::emitBlock(const ConceptInfo &I) {
  StreamSubBlockGuard Block(Stream, BI_CONCEPT_BLOCK_ID);
  emitScalars(I);
  for (const auto &CI : I.Description)
    emitBlock(*CI.Ptr);
  emitBlock(I.Template);
}

void ClangDocBitcodeWriter::emitBlock(const TemplateInfo &T) {
  StreamSubBlockGuard Block(Stream, BI_TEMPLATE_BLOCK_ID);
  for (const auto &P : T.Params)
    emitBlock(P);
  if (T.Specialization)
    emitBlock(*T.Specialization);
  for (const auto &C : T.Constraints)
    emitBlock(C);
}

void ClangDocBitcodeWriter::emitBlock(const TemplateSpecializationInfo &T) {
  StreamSubBlockGuard Block(Stream, BI_TEMPLATE_SPECIALIZATION_BLOCK_ID);
  emitScalars(T);
  for (const auto &P : T.Params)
    emitBlock(P);
}

void ClangDocBitcodeWriter::emitBlock(const TemplateParamInfo &T) {
  StreamSubBlockGuard Block(Stream, BI_TEMPLATE_PARAM_BLOCK_ID);
  emitScalars(T);
}

void ClangDocBitcodeWriter::emitBlock(const ConstraintInfo &C) {
  StreamSubBlockGuard Block(Stream, BI_CONSTRAINT_BLOCK_ID);
  emitScalars(C);
  emitBlock(C.ConceptRef, FieldId::F_concept);
}

void ClangDocBitcodeWriter::emitBlock(const VarInfo &I) {
  StreamSubBlockGuard Block(Stream, BI_VAR_BLOCK_ID);
  emitScalars(I);
  for (const auto &N : I.Namespace)
    emitBlock(N, FieldId::F_namespace);
  for (const auto &CI : I.Description)
    emitBlock(*CI.Ptr);
  emitBlock(I.Type);
}

bool ClangDocBitcodeWriter::dispatchInfoForWrite(Info *I) {
  switch (I->IT) {
  case InfoType::IT_namespace:
    emitBlock(*cast<NamespaceInfo>(I));
    break;
  case InfoType::IT_record:
    emitBlock(*cast<RecordInfo>(I));
    break;
  case InfoType::IT_enum:
    emitBlock(*cast<EnumInfo>(I));
    break;
  case InfoType::IT_function:
    emitBlock(*cast<FunctionInfo>(I));
    break;
  case InfoType::IT_typedef:
    emitBlock(*cast<TypedefInfo>(I));
    break;
  case InfoType::IT_concept:
    emitBlock(*cast<ConceptInfo>(I));
    break;
  case InfoType::IT_variable:
    emitBlock(*cast<VarInfo>(I));
    break;
  case InfoType::IT_friend:
    emitBlock(*cast<FriendInfo>(I));
    break;
  case InfoType::IT_default:
    unsigned ID = Diags.getCustomDiagID(DiagnosticsEngine::Error,
                                        "Unexpected info, unable to write.");
    Diags.Report(ID);
    return true;
  }
  return false;
}

} // namespace doc
} // namespace clang
