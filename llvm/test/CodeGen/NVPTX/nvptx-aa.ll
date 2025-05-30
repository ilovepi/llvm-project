; RUN: opt -passes=aa-eval -aa-pipeline=nvptx-aa -print-all-alias-modref-info < %s -S 2>&1 \
; RUN:   | FileCheck %s --check-prefixes CHECK-ALIAS
;
; RUN: opt -aa-pipeline=nvptx-aa -passes=licm < %s -S | FileCheck %s --check-prefixes CHECK-AA-CONST
; RUN: opt -aa-pipeline=basic-aa -passes=licm < %s -S | FileCheck %s --check-prefixes CHECK-NOAA-CONST

target datalayout = "e-i64:64-v16:16-v32:32-n16:32:64"
target triple = "nvptx64-nvidia-cuda"

; CHECK-ALIAS-LABEL: Function: test
; CHECK-ALIAS: MayAlias: i8* %gen, i8 addrspace(1)* %global
; CHECK-ALIAS: MayAlias: i8* %gen, i8 addrspace(3)* %shared
; CHECK-ALIAS: NoAlias:  i8 addrspace(1)* %global, i8 addrspace(3)* %shared
; CHECK-ALIAS: MayAlias: i8 addrspace(4)* %const, i8* %gen
; CHECK-ALIAS: NoAlias:  i8 addrspace(4)* %const, i8 addrspace(1)* %global
; CHECK-ALIAS: NoAlias:  i8 addrspace(4)* %const, i8 addrspace(3)* %shared
; CHECK-ALIAS: MayAlias: i8* %gen, i8 addrspace(5)* %local
; CHECK-ALIAS: NoAlias:  i8 addrspace(1)* %global, i8 addrspace(5)* %local
; CHECK-ALIAS: NoAlias:  i8 addrspace(5)* %local, i8 addrspace(3)* %shared
; CHECK-ALIAS: NoAlias:  i8 addrspace(4)* %const, i8 addrspace(5)* %local
; CHECK-ALIAS: MayAlias:     i8* %gen, i8 addrspace(7)* %shared_cluster
; CHECK-ALIAS: NoAlias:      i8 addrspace(1)* %global, i8 addrspace(7)* %shared_cluster
; CHECK-ALIAS: MayAlias:     i8 addrspace(3)* %shared, i8 addrspace(7)* %shared_cluster
; CHECK-ALIAS: NoAlias:      i8 addrspace(4)* %const, i8 addrspace(7)* %shared_cluster
; CHECK-ALIAS: NoAlias:      i8 addrspace(5)* %local, i8 addrspace(7)* %shared_cluster
; CHECK-ALIAS: MayAlias: i8* %gen, i8 addrspace(101)* %param
; CHECK-ALIAS: NoAlias:  i8 addrspace(1)* %global, i8 addrspace(101)* %param
; CHECK-ALIAS: NoAlias:  i8 addrspace(101)* %param, i8 addrspace(3)* %shared
; CHECK-ALIAS: NoAlias:  i8 addrspace(4)* %const, i8 addrspace(101)* %param
; CHECK-ALIAS: NoAlias:  i8 addrspace(5)* %local, i8 addrspace(101)* %param
; CHECK-ALIAS: NoAlias:  i8 addrspace(101)* %param, i8 addrspace(7)* %shared_cluster

define i8 @test_alias(ptr %gen, ptr addrspace(1) %global, ptr addrspace(3) %shared, ptr addrspace(4) %const, ptr addrspace(5) %local, ptr addrspace(7) %shared_cluster) {
  %param = addrspacecast ptr %gen to ptr addrspace(101)
  %v1 = load i8, ptr %gen
  %v2 = load i8, ptr addrspace(1) %global
  %v3 = load i8, ptr addrspace(3) %shared
  %v4 = load i8, ptr addrspace(4) %const
  %v5 = load i8, ptr addrspace(5) %local
  %v6 = load i8, ptr addrspace(7) %shared_cluster
  %v7 = load i8, ptr addrspace(101) %param
  %res1 = add i8 %v1, %v2
  %res2 = add i8 %res1, %v3
  %res3 = add i8 %res2, %v4
  %res4 = add i8 %res3, %v5
  %res5 = add i8 %res4, %v6
  %res6 = add i8 %res4, %v7
  ret i8 %res5
}

; CHECK-ALIAS-LABEL: Function: test_const
; CHECK-ALIAS: MayAlias: i8* %gen, i8 addrspace(1)* %global
; CHECK-ALIAS: NoAlias:  i8 addrspace(4)* %const, i8 addrspace(1)* %global
; CHECK-ALIAS: MayAlias: i8 addrspace(4)* %const, i8* %gen
;
define i8 @test_const(ptr %gen, ptr addrspace(1) %global, ptr addrspace(4) %const) {
;
; Even though %gen and %const may alias and there is a store to %gen,
; LICM should be able to hoist %load_const because it is known to be
; constant (AA::pointsToConstantMemory()).
;
; CHECK-AA-CONST-LABEL: @test_const
; CHECK-AA-CONST-LABEL: entry
; CHECK-AA-CONST: %[[LOAD_CONST:.+]] = load i8, ptr addrspace(4)
; CHECK-AA-CONST-LABEL: loop
; CHECK-AA-CONST: add {{.*}}%[[LOAD_CONST]]
;
; Without NVPTX AA the load is left in the loop because we assume that
; it may be clobbered by the store.
;
; CHECK-NOAA-CONST-LABEL: @test_const
; CHECK-NOAA-CONST-LABEL: loop
; CHECK-NOAA-CONST: %[[LOAD_CONST:.+]] = load i8, ptr addrspace(4)
; CHECK-NOAA-CONST: add {{.*}}%[[LOAD_CONST]]
entry:
  br label %loop
loop:
  %v = phi i8 [0, %entry], [%v2, %loop]
  %load_global = load i8, ptr addrspace(1) %global
  store i8 %load_global, ptr %gen
  %load_const = load i8, ptr addrspace(4) %const
  %v2 = add i8 %v, %load_const
  %cond = icmp eq i8 %load_const, 0
  br i1 %cond, label %done, label %loop
done:
  ret i8 %v2
}

; Same as @test_const above, but for param space.
;
; CHECK-ALIAS-LABEL: Function: test_param
; CHECK-ALIAS: MayAlias: i8* %gen, i8 addrspace(1)* %global
; CHECK-ALIAS: NoAlias:  i8 addrspace(1)* %global, i8 addrspace(101)* %param
; CHECK-ALIAS: MayAlias: i8* %gen, i8 addrspace(101)* %param
;
define i8 @test_param(ptr %gen, ptr addrspace(1) %global, ptr %param_gen) {
;
; CHECK-AA-CONST-LABEL: @test_param
; CHECK-AA-CONST-LABEL: entry
; CHECK-AA-CONST: %[[LOAD_PARAM:.+]] = load i8, ptr addrspace(101)
; CHECK-AA-CONST-LABEL: loop
; CHECK-AA-CONST: add {{.*}}%[[LOAD_PARAM]]
;
; CHECK-NOAA-CONST-LABEL: @test_param
; CHECK-NOAA-CONST-LABEL: loop
; CHECK-NOAA-CONST: %[[LOAD_PARAM:.+]] = load i8, ptr addrspace(101)
; CHECK-NOAA-CONST: add {{.*}}%[[LOAD_PARAM]]
entry:
  %param = addrspacecast ptr %param_gen to ptr addrspace(101)
  br label %loop
loop:
  %v = phi i8 [0, %entry], [%v2, %loop]
  %load_global = load i8, ptr addrspace(1) %global
  store i8 %load_global, ptr %gen
  %load_const = load i8, ptr addrspace(101) %param
  %v2 = add i8 %v, %load_const
  %cond = icmp eq i8 %load_const, 0
  br i1 %cond, label %done, label %loop
done:
  ret i8 %v2
}

;; Address space information may be encoded anywhere along the UD chain.
;; We define a set of tests that:
;;  1. Perform some number of address space casts on pointer A and B
;;  2. Store a value to address A
;;  3. Store a value to address B (that we know does not alias with A)

;; generic->space
; CHECK-ALIAS-LABEL: Function: test_traversal_gen_space
; CHECK-ALIAS: NoAlias: i32 addrspace(1)* %global, i32 addrspace(5)* %local
define void @test_traversal_gen_space(ptr %gen, ptr addrspace(1) %global) {
  %local = addrspacecast ptr %gen to ptr addrspace(5)
  store i32 1, ptr addrspace(5) %local, align 8
  store i32 5, ptr addrspace(1) %global, align 8
  ret void
}

;; space->generic
; CHECK-ALIAS-LABEL: Function: test_traversal_space_gen
; CHECK-ALIAS: NoAlias: i32* %gen, i32 addrspace(1)* %global
define void @test_traversal_space_gen(ptr addrspace(5) %local, ptr addrspace(1) %global) {
  %gen = addrspacecast ptr addrspace(5) %local to ptr
  store i32 2, ptr %gen, align 8
  store i32 5, ptr addrspace(1) %global, align 8
  ret void
}

;; generic->space->generic
; CHECK-ALIAS-LABEL: Function: test_traversal_gen_space_gen
; CHECK-ALIAS: NoAlias: i32* %gen2, i32 addrspace(1)* %global
define void @test_traversal_gen_space_gen(ptr %gen1, ptr addrspace(1) %global) {
  %local = addrspacecast ptr %gen1 to ptr addrspace(5)
  %gen2 = addrspacecast ptr addrspace(5) %local to ptr
  store i32 3, ptr %gen2, align 8
  store i32 5, ptr addrspace(1) %global, align 8
  ret void
}
