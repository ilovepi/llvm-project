//===-- HTMLMustacheGenerator.cpp - HTML Mustache Generator -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "Generators.h"
#include "Representation.h"
#include "support/File.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mustache.h"

using namespace llvm;
using namespace llvm::json;
using namespace llvm::mustache;

namespace clang {
namespace doc {

class MustacheHTMLGenerator : public Generator {
public:
  static const char *Format;
  Error generateDocs(StringRef RootDir,
                           StringMap<std::unique_ptr<doc::Info>> Infos,
                           const ClangDocContext &CDCtx) override;
  Error createResources(ClangDocContext &CDCtx) override;
  Error generateDocForInfo(Info *I, raw_ostream &OS,
                                 const ClangDocContext &CDCtx) override;
};

class MustacheTemplateFile : public Template {
public:
  static ErrorOr<std::unique_ptr<MustacheTemplateFile>>
  createMustacheFile(StringRef FileName) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrError =
        MemoryBuffer::getFile(FileName);
    if (auto EC = BufferOrError.getError())
      return EC;

    std::unique_ptr<MemoryBuffer> Buffer = std::move(BufferOrError.get());
    StringRef FileContent = Buffer->getBuffer();
    return std::make_unique<MustacheTemplateFile>(FileContent);
  }

  Error registerPartialFile(StringRef Name, StringRef FileName) {
    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrError =
        MemoryBuffer::getFile(FileName);
    if (auto EC = BufferOrError.getError())
      return createFileError("cannot open file", EC);
    std::unique_ptr<MemoryBuffer> Buffer = std::move(BufferOrError.get());
    StringRef FileContent = Buffer->getBuffer();
    registerPartial(Name.str(), FileContent.str());
    return Error::success();
  }

  MustacheTemplateFile(StringRef TemplateStr) : Template(TemplateStr) {}
};

static std::unique_ptr<MustacheTemplateFile> NamespaceTemplate = nullptr;

static std::unique_ptr<MustacheTemplateFile> RecordTemplate = nullptr;

static Error
setupTemplate(std::unique_ptr<MustacheTemplateFile> &Template,
              StringRef TemplatePath,
              std::vector<std::pair<StringRef, StringRef>> Partials) {
  auto T = MustacheTemplateFile::createMustacheFile(TemplatePath);
  if (auto EC = T.getError())
    return createFileError("cannot open file", EC);
  Template = std::move(T.get());
  for (const auto [Name, FileName] : Partials) {
    if (auto Err = Template->registerPartialFile(Name, FileName))
      return Err;
  }
  return Error::success();
}

static Error setupTemplateFiles(const clang::doc::ClangDocContext &CDCtx) {
  std::string NamespaceFilePath =
      CDCtx.MustacheTemplates.lookup("namespace-template");
  std::string ClassFilePath = CDCtx.MustacheTemplates.lookup("class-template");
  std::string CommentFilePath =
      CDCtx.MustacheTemplates.lookup("comments-template");
  std::string FunctionFilePath =
      CDCtx.MustacheTemplates.lookup("function-template");
  std::string EnumFilePath = CDCtx.MustacheTemplates.lookup("enum-template");
  std::vector<std::pair<StringRef, StringRef>> Partials = {
      {"Comments", CommentFilePath},
      {"FunctionPartial", FunctionFilePath},
      {"EnumPartial", EnumFilePath}};

  if(Error Err = setupTemplate(NamespaceTemplate, NamespaceFilePath, Partials))
    return Err;

  if(Error Err = setupTemplate(RecordTemplate, ClassFilePath, Partials))
    return Err;

  return Error::success();
}

Error MustacheHTMLGenerator::generateDocs(
    StringRef RootDir, StringMap<std::unique_ptr<doc::Info>> Infos,
    const clang::doc::ClangDocContext &CDCtx) {
  if (auto Err = setupTemplateFiles(CDCtx))
    return Err;
  // Track which directories we already tried to create.
  StringSet<> CreatedDirs;
  // Collect all output by file name and create the necessary directories.
  StringMap<std::vector<doc::Info *>> FileToInfos;
  for (const auto &Group : Infos) {
    doc::Info *Info = Group.getValue().get();

    SmallString<128> Path;
    sys::path::native(RootDir, Path);
    sys::path::append(Path, Info->getRelativeFilePath(""));
    if (!CreatedDirs.contains(Path)) {
      if (std::error_code Err = sys::fs::create_directories(Path);
          Err != std::error_code())
        return createStringError(Err, "Failed to create directory '%s'.",
                                 Path.c_str());
      CreatedDirs.insert(Path);
    }

    sys::path::append(Path, Info->getFileBaseName() + ".html");
    FileToInfos[Path].push_back(Info);
  }

  for (const auto &Group : FileToInfos) {
    std::error_code FileErr;
    raw_fd_ostream InfoOS(Group.getKey(), FileErr,
                                sys::fs::OF_None);
    if (FileErr)
      return createStringError(FileErr, "Error opening file '%s'",
                                     Group.getKey().data());

    for (const auto &Info : Group.getValue()) {
      if (Error Err = generateDocForInfo(Info, InfoOS, CDCtx))
        return Err;
    }
  }
  return Error::success();
}

static json::Value
extractValue(const Location &L,
             std::optional<StringRef> RepositoryUrl = std::nullopt) {
  Object Obj = Object();
  // Should there be Start/End line numbers?
  Obj.insert({"LineNumber", L.StartLineNumber});
  Obj.insert({"Filename", L.Filename});

  if (!L.IsFileInRootDir || !RepositoryUrl) {
    return Obj;
  }
  SmallString<128> FileURL(*RepositoryUrl);
  sys::path::append(FileURL, sys::path::Style::posix, L.Filename);
  FileURL += "#" + std::to_string(L.StartLineNumber);
  Obj.insert({"FileURL", FileURL});

  return Obj;
}

static json::Value extractValue(const Reference &I, StringRef CurrentDirectory) {
  SmallString<64> Path = I.getRelativeFilePath(CurrentDirectory);
  sys::path::append(Path, I.getFileBaseName() + ".html");
  sys::path::native(Path, sys::path::Style::posix);
  Object Obj = Object();
  Obj.insert({"Link", Path});
  Obj.insert({"Name", I.Name});
  Obj.insert({"QualName", I.QualName});
  Obj.insert({"ID", toHex(toStringRef(I.USR))});
  return Obj;
}

static json::Value extractValue(const TypedefInfo &I) {
  // Not Supported
  return nullptr;
}

static json::Value extractValue(const CommentInfo &I) {
  assert((I.Kind == "BlockCommandComment" || I.Kind == "FullComment" ||
          I.Kind == "ParagraphComment" || I.Kind == "TextComment") &&
         "Unknown Comment type in CommentInfo.");

  Object Obj = Object();
  json::Value Child = Object();

  // TextComment has no children, so return it.
  if (I.Kind == "TextComment") {
    Obj.insert({"TextComment", I.Text});
    return Obj;
  }

  // BlockCommandComment needs to generate a Command key.
  if(I.Kind == "BlockCommandComment"){
    Child.getAsObject()->insert({"Command", I.Name});
  }

  // Use the same handling for everything else.
  // Only valid for:
  //  - BlockCommandComment
  //  - FullComment
  //  - ParagraphComment
  json::Value ChildArr = Array();
  for (const auto &C : I.Children)
    ChildArr.getAsArray()->emplace_back(extractValue(*C));
  Child.getAsObject()->insert({"Children", ChildArr});
  Obj.insert({I.Kind, Child});

  return Obj;
}

static void maybeInsertLocation(std::optional<Location> Loc,
                                const ClangDocContext &CDCtx, Object &Obj) {
  if (!Loc)
    return;
  Location L = *Loc;
  Obj.insert({"Location", extractValue(L, CDCtx.RepositoryUrl)});
}

static json::Value extractValue(const FunctionInfo &I, StringRef ParentInfoDir,
                                const ClangDocContext &CDCtx) {
  Object Obj = Object();
  Obj.insert({"Name", I.Name});
  Obj.insert({"ID", toHex(toStringRef(I.USR))});
  Obj.insert({"Access", getAccessSpelling(I.Access).str()});
  Obj.insert({"ReturnType", extractValue(I.ReturnType.Type, ParentInfoDir)});

  json::Value ParamArr = Array();
  for (const auto Val : enumerate(I.Params)) {
    json::Value V = Object();
    V.getAsObject()->insert({"Name", Val.value().Name});
    V.getAsObject()->insert({"Type", Val.value().Type.Name});
    V.getAsObject()->insert({"End", Val.index() + 1 == I.Params.size()});
    ParamArr.getAsArray()->emplace_back(V);
  }
  Obj.insert({"Params", ParamArr});

  if (!I.Description.empty()) {
    json::Value ArrDesc = Array();
    for (const CommentInfo &Child : I.Description)
      ArrDesc.getAsArray()->emplace_back(extractValue(Child));
    Obj.insert({"FunctionComments", ArrDesc});
  }

  maybeInsertLocation(I.DefLoc, CDCtx, Obj);
  return Obj;
}

static void extractDescriptionFromInfo(ArrayRef<CommentInfo> Descriptions,
                                       json::Object &EnumValObj) {
  if(Descriptions.empty())
    return;
  json::Value ArrDesc = Array();
  json::Array &ADescRef = *ArrDesc.getAsArray();
  for (const CommentInfo &Child : Descriptions)
    ADescRef.emplace_back(extractValue(Child));
  EnumValObj.insert({"EnumValueComments", ArrDesc});
}

static json::Value extractValue(const EnumInfo &I,
                                const ClangDocContext &CDCtx) {
  Object Obj = Object();
  std::string EnumType = I.Scoped ? "enum class " : "enum ";
  EnumType += I.Name;
  bool HasComment = std::any_of(
      I.Members.begin(), I.Members.end(),
      [](const EnumValueInfo &M) { return !M.Description.empty(); });
  Obj.insert({"EnumName", EnumType});
  Obj.insert({"HasComment", HasComment});
  Obj.insert({"ID", toHex(toStringRef(I.USR))});
  json::Value Arr = Array();
  json::Array &ARef = *Arr.getAsArray();
  for (const EnumValueInfo &M : I.Members) {
    json::Value EnumValue = Object();
    auto &EnumValObj = *EnumValue.getAsObject();
    EnumValObj.insert({"Name", M.Name});
    if (!M.ValueExpr.empty())
      EnumValObj.insert({"ValueExpr", M.ValueExpr});
    else
      EnumValObj.insert({"Value", M.Value});

    extractDescriptionFromInfo(M.Description, EnumValObj);
    ARef.emplace_back(EnumValue);
  }
  Obj.insert({"EnumValues", Arr});

  extractDescriptionFromInfo(I.Description, Obj);
  maybeInsertLocation(I.DefLoc, CDCtx, Obj);

  return Obj;
}

static void extractScopeChildren(const ScopeChildren &S, Object &Obj,
                          StringRef ParentInfoDir,
                          const ClangDocContext &CDCtx) {
  json::Value ArrNamespace = Array();
  for (const Reference &Child : S.Namespaces)
    ArrNamespace.getAsArray()->emplace_back(extractValue(Child, ParentInfoDir));

  if (!ArrNamespace.getAsArray()->empty())
    Obj.insert({"Namespace", Object{{"Links", ArrNamespace}}});

  json::Value ArrRecord = Array();
  for (const Reference &Child : S.Records)
    ArrRecord.getAsArray()->emplace_back(extractValue(Child, ParentInfoDir));

  if (!ArrRecord.getAsArray()->empty())
    Obj.insert({"Record", Object{{"Links", ArrRecord}}});

  json::Value ArrFunction = Array();
  json::Value PublicFunction = Array();
  json::Value ProtectedFunction = Array();
  json::Value PrivateFunction = Array();

  for (const FunctionInfo &Child : S.Functions) {
    json::Value F = extractValue(Child, ParentInfoDir, CDCtx);
    AccessSpecifier Access = Child.Access;
    if (Access == AccessSpecifier::AS_public)
      PublicFunction.getAsArray()->emplace_back(F);
    else if (Access == AccessSpecifier::AS_protected)
      ProtectedFunction.getAsArray()->emplace_back(F);
    else
      ArrFunction.getAsArray()->emplace_back(F);
  }

  if (!ArrFunction.getAsArray()->empty())
    Obj.insert({"Function", Object{{"Obj", ArrFunction}}});

  if (!PublicFunction.getAsArray()->empty())
    Obj.insert({"PublicFunction", Object{{"Obj", PublicFunction}}});

  if (!ProtectedFunction.getAsArray()->empty())
    Obj.insert({"ProtectedFunction", Object{{"Obj", ProtectedFunction}}});

  json::Value ArrEnum = Array();
  auto& ArrEnumRef = *ArrEnum.getAsArray();
  for (const EnumInfo &Child : S.Enums)
    ArrEnumRef.emplace_back(extractValue(Child, CDCtx));

  if (!ArrEnumRef.empty())
    Obj.insert({"Enums", Object{{"Obj", ArrEnum}}});

  json::Value ArrTypedefs = Array();
  auto& ArrTypedefsRef = *ArrTypedefs.getAsArray();
  for (const TypedefInfo &Child : S.Typedefs)
    ArrTypedefsRef.emplace_back(extractValue(Child));

  if (!ArrTypedefsRef.empty())
    Obj.insert({"Typedefs", Object{{"Obj", ArrTypedefs}}});
}

static json::Value extractValue(const NamespaceInfo &I, const ClangDocContext &CDCtx) {
  Object NamespaceValue = Object();
  std::string InfoTitle = I.Name.empty() ? "Global Namespace"
                                         : (Twine("namespace ") + I.Name).str();

  StringRef BasePath = I.getRelativeFilePath("");
  NamespaceValue.insert({"NamespaceTitle", InfoTitle});
  NamespaceValue.insert({"NamespacePath", BasePath});

  extractDescriptionFromInfo(I.Description, NamespaceValue);
  extractScopeChildren(I.Children, NamespaceValue, BasePath, CDCtx);
  return NamespaceValue;
}

static json::Value extractValue(const RecordInfo &I, const ClangDocContext &CDCtx) {
  Object RecordValue = Object();
  extractDescriptionFromInfo(I.Description, RecordValue);
  RecordValue.insert({"Name", I.Name});
  RecordValue.insert({"FullName", I.FullName});
  RecordValue.insert({"RecordType", getTagType(I.TagType)});

  maybeInsertLocation(I.DefLoc, CDCtx, RecordValue);

  StringRef BasePath = I.getRelativeFilePath("");
  extractScopeChildren(I.Children, RecordValue, BasePath, CDCtx);
  json::Value PublicMembers = Array();
  json::Array &PubMemberRef = *PublicMembers.getAsArray();
  json::Value ProtectedMembers = Array();
  json::Array &ProtMemberRef = *ProtectedMembers.getAsArray();
  json::Value PrivateMembers = Array();
  json::Array &PrivMemberRef = *PrivateMembers.getAsArray();
  for (const MemberTypeInfo &Member : I.Members) {
    json::Value MemberValue = Object();
    auto &MVRef = *MemberValue.getAsObject();
    MVRef.insert({"Name", Member.Name});
    MVRef.insert({"Type", Member.Type.Name});
    extractDescriptionFromInfo(Member.Description, MVRef);

    if (Member.Access == AccessSpecifier::AS_public)
      PubMemberRef.emplace_back(MemberValue);
    else if (Member.Access == AccessSpecifier::AS_protected)
      ProtMemberRef.emplace_back(MemberValue);
    else if (Member.Access == AccessSpecifier::AS_private)
      ProtMemberRef.emplace_back(MemberValue);
  }
  if (!PubMemberRef.empty())
    RecordValue.insert({"PublicMembers", Object{{"Obj", PublicMembers}}});
  if (!ProtMemberRef.empty())
    RecordValue.insert({"ProtectedMembers", Object{{"Obj", ProtectedMembers}}});
  if (!PrivMemberRef.empty())
    RecordValue.insert({"PrivateMembers", Object{{"Obj", PrivateMembers}}});

  return RecordValue;
}

static void setupTemplateValue(const ClangDocContext &CDCtx, json::Value &V, Info *I) {
  V.getAsObject()->insert({"ProjectName", CDCtx.ProjectName});
  json::Value StylesheetArr = Array();
  auto InfoPath = I->getRelativeFilePath("");
  SmallString<128> RelativePath = computeRelativePath("", InfoPath);
  for (const auto &FilePath : CDCtx.UserStylesheets) {
    SmallString<128> StylesheetPath = RelativePath;
    sys::path::append(StylesheetPath,
                            sys::path::filename(FilePath));
    sys::path::native(StylesheetPath, sys::path::Style::posix);
    StylesheetArr.getAsArray()->emplace_back(StylesheetPath);
  }
  V.getAsObject()->insert({"Stylesheets", StylesheetArr});

  json::Value ScriptArr = Array();
  for (auto Script : CDCtx.JsScripts) {
    SmallString<128> JsPath = RelativePath;
    sys::path::append(JsPath, sys::path::filename(Script));
    ScriptArr.getAsArray()->emplace_back(JsPath);
  }
  V.getAsObject()->insert({"Scripts", ScriptArr});
}

Error
MustacheHTMLGenerator::generateDocForInfo(Info *I, raw_ostream &OS,
                                          const ClangDocContext &CDCtx) {
  switch (I->IT) {
  case InfoType::IT_namespace: {
    json::Value V =
        extractValue(*static_cast<clang::doc::NamespaceInfo *>(I), CDCtx);
    setupTemplateValue(CDCtx, V, I);
    NamespaceTemplate->render(V, OS);
    break;
  }
  case InfoType::IT_record: {
    json::Value V =
        extractValue(*static_cast<clang::doc::RecordInfo *>(I), CDCtx);
    setupTemplateValue(CDCtx, V, I);
    // Serialize the JSON value to the output stream in a readable format.
    outs() << "Visit: " << I->Name << "\n";
    // outs() << formatv("{0:2}", V) << "\n";
    RecordTemplate->render(V, outs());
    break;
  }
  case InfoType::IT_enum:
    outs() << "IT_enum\n";
    break;
  case InfoType::IT_function:
    outs() << "IT_Function\n";
    break;
  case InfoType::IT_typedef:
    outs() << "IT_typedef\n";
    break;
  case InfoType::IT_default:
    return createStringError(inconvertibleErrorCode(),
                             "unexpected InfoType");
  }
  return Error::success();
}

Error MustacheHTMLGenerator::createResources(ClangDocContext &CDCtx) {
  Error Err = Error::success();
  for (const auto &FilePath : CDCtx.UserStylesheets) {
    Err = copyFile(FilePath, CDCtx.OutDirectory);
    if (Err)
      return Err;
  }
  for (const auto &FilePath : CDCtx.JsScripts) {
    Err = copyFile(FilePath, CDCtx.OutDirectory);
    if (Err)
      return Err;
  }
  return Error::success();
}

const char *MustacheHTMLGenerator::Format = "mhtml";

static GeneratorRegistry::Add<MustacheHTMLGenerator>
    MHTML(MustacheHTMLGenerator::Format, "Generator for mustache HTML output.");

// This anchor is used to force the linker to link in the generated object
// file and thus register the generator.
volatile int MHTMLGeneratorAnchorSource = 0;

} // namespace doc
} // namespace clang
