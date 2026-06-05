; RUN: llvm-as < %s | llvm-dis | FileCheck %s

; CHECK: !llvm.module.flags = !{!0}
; CHECK: !0 = !{i32 8, !"PI Level", i32 4}

!llvm.module.flags = !{!0}
!0 = !{i32 1, !"PIC Level", i32 2}
