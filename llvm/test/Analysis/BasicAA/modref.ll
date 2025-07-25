; NOTE: Assertions have been autogenerated by utils/update_test_checks.py
; RUN: opt < %s -aa-pipeline=basic-aa -passes=gvn,dse -S | FileCheck %s
target datalayout = "E-p:64:64:64-a0:0:8-f32:32:32-f64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:32:64-v64:64:64-v128:128:128"

declare void @llvm.lifetime.end.p0(i64, ptr nocapture)

declare void @external(ptr)

define i32 @test0(ptr %P) {
; CHECK-LABEL: @test0(
; CHECK-NEXT:    [[A:%.*]] = alloca i32, align 4
; CHECK-NEXT:    call void @external(ptr [[A]])
; CHECK-NEXT:    call void @llvm.memset.p0.i32(ptr [[P:%.*]], i8 0, i32 42, i1 false)
; CHECK-NEXT:    ret i32 0
;
  %A = alloca i32
  call void @external(ptr %A)

  store i32 0, ptr %A

  call void @llvm.memset.p0.i32(ptr %P, i8 0, i32 42, i1 false)

  %B = load i32, ptr %A
  ret i32 %B
}

define i8 @test1() {
; CHECK-LABEL: @test1(
; CHECK-NEXT:    ret i8 2
;
  %A = alloca i8
  %B = alloca i8

  store i8 2, ptr %B  ;; Not written to by memcpy

  call void @llvm.memcpy.p0.p0.i8(ptr %A, ptr %B, i8 -1, i1 false)

  %C = load i8, ptr %B
  ret i8 %C
}

define i8 @test2(ptr %P) {
; CHECK-LABEL: @test2(
; CHECK-NEXT:    [[P2:%.*]] = getelementptr i8, ptr [[P:%.*]], i32 127
; CHECK-NEXT:    store i8 1, ptr [[P2]], align 1
; CHECK-NEXT:    call void @llvm.memset.p0.i8(ptr [[P]], i8 2, i8 127, i1 false)
; CHECK-NEXT:    ret i8 1
;
  %P2 = getelementptr i8, ptr %P, i32 127
  store i8 1, ptr %P2  ;; Not dead across memset
  call void @llvm.memset.p0.i8(ptr %P, i8 2, i8 127, i1 false)
  %A = load i8, ptr %P2
  ret i8 %A
}

define i8 @test2a(ptr %P) {
; CHECK-LABEL: @test2a(
; CHECK-NEXT:    call void @llvm.memset.p0.i8(ptr [[P:%.*]], i8 2, i8 127, i1 false)
; CHECK-NEXT:    ret i8 2
;
  %P2 = getelementptr i8, ptr %P, i32 126

  store i8 1, ptr %P2  ;; Dead, clobbered by memset.

  call void @llvm.memset.p0.i8(ptr %P, i8 2, i8 127, i1 false)
  %A = load i8, ptr %P2
  ret i8 %A
}

define void @test3(i8 %X) {
; CHECK-LABEL: @test3(
; CHECK-NEXT:    [[P:%.*]] = alloca i64, align 8
; CHECK-NEXT:    [[P2:%.*]] = getelementptr i8, ptr [[P]], i32 2
; CHECK-NEXT:    call void @llvm.lifetime.end.p0(i64 1, ptr [[P]])
; CHECK-NEXT:    store i8 2, ptr [[P2]], align 1
; CHECK-NEXT:    call void @external(ptr [[P]])
; CHECK-NEXT:    ret void
;
  %P = alloca i64
  %Y = add i8 %X, 1     ;; Dead, because the only use (the store) is dead.

  %P2 = getelementptr i8, ptr %P, i32 2
  store i8 %Y, ptr %P2  ;; Not read by lifetime.end, should be removed.
  call void @llvm.lifetime.end.p0(i64 1, ptr %P)
  store i8 2, ptr %P2
  call void @external(ptr %P)
  ret void
}

define void @test3a(i8 %X) {
; CHECK-LABEL: @test3a(
; CHECK-NEXT:    [[P:%.*]] = alloca i64, align 8
; CHECK-NEXT:    call void @llvm.lifetime.end.p0(i64 10, ptr [[P]])
; CHECK-NEXT:    ret void
;
  %P = alloca i64
  %Y = add i8 %X, 1     ;; Dead, because the only use (the store) is dead.

  %P2 = getelementptr i8, ptr %P, i32 2
  store i8 %Y, ptr %P2
  call void @llvm.lifetime.end.p0(i64 10, ptr %P)
  ret void
}

@G1 = external global i32
@G2 = external global [4000 x i32]

define i32 @test4(ptr %P) {
; CHECK-LABEL: @test4(
; CHECK-NEXT:    call void @llvm.memset.p0.i32(ptr @G2, i8 0, i32 4000, i1 false)
; CHECK-NEXT:    ret i32 0
;
  %tmp = load i32, ptr @G1
  call void @llvm.memset.p0.i32(ptr @G2, i8 0, i32 4000, i1 false)
  %tmp2 = load i32, ptr @G1
  %sub = sub i32 %tmp2, %tmp
  ret i32 %sub
}

; Verify that basicaa is handling variable length memcpy, knowing it doesn't
; write to G1.
define i32 @test5(ptr %P, i32 %Len) {
; CHECK-LABEL: @test5(
; CHECK-NEXT:    call void @llvm.memcpy.p0.p0.i32(ptr @G2, ptr @G1, i32 [[LEN:%.*]], i1 false)
; CHECK-NEXT:    ret i32 0
;
  %tmp = load i32, ptr @G1
  call void @llvm.memcpy.p0.p0.i32(ptr @G2, ptr @G1, i32 %Len, i1 false)
  %tmp2 = load i32, ptr @G1
  %sub = sub i32 %tmp2, %tmp
  ret i32 %sub
}

define i8 @test6(ptr %p, ptr noalias %a) {
; CHECK-LABEL: @test6(
; CHECK-NEXT:    [[X:%.*]] = load i8, ptr [[A:%.*]], align 1
; CHECK-NEXT:    [[T:%.*]] = va_arg ptr [[P:%.*]], float
; CHECK-NEXT:    [[Z:%.*]] = add i8 [[X]], [[X]]
; CHECK-NEXT:    ret i8 [[Z]]
;
  %x = load i8, ptr %a
  %t = va_arg ptr %p, float
  %y = load i8, ptr %a
  %z = add i8 %x, %y
  ret i8 %z
}

; PR10628
declare void @test7decl(ptr nocapture %x)
define i32 @test7() nounwind uwtable ssp {
; CHECK-LABEL: @test7(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    [[X:%.*]] = alloca i32, align 4
; CHECK-NEXT:    store i32 0, ptr [[X]], align 4
; CHECK-NEXT:    [[ADD_PTR:%.*]] = getelementptr inbounds i32, ptr [[X]], i64 1
; CHECK-NEXT:    call void @test7decl(ptr [[ADD_PTR]])
; CHECK-NEXT:    [[TMP:%.*]] = load i32, ptr [[X]], align 4
; CHECK-NEXT:    ret i32 [[TMP]]
;
entry:
  %x = alloca i32, align 4
  store i32 0, ptr %x, align 4
  %add.ptr = getelementptr inbounds i32, ptr %x, i64 1
  call void @test7decl(ptr %add.ptr)
  %tmp = load i32, ptr %x, align 4
  ret i32 %tmp
}

;; Check that aa correctly handles functions marked with argmemonly
;; attribute.
declare i32 @func_argmemonly(ptr %P) argmemonly

;; Can not remove redundant load, function may write to it.

define i32 @test8(ptr %P) {
; CHECK-LABEL: @test8(
; CHECK-NEXT:    [[V1:%.*]] = load i32, ptr [[P:%.*]], align 4
; CHECK-NEXT:    [[TMP1:%.*]] = call i32 @func_argmemonly(ptr [[P]])
; CHECK-NEXT:    [[V2:%.*]] = load i32, ptr [[P]], align 4
; CHECK-NEXT:    [[DIFF:%.*]] = sub i32 [[V1]], [[V2]]
; CHECK-NEXT:    ret i32 [[DIFF]]
;
  %V1 = load i32, ptr %P
  call i32 @func_argmemonly(ptr %P)
  %V2 = load i32, ptr %P
  %Diff = sub i32 %V1, %V2
  ret i32 %Diff
}

;; In this case load can be removed, function clobbers only %P2.
define i32 @test9(ptr %P, ptr noalias %P2) {
; CHECK-LABEL: @test9(
; CHECK-NEXT:    [[TMP1:%.*]] = call i32 @func_argmemonly(ptr [[P2:%.*]])
; CHECK-NEXT:    ret i32 0
;
  %V1 = load i32, ptr %P
  call i32 @func_argmemonly(ptr %P2)
  %V2 = load i32, ptr %P
  %Diff = sub i32 %V1, %V2
  ret i32 %Diff
}

;; In this case load can *not* be removed. Function clobers only %P2 but it may
;; alias with %P.
define i32 @test10(ptr %P, ptr %P2) {
; CHECK-LABEL: @test10(
; CHECK-NEXT:    [[V1:%.*]] = load i32, ptr [[P:%.*]], align 4
; CHECK-NEXT:    [[TMP1:%.*]] = call i32 @func_argmemonly(ptr [[P2:%.*]])
; CHECK-NEXT:    [[V2:%.*]] = load i32, ptr [[P]], align 4
; CHECK-NEXT:    [[DIFF:%.*]] = sub i32 [[V1]], [[V2]]
; CHECK-NEXT:    ret i32 [[DIFF]]
;
  %V1 = load i32, ptr %P
  call i32 @func_argmemonly(ptr %P2)
  %V2 = load i32, ptr %P
  %Diff = sub i32 %V1, %V2
  ret i32 %Diff
}

define i32 @test11(ptr %P, ptr %P2) {
; CHECK-LABEL: @test11(
; CHECK-NEXT:    [[TMP1:%.*]] = call i32 @func_argmemonly(ptr readonly [[P2:%.*]])
; CHECK-NEXT:    ret i32 0
;
  %V1 = load i32, ptr %P
  call i32 @func_argmemonly(ptr readonly %P2)
  %V2 = load i32, ptr %P
  %Diff = sub i32 %V1, %V2
  ret i32 %Diff

}

declare i32 @func_argmemonly_two_args(ptr %P, ptr %P2) argmemonly

define i32 @test12(ptr %P, ptr %P2, ptr %P3) {
; CHECK-LABEL: @test12(
; CHECK-NEXT:    [[V1:%.*]] = load i32, ptr [[P:%.*]], align 4
; CHECK-NEXT:    [[TMP1:%.*]] = call i32 @func_argmemonly_two_args(ptr readonly [[P2:%.*]], ptr [[P3:%.*]])
; CHECK-NEXT:    [[V2:%.*]] = load i32, ptr [[P]], align 4
; CHECK-NEXT:    [[DIFF:%.*]] = sub i32 [[V1]], [[V2]]
; CHECK-NEXT:    ret i32 [[DIFF]]
;
  %V1 = load i32, ptr %P
  call i32 @func_argmemonly_two_args(ptr readonly %P2, ptr %P3)
  %V2 = load i32, ptr %P
  %Diff = sub i32 %V1, %V2
  ret i32 %Diff
}

define i32 @test13(ptr %P, ptr %P2) {
; CHECK-LABEL: @test13(
; CHECK-NEXT:    [[TMP1:%.*]] = call i32 @func_argmemonly(ptr readnone [[P2:%.*]])
; CHECK-NEXT:    ret i32 0
;
  %V1 = load i32, ptr %P
  call i32 @func_argmemonly(ptr readnone %P2)
  %V2 = load i32, ptr %P
  %Diff = sub i32 %V1, %V2
  ret i32 %Diff
}

declare void @llvm.memset.p0.i32(ptr nocapture, i8, i32, i1) nounwind
declare void @llvm.memset.p0.i8(ptr nocapture, i8, i8, i1) nounwind
declare void @llvm.memcpy.p0.p0.i8(ptr nocapture, ptr nocapture, i8, i1) nounwind
declare void @llvm.memcpy.p0.p0.i32(ptr nocapture, ptr nocapture, i32, i1) nounwind
