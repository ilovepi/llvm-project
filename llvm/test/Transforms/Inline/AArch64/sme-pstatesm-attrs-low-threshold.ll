; NOTE: Assertions have been autogenerated by utils/update_test_checks.py UTC_ARGS: --version 2
; RUN: opt < %s -mtriple=aarch64-unknown-linux-gnu -mattr=+sme -S -passes=inline  -inlinedefault-threshold=1 | FileCheck %s

; This test sets the inline-threshold to 1 such that by default the call to @streaming_callee is not inlined.
; However, if the call to @streaming_callee requires a streaming-mode change, it should always inline the call because the streaming-mode change is more expensive.
target triple = "aarch64"

declare void @streaming_compatible_f() #0 "aarch64_pstate_sm_compatible"

; Function @non_streaming_callee doesn't contain any operations that may use ZA
; state and therefore can be legally inlined into a normal function.
define void @non_streaming_callee() #0 {
; CHECK-LABEL: define void @non_streaming_callee
; CHECK-SAME: () #[[ATTR1:[0-9]+]] {
; CHECK-NEXT:    call void @streaming_compatible_f()
; CHECK-NEXT:    call void @streaming_compatible_f()
; CHECK-NEXT:    ret void
;
  call void @streaming_compatible_f()
  call void @streaming_compatible_f()
  ret void
}

; Inline call to @non_streaming_callee to remove a streaming mode change.
define void @streaming_caller_inline() #0 "aarch64_pstate_sm_enabled" {
; CHECK-LABEL: define void @streaming_caller_inline
; CHECK-SAME: () #[[ATTR2:[0-9]+]] {
; CHECK-NEXT:    call void @streaming_compatible_f()
; CHECK-NEXT:    call void @streaming_compatible_f()
; CHECK-NEXT:    ret void
;
  call void @non_streaming_callee()
  ret void
}

; Don't inline call to @non_streaming_callee when the inline-threshold is set to 1, because it does not eliminate a streaming-mode change.
define void @non_streaming_caller_dont_inline() #0 {
; CHECK-LABEL: define void @non_streaming_caller_dont_inline
; CHECK-SAME: () #[[ATTR1]] {
; CHECK-NEXT:    call void @non_streaming_callee()
; CHECK-NEXT:    ret void
;
  call void @non_streaming_callee()
  ret void
}

attributes #0 = { "target-features"="+sve,+sme" }
