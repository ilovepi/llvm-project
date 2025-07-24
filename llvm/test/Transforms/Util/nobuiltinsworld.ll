; RUN: opt -passes=no-builtins-world -S < %s | FileCheck %s

define i32 @f() {
 ret i32 0
}

; CHECK-LABEL: @f()
; CHECK: { "no-builtins" }
