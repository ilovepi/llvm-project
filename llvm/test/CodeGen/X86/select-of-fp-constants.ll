; NOTE: Assertions have been autogenerated by utils/update_llc_test_checks.py
; RUN: llc < %s -mtriple=i386--   -mattr=sse2     | FileCheck %s --check-prefixes=X32,X32_SSE,X32_SSE2
; RUN: llc < %s -mtriple=i386--   -mattr=sse4.1   | FileCheck %s --check-prefixes=X32,X32_SSE,X32_SSE4
; RUN: llc < %s -mtriple=i386--   -mattr=avx2     | FileCheck %s --check-prefixes=X32,X32_AVX,X32_AVX2
; RUN: llc < %s -mtriple=i386--   -mattr=avx512f  | FileCheck %s --check-prefixes=X32,X32_AVX,X32_AVX512F
; RUN: llc < %s -mtriple=x86_64-- -mattr=sse2     | FileCheck %s --check-prefixes=X64,X64_SSE,X64_SSE2
; RUN: llc < %s -mtriple=x86_64-- -mattr=sse4.1   | FileCheck %s --check-prefixes=X64,X64_SSE,X64_SSE4
; RUN: llc < %s -mtriple=x86_64-- -mattr=avx2     | FileCheck %s --check-prefixes=X64,X64_AVX,X64_AVX2
; RUN: llc < %s -mtriple=x86_64-- -mattr=avx512f  | FileCheck %s --check-prefixes=X64,X64_AVX,X64_AVX512F

; This should do a single load into the fp stack for the return, not diddle with xmm registers.

define float @icmp_select_fp_constants(i32 %x) nounwind readnone {
; X32-LABEL: icmp_select_fp_constants:
; X32:       # %bb.0:
; X32-NEXT:    xorl %eax, %eax
; X32-NEXT:    cmpl $0, {{[0-9]+}}(%esp)
; X32-NEXT:    sete %al
; X32-NEXT:    flds {{\.LCPI.*}}(,%eax,4)
; X32-NEXT:    retl
;
; X64_SSE-LABEL: icmp_select_fp_constants:
; X64_SSE:       # %bb.0:
; X64_SSE-NEXT:    xorl %eax, %eax
; X64_SSE-NEXT:    testl %edi, %edi
; X64_SSE-NEXT:    sete %al
; X64_SSE-NEXT:    movss {{.*#+}} xmm0 = mem[0],zero,zero,zero
; X64_SSE-NEXT:    retq
;
; X64_AVX-LABEL: icmp_select_fp_constants:
; X64_AVX:       # %bb.0:
; X64_AVX-NEXT:    xorl %eax, %eax
; X64_AVX-NEXT:    testl %edi, %edi
; X64_AVX-NEXT:    sete %al
; X64_AVX-NEXT:    vmovss {{.*#+}} xmm0 = mem[0],zero,zero,zero
; X64_AVX-NEXT:    retq
	%c = icmp eq i32 %x, 0
	%r = select i1 %c, float 42.0, float 23.0
	ret float %r
}

define float @fcmp_select_fp_constants(float %x) nounwind readnone {
; X32_SSE-LABEL: fcmp_select_fp_constants:
; X32_SSE:       # %bb.0:
; X32_SSE-NEXT:    movss {{.*#+}} xmm0 = mem[0],zero,zero,zero
; X32_SSE-NEXT:    cmpneqss {{[0-9]+}}(%esp), %xmm0
; X32_SSE-NEXT:    movd %xmm0, %eax
; X32_SSE-NEXT:    andl $1, %eax
; X32_SSE-NEXT:    flds {{\.LCPI.*}}(,%eax,4)
; X32_SSE-NEXT:    retl
;
; X32_AVX2-LABEL: fcmp_select_fp_constants:
; X32_AVX2:       # %bb.0:
; X32_AVX2-NEXT:    vmovss {{.*#+}} xmm0 = mem[0],zero,zero,zero
; X32_AVX2-NEXT:    vcmpneqss {{[0-9]+}}(%esp), %xmm0, %xmm0
; X32_AVX2-NEXT:    vmovd %xmm0, %eax
; X32_AVX2-NEXT:    andl $1, %eax
; X32_AVX2-NEXT:    flds {{\.LCPI.*}}(,%eax,4)
; X32_AVX2-NEXT:    retl
;
; X32_AVX512F-LABEL: fcmp_select_fp_constants:
; X32_AVX512F:       # %bb.0:
; X32_AVX512F-NEXT:    vmovss {{.*#+}} xmm0 = mem[0],zero,zero,zero
; X32_AVX512F-NEXT:    vcmpneqss {{\.LCPI.*}}, %xmm0, %k0
; X32_AVX512F-NEXT:    kmovw %k0, %eax
; X32_AVX512F-NEXT:    flds {{\.LCPI.*}}(,%eax,4)
; X32_AVX512F-NEXT:    retl
;
; X64_SSE-LABEL: fcmp_select_fp_constants:
; X64_SSE:       # %bb.0:
; X64_SSE-NEXT:    cmpneqss {{.*}}(%rip), %xmm0
; X64_SSE-NEXT:    movd %xmm0, %eax
; X64_SSE-NEXT:    andl $1, %eax
; X64_SSE-NEXT:    movss {{.*#+}} xmm0 = mem[0],zero,zero,zero
; X64_SSE-NEXT:    retq
;
; X64_AVX2-LABEL: fcmp_select_fp_constants:
; X64_AVX2:       # %bb.0:
; X64_AVX2-NEXT:    vcmpneqss {{.*}}(%rip), %xmm0, %xmm0
; X64_AVX2-NEXT:    vmovss {{.*#+}} xmm1 = mem[0],zero,zero,zero
; X64_AVX2-NEXT:    vmovss {{.*#+}} xmm2 = mem[0],zero,zero,zero
; X64_AVX2-NEXT:    vblendvps %xmm0, %xmm1, %xmm2, %xmm0
; X64_AVX2-NEXT:    retq
;
; X64_AVX512F-LABEL: fcmp_select_fp_constants:
; X64_AVX512F:       # %bb.0:
; X64_AVX512F-NEXT:    vmovss {{.*#+}} xmm1 = mem[0],zero,zero,zero
; X64_AVX512F-NEXT:    vcmpneqss {{.*}}(%rip), %xmm0, %k1
; X64_AVX512F-NEXT:    vmovss {{.*#+}} xmm0 = mem[0],zero,zero,zero
; X64_AVX512F-NEXT:    vmovss %xmm1, %xmm0, %xmm0 {%k1}
; X64_AVX512F-NEXT:    retq
 %c = fcmp une float %x, -4.0
 %r = select i1 %c, float 42.0, float 23.0
 ret float %r
}

