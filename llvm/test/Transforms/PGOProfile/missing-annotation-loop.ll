; Test missing-annotations remarks for branches in and around loops.

; RUN: rm -rf %t && split-file %s %t
; RUN: llvm-profdata merge %t/a.proftext -o %t/a.profdata
; RUN: opt < %t/a.ll -passes=pgo-instr-use -pgo-test-profile-file=%t/a.profdata \
; RUN:   -pgo-missing-annotations -pass-remarks=missing-annotations -S 2>&1 | \
; RUN:   FileCheck %s --implicit-check-not=remark:

; FIXME: The loop conditions on lines 3 and 10 shouldn't be reported.
; CHECK: remark: loop.c:3:0: Extremely hot condition. Consider adding llvm.expect intrinsic
; CHECK: remark: loop.c:4:0: Extremely hot condition. Consider adding llvm.expect intrinsic
; CHECK: remark: loop.c:10:0: Extremely hot condition. Consider adding llvm.expect intrinsic

;--- a.proftext
:ir
for_loop
1124680652115249575
3
20000
0
1

do_while
146835643953959699
2
20000
1

;--- a.ll
target datalayout = "e-m:e-i64:64-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @for_loop(i32 %n) !dbg !4 {
entry:
  br label %for.cond

for.cond:
  %i = phi i32 [ 0, %entry ], [ %inc, %for.inc ]
  %cmp = icmp slt i32 %i, %n, !dbg !7
  br i1 %cmp, label %for.body, label %for.end, !dbg !7

for.body:
  %c = call i32 @cond(i32 %i), !dbg !8
  %tobool = icmp ne i32 %c, 0, !dbg !8
  br i1 %tobool, label %if.then, label %for.inc, !dbg !8

if.then:
  call void @rare(), !dbg !8
  br label %for.inc

for.inc:
  %inc = add nsw i32 %i, 1
  br label %for.cond

for.end:
  ret i32 %i
}

define void @do_while() !dbg !9 {
entry:
  br label %do.body

do.body:
  call void @work(), !dbg !10
  br label %do.cond

do.cond:
  %c = call i32 @cond(i32 0), !dbg !10
  %tobool = icmp ne i32 %c, 0, !dbg !10
  br i1 %tobool, label %do.body, label %do.end, !dbg !10

do.end:
  ret void
}

declare i32 @cond(i32)
declare void @rare()
declare void @work()

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!3}

!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !1, isOptimized: true, runtimeVersion: 0, emissionKind: LineTablesOnly, enums: !2)
!1 = !DIFile(filename: "loop.c", directory: ".")
!2 = !{}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = distinct !DISubprogram(name: "for_loop", scope: !1, file: !1, line: 1, type: !5, scopeLine: 1, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!5 = !DISubroutineType(types: !2)
!7 = !DILocation(line: 3, scope: !4)
!8 = !DILocation(line: 4, scope: !4)
!9 = distinct !DISubprogram(name: "do_while", scope: !1, file: !1, line: 8, type: !5, scopeLine: 8, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !2)
!10 = !DILocation(line: 10, scope: !9)
