//===-- ConvertType.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "flang/Lower/ConvertType.h"
#include "flang/Common/type-kinds.h"
#include "flang/Lower/AbstractConverter.h"
#include "flang/Lower/CallInterface.h"
#include "flang/Lower/ConvertVariable.h"
#include "flang/Lower/Mangler.h"
#include "flang/Lower/PFTBuilder.h"
#include "flang/Lower/Support/Utils.h"
#include "flang/Optimizer/Builder/Todo.h"
#include "flang/Optimizer/Dialect/FIRType.h"
#include "flang/Semantics/tools.h"
#include "flang/Semantics/type.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinTypes.h"
#include "llvm/Support/Debug.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"

#define DEBUG_TYPE "flang-lower-type"

using Fortran::common::VectorElementCategory;

//===--------------------------------------------------------------------===//
// Intrinsic type translation helpers
//===--------------------------------------------------------------------===//

static mlir::Type genRealType(mlir::MLIRContext *context, int kind) {
  if (Fortran::common::IsValidKindOfIntrinsicType(
          Fortran::common::TypeCategory::Real, kind)) {
    switch (kind) {
    case 2:
      return mlir::Float16Type::get(context);
    case 3:
      return mlir::BFloat16Type::get(context);
    case 4:
      return mlir::Float32Type::get(context);
    case 8:
      return mlir::Float64Type::get(context);
    case 10:
      return mlir::Float80Type::get(context);
    case 16:
      return mlir::Float128Type::get(context);
    }
  }
  llvm_unreachable("REAL type translation not implemented");
}

template <int KIND>
int getIntegerBits() {
  return Fortran::evaluate::Type<Fortran::common::TypeCategory::Integer,
                                 KIND>::Scalar::bits;
}
static mlir::Type genIntegerType(mlir::MLIRContext *context, int kind,
                                 bool isUnsigned = false) {
  if (Fortran::common::IsValidKindOfIntrinsicType(
          Fortran::common::TypeCategory::Integer, kind)) {
    mlir::IntegerType::SignednessSemantics signedness =
        (isUnsigned ? mlir::IntegerType::SignednessSemantics::Unsigned
                    : mlir::IntegerType::SignednessSemantics::Signless);

    switch (kind) {
    case 1:
      return mlir::IntegerType::get(context, getIntegerBits<1>(), signedness);
    case 2:
      return mlir::IntegerType::get(context, getIntegerBits<2>(), signedness);
    case 4:
      return mlir::IntegerType::get(context, getIntegerBits<4>(), signedness);
    case 8:
      return mlir::IntegerType::get(context, getIntegerBits<8>(), signedness);
    case 16:
      return mlir::IntegerType::get(context, getIntegerBits<16>(), signedness);
    }
  }
  llvm_unreachable("INTEGER or UNSIGNED kind not translated");
}

static mlir::Type genLogicalType(mlir::MLIRContext *context, int KIND) {
  if (Fortran::common::IsValidKindOfIntrinsicType(
          Fortran::common::TypeCategory::Logical, KIND))
    return fir::LogicalType::get(context, KIND);
  return {};
}

static mlir::Type genCharacterType(
    mlir::MLIRContext *context, int KIND,
    Fortran::lower::LenParameterTy len = fir::CharacterType::unknownLen()) {
  if (Fortran::common::IsValidKindOfIntrinsicType(
          Fortran::common::TypeCategory::Character, KIND))
    return fir::CharacterType::get(context, KIND, len);
  return {};
}

static mlir::Type genComplexType(mlir::MLIRContext *context, int KIND) {
  return mlir::ComplexType::get(genRealType(context, KIND));
}

static mlir::Type
genFIRType(mlir::MLIRContext *context, Fortran::common::TypeCategory tc,
           int kind,
           llvm::ArrayRef<Fortran::lower::LenParameterTy> lenParameters) {
  switch (tc) {
  case Fortran::common::TypeCategory::Real:
    return genRealType(context, kind);
  case Fortran::common::TypeCategory::Integer:
    return genIntegerType(context, kind, false);
  case Fortran::common::TypeCategory::Unsigned:
    return genIntegerType(context, kind, true);
  case Fortran::common::TypeCategory::Complex:
    return genComplexType(context, kind);
  case Fortran::common::TypeCategory::Logical:
    return genLogicalType(context, kind);
  case Fortran::common::TypeCategory::Character:
    if (!lenParameters.empty())
      return genCharacterType(context, kind, lenParameters[0]);
    return genCharacterType(context, kind);
  default:
    break;
  }
  llvm_unreachable("unhandled type category");
}

//===--------------------------------------------------------------------===//
// Symbol and expression type translation
//===--------------------------------------------------------------------===//

/// TypeBuilderImpl translates expression and symbol type taking into account
/// their shape and length parameters. For symbols, attributes such as
/// ALLOCATABLE or POINTER are reflected in the fir type.
/// It uses evaluate::DynamicType and evaluate::Shape when possible to
/// avoid re-implementing type/shape analysis here.
/// Do not use the FirOpBuilder from the AbstractConverter to get fir/mlir types
/// since it is not guaranteed to exist yet when we lower types.
namespace {
struct TypeBuilderImpl {

  TypeBuilderImpl(Fortran::lower::AbstractConverter &converter)
      : derivedTypeInConstruction{converter.getTypeConstructionStack()},
        converter{converter}, context{&converter.getMLIRContext()} {}

  template <typename A>
  mlir::Type genExprType(const A &expr) {
    std::optional<Fortran::evaluate::DynamicType> dynamicType = expr.GetType();
    if (!dynamicType)
      return genTypelessExprType(expr);
    Fortran::common::TypeCategory category = dynamicType->category();

    mlir::Type baseType;
    bool isPolymorphic = (dynamicType->IsPolymorphic() ||
                          dynamicType->IsUnlimitedPolymorphic()) &&
                         !dynamicType->IsAssumedType();
    if (dynamicType->IsUnlimitedPolymorphic()) {
      baseType = mlir::NoneType::get(context);
    } else if (category == Fortran::common::TypeCategory::Derived) {
      baseType = genDerivedType(dynamicType->GetDerivedTypeSpec());
    } else {
      // INTEGER, UNSIGNED, REAL, COMPLEX, CHARACTER, LOGICAL
      llvm::SmallVector<Fortran::lower::LenParameterTy> params;
      translateLenParameters(params, category, expr);
      baseType = genFIRType(context, category, dynamicType->kind(), params);
    }
    std::optional<Fortran::evaluate::Shape> shapeExpr =
        Fortran::evaluate::GetShape(converter.getFoldingContext(), expr);
    fir::SequenceType::Shape shape;
    if (shapeExpr) {
      translateShape(shape, std::move(*shapeExpr));
    } else {
      // Shape static analysis cannot return something useful for the shape.
      // Use unknown extents.
      int rank = expr.Rank();
      if (rank < 0)
        TODO(converter.getCurrentLocation(), "assumed rank expression types");
      for (int dim = 0; dim < rank; ++dim)
        shape.emplace_back(fir::SequenceType::getUnknownExtent());
    }

    if (!shape.empty()) {
      if (isPolymorphic)
        return fir::ClassType::get(fir::SequenceType::get(shape, baseType));
      return fir::SequenceType::get(shape, baseType);
    }
    if (isPolymorphic)
      return fir::ClassType::get(baseType);
    return baseType;
  }

  template <typename A>
  void translateShape(A &shape, Fortran::evaluate::Shape &&shapeExpr) {
    for (Fortran::evaluate::MaybeExtentExpr extentExpr : shapeExpr) {
      fir::SequenceType::Extent extent = fir::SequenceType::getUnknownExtent();
      if (std::optional<std::int64_t> constantExtent =
              toInt64(std::move(extentExpr)))
        extent = *constantExtent;
      shape.push_back(extent);
    }
  }

  template <typename A>
  std::optional<std::int64_t> toInt64(A &&expr) {
    return Fortran::evaluate::ToInt64(Fortran::evaluate::Fold(
        converter.getFoldingContext(), std::move(expr)));
  }

  template <typename A>
  mlir::Type genTypelessExprType(const A &expr) {
    fir::emitFatalError(converter.getCurrentLocation(), "not a typeless expr");
  }

  mlir::Type genTypelessExprType(const Fortran::lower::SomeExpr &expr) {
    return Fortran::common::visit(
        Fortran::common::visitors{
            [&](const Fortran::evaluate::BOZLiteralConstant &) -> mlir::Type {
              return mlir::NoneType::get(context);
            },
            [&](const Fortran::evaluate::NullPointer &) -> mlir::Type {
              return fir::ReferenceType::get(mlir::NoneType::get(context));
            },
            [&](const Fortran::evaluate::ProcedureDesignator &proc)
                -> mlir::Type {
              return Fortran::lower::translateSignature(proc, converter);
            },
            [&](const Fortran::evaluate::ProcedureRef &) -> mlir::Type {
              return mlir::NoneType::get(context);
            },
            [](const auto &x) -> mlir::Type {
              using T = std::decay_t<decltype(x)>;
              static_assert(!Fortran::common::HasMember<
                                T, Fortran::evaluate::TypelessExpression>,
                            "missing typeless expr handling");
              llvm::report_fatal_error("not a typeless expression");
            },
        },
        expr.u);
  }

  mlir::Type genSymbolType(const Fortran::semantics::Symbol &symbol,
                           bool isAlloc = false, bool isPtr = false) {
    mlir::Location loc = converter.genLocation(symbol.name());
    mlir::Type ty;
    // If the symbol is not the same as the ultimate one (i.e, it is host or use
    // associated), all the symbol properties are the ones of the ultimate
    // symbol but the volatile and asynchronous attributes that may differ. To
    // avoid issues with helper functions that would not follow association
    // links, the fir type is built based on the ultimate symbol. This relies
    // on the fact volatile and asynchronous are not reflected in fir types.
    const Fortran::semantics::Symbol &ultimate = symbol.GetUltimate();

    if (Fortran::semantics::IsProcedurePointer(ultimate)) {
      Fortran::evaluate::ProcedureDesignator proc(ultimate);
      auto procTy{Fortran::lower::translateSignature(proc, converter)};
      return fir::BoxProcType::get(context, procTy);
    }

    if (const Fortran::semantics::DeclTypeSpec *type = ultimate.GetType()) {
      if (const Fortran::semantics::IntrinsicTypeSpec *tySpec =
              type->AsIntrinsic()) {
        int kind = toInt64(Fortran::common::Clone(tySpec->kind())).value();
        llvm::SmallVector<Fortran::lower::LenParameterTy> params;
        translateLenParameters(params, tySpec->category(), ultimate);
        ty = genFIRType(context, tySpec->category(), kind, params);
      } else if (type->IsUnlimitedPolymorphic()) {
        ty = mlir::NoneType::get(context);
      } else if (const Fortran::semantics::DerivedTypeSpec *tySpec =
                     type->AsDerived()) {
        ty = genDerivedType(*tySpec);
      } else {
        fir::emitFatalError(loc, "symbol's type must have a type spec");
      }
    } else {
      fir::emitFatalError(loc, "symbol must have a type");
    }

    auto shapeExpr =
        Fortran::evaluate::GetShape(converter.getFoldingContext(), ultimate);

    if (shapeExpr && !shapeExpr->empty()) {
      // Statically ranked array.
      fir::SequenceType::Shape shape;
      translateShape(shape, std::move(*shapeExpr));
      ty = fir::SequenceType::get(shape, ty);
    } else if (!shapeExpr) {
      // Assumed-rank.
      ty = fir::SequenceType::get(fir::SequenceType::Shape{}, ty);
    }

    bool isPolymorphic = (Fortran::semantics::IsPolymorphic(symbol) ||
                          Fortran::semantics::IsUnlimitedPolymorphic(symbol)) &&
                         !Fortran::semantics::IsAssumedType(symbol);
    if (Fortran::semantics::IsPointer(symbol))
      return fir::wrapInClassOrBoxType(fir::PointerType::get(ty),
                                       isPolymorphic);
    if (Fortran::semantics::IsAllocatable(symbol))
      return fir::wrapInClassOrBoxType(fir::HeapType::get(ty), isPolymorphic);
    // isPtr and isAlloc are variable that were promoted to be on the
    // heap or to be pointers, but they do not have Fortran allocatable
    // or pointer semantics, so do not use box for them.
    if (isPtr)
      return fir::PointerType::get(ty);
    if (isAlloc)
      return fir::HeapType::get(ty);
    if (isPolymorphic)
      return fir::ClassType::get(ty);
    return ty;
  }

  /// Does \p component has non deferred lower bounds that are not compile time
  /// constant 1.
  static bool componentHasNonDefaultLowerBounds(
      const Fortran::semantics::Symbol &component) {
    if (const auto *objDetails =
            component.detailsIf<Fortran::semantics::ObjectEntityDetails>())
      for (const Fortran::semantics::ShapeSpec &bounds : objDetails->shape())
        if (auto lb = bounds.lbound().GetExplicit())
          if (auto constant = Fortran::evaluate::ToInt64(*lb))
            if (!constant || *constant != 1)
              return true;
    return false;
  }

  mlir::Type genVectorType(const Fortran::semantics::DerivedTypeSpec &tySpec) {
    assert(tySpec.scope() && "Missing scope for Vector type");
    auto vectorSize{tySpec.scope()->size()};
    switch (tySpec.category()) {
      SWITCH_COVERS_ALL_CASES
    case (Fortran::semantics::DerivedTypeSpec::Category::IntrinsicVector): {
      int64_t vecElemKind;
      int64_t vecElemCategory;

      for (const auto &pair : tySpec.parameters()) {
        if (pair.first == "element_category") {
          vecElemCategory =
              Fortran::evaluate::ToInt64(pair.second.GetExplicit())
                  .value_or(-1);
        } else if (pair.first == "element_kind") {
          vecElemKind =
              Fortran::evaluate::ToInt64(pair.second.GetExplicit()).value_or(0);
        }
      }

      assert((vecElemCategory >= 0 &&
              static_cast<size_t>(vecElemCategory) <
                  Fortran::common::VectorElementCategory_enumSize) &&
             "Vector element type is not specified");
      assert(vecElemKind && "Vector element kind is not specified");

      int64_t numOfElements = vectorSize / vecElemKind;
      switch (static_cast<VectorElementCategory>(vecElemCategory)) {
        SWITCH_COVERS_ALL_CASES
      case VectorElementCategory::Integer:
        return fir::VectorType::get(numOfElements,
                                    genIntegerType(context, vecElemKind));
      case VectorElementCategory::Unsigned:
        return fir::VectorType::get(numOfElements,
                                    genIntegerType(context, vecElemKind, true));
      case VectorElementCategory::Real:
        return fir::VectorType::get(numOfElements,
                                    genRealType(context, vecElemKind));
      }
      break;
    }
    case (Fortran::semantics::DerivedTypeSpec::Category::PairVector):
    case (Fortran::semantics::DerivedTypeSpec::Category::QuadVector):
      return fir::VectorType::get(vectorSize * 8,
                                  mlir::IntegerType::get(context, 1));
    case (Fortran::semantics::DerivedTypeSpec::Category::DerivedType):
      Fortran::common::die("Vector element type not implemented");
    }
  }

  mlir::Type genDerivedType(const Fortran::semantics::DerivedTypeSpec &tySpec) {
    std::vector<std::pair<std::string, mlir::Type>> ps;
    std::vector<std::pair<std::string, mlir::Type>> cs;
    if (tySpec.IsVectorType()) {
      return genVectorType(tySpec);
    }

    const Fortran::semantics::Symbol &typeSymbol = tySpec.typeSymbol();
    const Fortran::semantics::Scope &derivedScope = DEREF(tySpec.GetScope());
    if (mlir::Type ty = getTypeIfDerivedAlreadyInConstruction(derivedScope))
      return ty;

    auto rec = fir::RecordType::get(context, converter.mangleName(tySpec));
    // Maintain the stack of types for recursive references and to speed-up
    // the derived type constructions that can be expensive for derived type
    // with dozens of components/parents (modern Fortran).
    derivedTypeInConstruction.try_emplace(&derivedScope, rec);

    auto targetTriple{llvm::Triple(
        llvm::Triple::normalize(llvm::sys::getDefaultTargetTriple()))};
    // Always generate packed FIR struct type for bind(c) derived type for AIX
    if (targetTriple.getOS() == llvm::Triple::OSType::AIX &&
        tySpec.typeSymbol().attrs().test(Fortran::semantics::Attr::BIND_C) &&
        !IsIsoCType(&tySpec) && !fir::isa_builtin_cdevptr_type(rec)) {
      rec.pack(true);
    }

    // Gather the record type fields.
    // (1) The data components.
    if (converter.getLoweringOptions().getLowerToHighLevelFIR()) {
      size_t prev_offset{0};
      unsigned padCounter{0};
      // In HLFIR the parent component is the first fir.type component.
      for (const auto &componentName :
           typeSymbol.get<Fortran::semantics::DerivedTypeDetails>()
               .componentNames()) {
        auto scopeIter = derivedScope.find(componentName);
        assert(scopeIter != derivedScope.cend() &&
               "failed to find derived type component symbol");
        const Fortran::semantics::Symbol &component = scopeIter->second.get();
        mlir::Type ty = genSymbolType(component);
        if (rec.isPacked()) {
          auto compSize{component.size()};
          auto compOffset{component.offset()};

          if (prev_offset < compOffset) {
            size_t pad{compOffset - prev_offset};
            mlir::Type i8Ty{mlir::IntegerType::get(context, 8)};
            fir::SequenceType::Shape shape{static_cast<int64_t>(pad)};
            mlir::Type padTy{fir::SequenceType::get(shape, i8Ty)};
            prev_offset += pad;
            cs.emplace_back("__padding" + std::to_string(padCounter++), padTy);
          }
          prev_offset += compSize;
        }
        cs.emplace_back(converter.getRecordTypeFieldName(component), ty);
        if (rec.isPacked()) {
          // For the last component, determine if any padding is needed.
          if (componentName ==
              typeSymbol.get<Fortran::semantics::DerivedTypeDetails>()
                  .componentNames()
                  .back()) {
            auto compEnd{component.offset() + component.size()};
            if (compEnd < derivedScope.size()) {
              size_t pad{derivedScope.size() - compEnd};
              mlir::Type i8Ty{mlir::IntegerType::get(context, 8)};
              fir::SequenceType::Shape shape{static_cast<int64_t>(pad)};
              mlir::Type padTy{fir::SequenceType::get(shape, i8Ty)};
              cs.emplace_back("__padding" + std::to_string(padCounter++),
                              padTy);
            }
          }
        }
      }
    } else {
      for (const auto &component :
           Fortran::semantics::OrderedComponentIterator(tySpec)) {
        // In the lowering to FIR the parent component does not appear in the
        // fir.type and its components are inlined at the beginning of the
        // fir.type<>.
        // FIXME: this strategy leads to bugs because padding should be inserted
        // after the component of the parents so that the next components do not
        // end-up in the parent storage if the sum of the parent's component
        // storage size is not a multiple of the parent type storage alignment.

        // Lowering is assuming non deferred component lower bounds are
        // always 1. Catch any situations where this is not true for now.
        if (componentHasNonDefaultLowerBounds(component))
          TODO(converter.genLocation(component.name()),
               "derived type components with non default lower bounds");
        if (IsProcedure(component))
          TODO(converter.genLocation(component.name()), "procedure components");
        mlir::Type ty = genSymbolType(component);
        // Do not add the parent component (component of the parents are
        // added and should be sufficient, the parent component would
        // duplicate the fields). Note that genSymbolType must be called above
        // on it so that the dispatch table for the parent type still gets
        // emitted as needed.
        if (component.test(Fortran::semantics::Symbol::Flag::ParentComp))
          continue;
        cs.emplace_back(converter.getRecordTypeFieldName(component), ty);
      }
    }

    mlir::Location loc = converter.genLocation(typeSymbol.name());
    // (2) The LEN type parameters.
    for (const auto &param :
         Fortran::semantics::OrderParameterDeclarations(typeSymbol))
      if (param->get<Fortran::semantics::TypeParamDetails>().attr() ==
          Fortran::common::TypeParamAttr::Len) {
        TODO(loc, "parameterized derived types");
        // TODO: emplace in ps. Beware that param is the symbol in the type
        // declaration, not instantiation: its kind may not be a constant.
        // The instantiated symbol in tySpec.scope should be used instead.
        ps.emplace_back(param->name().ToString(), genSymbolType(*param));
      }

    rec.finalize(ps, cs);

    if (!ps.empty()) {
      // TODO: this type is a PDT (parametric derived type) with length
      // parameter. Create the functions to use for allocation, dereferencing,
      // and address arithmetic here.
    }
    LLVM_DEBUG(llvm::dbgs() << "derived type: " << rec << '\n');

    // Generate the type descriptor object if any
    if (const Fortran::semantics::Symbol *typeInfoSym =
            derivedScope.runtimeDerivedTypeDescription())
      converter.registerTypeInfo(loc, *typeInfoSym, tySpec, rec);
    return rec;
  }

  // To get the character length from a symbol, make an fold a designator for
  // the symbol to cover the case where the symbol is an assumed length named
  // constant and its length comes from its init expression length.
  template <int Kind>
  fir::SequenceType::Extent
  getCharacterLengthHelper(const Fortran::semantics::Symbol &symbol) {
    using TC =
        Fortran::evaluate::Type<Fortran::common::TypeCategory::Character, Kind>;
    auto designator = Fortran::evaluate::Fold(
        converter.getFoldingContext(),
        Fortran::evaluate::Expr<TC>{Fortran::evaluate::Designator<TC>{symbol}});
    if (auto len = toInt64(std::move(designator.LEN())))
      return *len;
    return fir::SequenceType::getUnknownExtent();
  }

  template <typename T>
  void translateLenParameters(
      llvm::SmallVectorImpl<Fortran::lower::LenParameterTy> &params,
      Fortran::common::TypeCategory category, const T &exprOrSym) {
    if (category == Fortran::common::TypeCategory::Character)
      params.push_back(getCharacterLength(exprOrSym));
    else if (category == Fortran::common::TypeCategory::Derived)
      TODO(converter.getCurrentLocation(), "derived type length parameters");
  }
  Fortran::lower::LenParameterTy
  getCharacterLength(const Fortran::semantics::Symbol &symbol) {
    const Fortran::semantics::DeclTypeSpec *type = symbol.GetType();
    if (!type ||
        type->category() != Fortran::semantics::DeclTypeSpec::Character ||
        !type->AsIntrinsic())
      llvm::report_fatal_error("not a character symbol");
    int kind =
        toInt64(Fortran::common::Clone(type->AsIntrinsic()->kind())).value();
    switch (kind) {
    case 1:
      return getCharacterLengthHelper<1>(symbol);
    case 2:
      return getCharacterLengthHelper<2>(symbol);
    case 4:
      return getCharacterLengthHelper<4>(symbol);
    }
    llvm_unreachable("unknown character kind");
  }

  template <typename A>
  Fortran::lower::LenParameterTy getCharacterLength(const A &expr) {
    return fir::SequenceType::getUnknownExtent();
  }

  template <typename T>
  Fortran::lower::LenParameterTy
  getCharacterLength(const Fortran::evaluate::FunctionRef<T> &funcRef) {
    if (auto constantLen = toInt64(funcRef.LEN()))
      return *constantLen;
    return fir::SequenceType::getUnknownExtent();
  }

  Fortran::lower::LenParameterTy
  getCharacterLength(const Fortran::lower::SomeExpr &expr) {
    // Do not use dynamic type length here. We would miss constant
    // lengths opportunities because dynamic type only has the length
    // if it comes from a declaration.
    if (const auto *charExpr = std::get_if<
            Fortran::evaluate::Expr<Fortran::evaluate::SomeCharacter>>(
            &expr.u)) {
      if (auto constantLen = toInt64(charExpr->LEN()))
        return *constantLen;
    } else if (auto dynamicType = expr.GetType()) {
      // When generating derived type type descriptor as structure constructor,
      // semantics wraps designators to data component initialization into
      // CLASS(*), regardless of their actual type.
      // GetType() will recover the actual symbol type as the dynamic type, so
      // getCharacterLength may be reached even if expr is packaged as an
      // Expr<SomeDerived> instead of an Expr<SomeChar>.
      // Just use the dynamic type here again to retrieve the length.
      if (auto constantLen = toInt64(dynamicType->GetCharLength()))
        return *constantLen;
    }
    return fir::SequenceType::getUnknownExtent();
  }

  mlir::Type genVariableType(const Fortran::lower::pft::Variable &var) {
    return genSymbolType(var.getSymbol(), var.isHeapAlloc(), var.isPointer());
  }

  /// Derived type can be recursive. That is, pointer components of a derived
  /// type `t` have type `t`. This helper returns `t` if it is already being
  /// lowered to avoid infinite loops.
  mlir::Type getTypeIfDerivedAlreadyInConstruction(
      const Fortran::semantics::Scope &derivedScope) const {
    return derivedTypeInConstruction.lookup(&derivedScope);
  }

  /// Stack derived type being processed to avoid infinite loops in case of
  /// recursive derived types. The depth of derived types is expected to be
  /// shallow (<10), so a SmallVector is sufficient.
  Fortran::lower::TypeConstructionStack &derivedTypeInConstruction;
  Fortran::lower::AbstractConverter &converter;
  mlir::MLIRContext *context;
};
} // namespace

mlir::Type Fortran::lower::getFIRType(mlir::MLIRContext *context,
                                      Fortran::common::TypeCategory tc,
                                      int kind,
                                      llvm::ArrayRef<LenParameterTy> params) {
  return genFIRType(context, tc, kind, params);
}

mlir::Type Fortran::lower::translateDerivedTypeToFIRType(
    Fortran::lower::AbstractConverter &converter,
    const Fortran::semantics::DerivedTypeSpec &tySpec) {
  return TypeBuilderImpl{converter}.genDerivedType(tySpec);
}

mlir::Type Fortran::lower::translateSomeExprToFIRType(
    Fortran::lower::AbstractConverter &converter, const SomeExpr &expr) {
  return TypeBuilderImpl{converter}.genExprType(expr);
}

mlir::Type Fortran::lower::translateSymbolToFIRType(
    Fortran::lower::AbstractConverter &converter, const SymbolRef symbol) {
  return TypeBuilderImpl{converter}.genSymbolType(symbol);
}

mlir::Type Fortran::lower::translateVariableToFIRType(
    Fortran::lower::AbstractConverter &converter,
    const Fortran::lower::pft::Variable &var) {
  return TypeBuilderImpl{converter}.genVariableType(var);
}

mlir::Type Fortran::lower::convertReal(mlir::MLIRContext *context, int kind) {
  return genRealType(context, kind);
}

bool Fortran::lower::isDerivedTypeWithLenParameters(
    const Fortran::semantics::Symbol &sym) {
  if (const Fortran::semantics::DeclTypeSpec *declTy = sym.GetType())
    if (const Fortran::semantics::DerivedTypeSpec *derived =
            declTy->AsDerived())
      return Fortran::semantics::CountLenParameters(*derived) > 0;
  return false;
}

template <typename T>
mlir::Type Fortran::lower::TypeBuilder<T>::genType(
    Fortran::lower::AbstractConverter &converter,
    const Fortran::evaluate::FunctionRef<T> &funcRef) {
  return TypeBuilderImpl{converter}.genExprType(funcRef);
}

const Fortran::semantics::DerivedTypeSpec &
Fortran::lower::ComponentReverseIterator::advanceToParentType() {
  const Fortran::semantics::Scope *scope = currentParentType->GetScope();
  auto parentComp =
      DEREF(scope).find(currentTypeDetails->GetParentComponentName().value());
  assert(parentComp != scope->cend() && "failed to get parent component");
  setCurrentType(parentComp->second->GetType()->derivedTypeSpec());
  return *currentParentType;
}

void Fortran::lower::ComponentReverseIterator::setCurrentType(
    const Fortran::semantics::DerivedTypeSpec &derived) {
  currentParentType = &derived;
  currentTypeDetails = &currentParentType->typeSymbol()
                            .get<Fortran::semantics::DerivedTypeDetails>();
  componentIt = currentTypeDetails->componentNames().crbegin();
  componentItEnd = currentTypeDetails->componentNames().crend();
}

using namespace Fortran::evaluate;
using namespace Fortran::common;
FOR_EACH_SPECIFIC_TYPE(template class Fortran::lower::TypeBuilder, )
