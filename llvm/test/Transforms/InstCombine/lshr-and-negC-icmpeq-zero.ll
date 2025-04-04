; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt < %s -passes=instcombine -S | FileCheck %s

; For pattern ((X l>> Y) & ~C) ==/!= 0; when C+1 is power of 2
; it may be optimal to fold into (X l>> Y) </>= C+1
; rather than X & (~C << Y) ==/!= 0

; Scalar tests

define i1 @scalar_i8_lshr_and_negC_eq(i8 %x, i8 %y) {
; CHECK-LABEL: @scalar_i8_lshr_and_negC_eq(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i8 [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp ult i8 [[LSHR]], 4
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i8 %x, %y
  %and = and i8 %lshr, 252  ; ~3
  %r = icmp eq i8 %and, 0
  ret i1 %r
}

define i1 @scalar_i16_lshr_and_negC_eq(i16 %x, i16 %y) {
; CHECK-LABEL: @scalar_i16_lshr_and_negC_eq(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i16 [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp ult i16 [[LSHR]], 128
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i16 %x, %y
  %and = and i16 %lshr, 65408  ; ~127
  %r = icmp eq i16 %and, 0
  ret i1 %r
}

define i1 @scalar_i32_lshr_and_negC_eq(i32 %x, i32 %y) {
; CHECK-LABEL: @scalar_i32_lshr_and_negC_eq(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i32 [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp ult i32 [[LSHR]], 262144
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i32 %x, %y
  %and = and i32 %lshr, 4294705152  ; ~262143
  %r = icmp eq i32 %and, 0
  ret i1 %r
}

define i1 @scalar_i64_lshr_and_negC_eq(i64 %x, i64 %y) {
; CHECK-LABEL: @scalar_i64_lshr_and_negC_eq(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i64 [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp ult i64 [[LSHR]], 8589934592
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i64 %x, %y
  %and = and i64 %lshr, 18446744065119617024  ; ~8589934591
  %r = icmp eq i64 %and, 0
  ret i1 %r
}

define i1 @scalar_i32_lshr_and_negC_ne(i32 %x, i32 %y) {
; CHECK-LABEL: @scalar_i32_lshr_and_negC_ne(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i32 [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp ugt i32 [[LSHR]], 262143
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i32 %x, %y
  %and = and i32 %lshr, 4294705152  ; ~262143
  %r = icmp ne i32 %and, 0   ; check 'ne' predicate
  ret i1 %r
}

; Vector tests

define <4 x i1> @vec_4xi32_lshr_and_negC_eq(<4 x i32> %x, <4 x i32> %y) {
; CHECK-LABEL: @vec_4xi32_lshr_and_negC_eq(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr <4 x i32> [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp ult <4 x i32> [[LSHR]], splat (i32 8)
; CHECK-NEXT:    ret <4 x i1> [[R]]
;
  %lshr = lshr <4 x i32> %x, %y
  %and = and <4 x i32> %lshr, <i32 4294967288, i32 4294967288, i32 4294967288, i32 4294967288>  ; ~7
  %r = icmp eq <4 x i32> %and, <i32 0, i32 0, i32 0, i32 0>
  ret <4 x i1> %r
}

define <4 x i1> @vec_lshr_and_negC_eq_poison1(<4 x i32> %x, <4 x i32> %y) {
; CHECK-LABEL: @vec_lshr_and_negC_eq_poison1(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr <4 x i32> [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp ult <4 x i32> [[LSHR]], splat (i32 8)
; CHECK-NEXT:    ret <4 x i1> [[R]]
;
  %lshr = lshr <4 x i32> %x, %y
  %and = and <4 x i32> %lshr, <i32 4294967288, i32 poison, i32 4294967288, i32 4294967288>  ; ~7
  %r = icmp eq <4 x i32> %and, <i32 0, i32 0, i32 0, i32 0>
  ret <4 x i1> %r
}

define <4 x i1> @vec_lshr_and_negC_eq_poison2(<4 x i32> %x, <4 x i32> %y) {
; CHECK-LABEL: @vec_lshr_and_negC_eq_poison2(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr <4 x i32> [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp ult <4 x i32> [[LSHR]], splat (i32 8)
; CHECK-NEXT:    ret <4 x i1> [[R]]
;
  %lshr = lshr <4 x i32> %x, %y
  %and = and <4 x i32> %lshr, <i32 4294967288, i32 4294967288, i32 4294967288, i32 4294967288>  ; ~7
  %r = icmp eq <4 x i32> %and, <i32 0, i32 0, i32 0, i32 poison>
  ret <4 x i1> %r
}

define <4 x i1> @vec_lshr_and_negC_eq_poison3(<4 x i32> %x, <4 x i32> %y) {
; CHECK-LABEL: @vec_lshr_and_negC_eq_poison3(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr <4 x i32> [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp ult <4 x i32> [[LSHR]], splat (i32 8)
; CHECK-NEXT:    ret <4 x i1> [[R]]
;
  %lshr = lshr <4 x i32> %x, %y
  %and = and <4 x i32> %lshr, <i32 4294967288, i32 4294967288, i32 poison, i32 4294967288>  ; ~7
  %r = icmp eq <4 x i32> %and, <i32 0, i32 0, i32 0, i32 poison>
  ret <4 x i1> %r
}

; Extra use

; Fold happened
define i1 @scalar_lshr_and_negC_eq_extra_use_lshr(i32 %x, i32 %y, i32 %z, ptr %p) {
; CHECK-LABEL: @scalar_lshr_and_negC_eq_extra_use_lshr(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i32 [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[XOR:%.*]] = xor i32 [[LSHR]], [[Z:%.*]]
; CHECK-NEXT:    store i32 [[XOR]], ptr [[P:%.*]], align 4
; CHECK-NEXT:    [[R:%.*]] = icmp ult i32 [[LSHR]], 8
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i32 %x, %y
  %xor = xor i32 %lshr, %z  ; extra use of lshr
  store i32 %xor, ptr %p
  %and = and i32 %lshr, 4294967288  ; ~7
  %r = icmp eq i32 %and, 0
  ret i1 %r
}

; Not fold
define i1 @scalar_lshr_and_negC_eq_extra_use_and(i32 %x, i32 %y, i32 %z, ptr %p) {
; CHECK-LABEL: @scalar_lshr_and_negC_eq_extra_use_and(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i32 [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[AND:%.*]] = and i32 [[LSHR]], -8
; CHECK-NEXT:    [[MUL:%.*]] = mul i32 [[AND]], [[Z:%.*]]
; CHECK-NEXT:    store i32 [[MUL]], ptr [[P:%.*]], align 4
; CHECK-NEXT:    [[R:%.*]] = icmp eq i32 [[AND]], 0
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i32 %x, %y
  %and = and i32 %lshr, 4294967288  ; ~7
  %mul = mul i32 %and, %z  ; extra use of and
  store i32 %mul, ptr %p
  %r = icmp eq i32 %and, 0
  ret i1 %r
}

; Not fold
define i1 @scalar_lshr_and_negC_eq_extra_use_lshr_and(i32 %x, i32 %y, i32 %z, ptr %p, ptr %q) {
; CHECK-LABEL: @scalar_lshr_and_negC_eq_extra_use_lshr_and(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i32 [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[AND:%.*]] = and i32 [[LSHR]], -8
; CHECK-NEXT:    store i32 [[AND]], ptr [[P:%.*]], align 4
; CHECK-NEXT:    [[ADD:%.*]] = add i32 [[LSHR]], [[Z:%.*]]
; CHECK-NEXT:    store i32 [[ADD]], ptr [[Q:%.*]], align 4
; CHECK-NEXT:    [[R:%.*]] = icmp eq i32 [[AND]], 0
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i32 %x, %y
  %and = and i32 %lshr, 4294967288  ; ~7
  store i32 %and, ptr %p  ; extra use of and
  %add = add i32 %lshr, %z  ; extra use of lshr
  store i32 %add, ptr %q
  %r = icmp eq i32 %and, 0
  ret i1 %r
}

define i1 @scalar_i32_lshr_and_negC_eq_X_is_constant1(i32 %y) {
; CHECK-LABEL: @scalar_i32_lshr_and_negC_eq_X_is_constant1(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i32 12345, [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp samesign ult i32 [[LSHR]], 8
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i32 12345, %y
  %and = and i32 %lshr, 4294967288  ; ~7
  %r = icmp eq i32 %and, 0
  ret i1 %r
}

define i1 @scalar_i32_lshr_and_negC_eq_X_is_constant2(i32 %y) {
; CHECK-LABEL: @scalar_i32_lshr_and_negC_eq_X_is_constant2(
; CHECK-NEXT:    [[R:%.*]] = icmp ugt i32 [[Y:%.*]], 25
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i32 268435456, %y
  %and = and i32 %lshr, 4294967288  ; ~7
  %r = icmp eq i32 %and, 0
  ret i1 %r
}
define i1 @scalar_i32_udiv_and_negC_eq_X_is_constant3(i32 %y) {
; CHECK-LABEL: @scalar_i32_udiv_and_negC_eq_X_is_constant3(
; CHECK-NEXT:    [[R:%.*]] = icmp ult i32 [[Y:%.*]], 1544
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = udiv  i32 12345, %y
  %and = and i32 %lshr, 16376    ; 0x3ff8
  %r = icmp ne i32 %and, 0
  ret i1 %r
}

; Negative test

define i1 @scalar_i32_lshr_and_negC_eq_X_is_constant_negative(i32 %y) {
; CHECK-LABEL: @scalar_i32_lshr_and_negC_eq_X_is_constant_negative(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i32 16384, [[Y:%.*]]
; CHECK-NEXT:    [[AND:%.*]] = and i32 [[LSHR]], 16376
; CHECK-NEXT:    [[R:%.*]] = icmp eq i32 [[AND]], 0
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i32 16384, %y    ; 0x4000
  %and = and i32 %lshr, 16376   ; 0x3ff8
  %r = icmp eq i32 %and, 0
  ret i1 %r
}
; Check 'slt' predicate

define i1 @scalar_i32_lshr_and_negC_slt(i32 %x, i32 %y) {
; CHECK-LABEL: @scalar_i32_lshr_and_negC_slt(
; CHECK-NEXT:    [[LSHR:%.*]] = lshr i32 [[X:%.*]], [[Y:%.*]]
; CHECK-NEXT:    [[R:%.*]] = icmp slt i32 [[LSHR]], 0
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i32 %x, %y
  %and = and i32 %lshr, 4294967288  ; ~7
  %r = icmp slt i32 %and, 0
  ret i1 %r
}

; Compare with nonzero

define i1 @scalar_i32_lshr_and_negC_eq_nonzero(i32 %x, i32 %y) {
; CHECK-LABEL: @scalar_i32_lshr_and_negC_eq_nonzero(
; CHECK-NEXT:    ret i1 false
;
  %lshr = lshr i32 %x, %y
  %and = and i32 %lshr, 4294967288  ; ~7
  %r = icmp eq i32 %and, 1  ; should be comparing with 0
  ret i1 %r
}

; Not NegatedPowerOf2

define i1 @scalar_i8_lshr_and_negC_eq_not_negatedPowerOf2(i8 %x, i8 %y) {
; CHECK-LABEL: @scalar_i8_lshr_and_negC_eq_not_negatedPowerOf2(
; CHECK-NEXT:    [[TMP1:%.*]] = shl i8 -3, [[Y:%.*]]
; CHECK-NEXT:    [[TMP2:%.*]] = and i8 [[X:%.*]], [[TMP1]]
; CHECK-NEXT:    [[R:%.*]] = icmp eq i8 [[TMP2]], 0
; CHECK-NEXT:    ret i1 [[R]]
;
  %lshr = lshr i8 %x, %y
  %and = and i8 %lshr, 253  ; -3
  %r = icmp eq i8 %and, 0
  ret i1 %r
}
