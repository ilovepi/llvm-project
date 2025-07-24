;; This test comes from a real world scenario in LTO, where the
;; definition of bcmp is deleted because it has no uses, but later instcombine
;; re-introduces a call to bcmp() as part of SimplifyLibCalls.

; RUN: opt < %s -S -passes=instcombine -mtriple x86_64-unknown-linux-musl | FileCheck %s --check-prefix=BUILTINS_WORLD
; RUN: opt < %s -S -passes="no-builtins-world,instcombine" -mtriple x86_64-unknown-linux-musl | FileCheck %s --check-prefix=NO_BUILTINS_WORLD

define i1 @foo(ptr %0, [2 x i64] %1) {
  ; CHECK-LABEL: define{{.*}}i1 @foo
  ; CHECK-NEXT: %size = extractvalue [2 x i64] %1, 1
  ; BUILTINS_WORLD: %bcmp = {{.*}}call i32 @bcmp
  ; BUILTINS_WORLD: %eq = icmp eq i32 %bcmp, 0
  ; NO_BUILTINS_WORLD: %cmp = {{.*}}call i32 @memcmp
  ; NO_BUILTINS_WORLD: %eq = icmp eq i32 %cmp, 0
  ; CHECK-NEXT: ret i1 %eq

  %size = extractvalue [2 x i64] %1, 1
  %cmp = call i32 @memcmp(ptr %0, ptr null, i64 %size)
  %eq = icmp eq i32 %cmp, 0
  ret i1 %eq
}

; CHECK: declare i32 @memcmp(ptr, ptr, i64)
declare i32 @memcmp(ptr, ptr, i64)

; CHECK: define internal i32 @bcmp(ptr %0, ptr %1, i64 %2) {
define internal i32 @bcmp(ptr %0, ptr %1, i64 %2) {
  ret i32 0
}
