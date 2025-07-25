! RUN: bbc -hlfir=false %s -o - | tco | FileCheck %s
! RUN: %flang -emit-llvm -flang-deprecated-no-hlfir -S -mmlir -disable-external-name-interop %s -o - | FileCheck %s
! Test from Fortran source through to LLVM IR.
! UNSUPPORTED: system-windows
! Disabled on 32-bit targets due to the additional `trunc` opcodes required
! UNSUPPORTED: target-x86
! UNSUPPORTED: target=sparc-{{.*}}
! UNSUPPORTED: target=sparcel-{{.*}}

! Assumed size array of assumed length character.
program test
  character :: x(3) = (/ '1','2','3' /)
  call sub(x)
contains
  subroutine sub(x)
    character(*) x(:)
    forall (i=1:2)
       x(i:i)(1:1) = x(i+1:i+1)(1:1)
    end forall
    print *,x
  end subroutine sub
end program test

! CHECK-LABEL: define internal void @_QFPsub(
! CHECK-SAME:    ptr {{[^%]*}}%[[arg:.*]])
! CHECK: %[[extent:.*]] = getelementptr { {{.*}}, [1 x [3 x i64]] }, ptr %[[arg]], i32 0, i32 7, i64 0, i32 1
! CHECK: %[[extval:.*]] = load i64, ptr %[[extent]]
! CHECK: %[[elesize:.*]] = getelementptr { {{.*}}, [1 x [3 x i64]] }, ptr %[[arg]], i32 0, i32 1
! CHECK: %[[esval:.*]] = load i64, ptr %[[elesize]]
! CHECK: %[[mul:.*]] = mul i64 1, %[[esval]]
! CHECK: %[[mul2:.*]] = mul i64 %[[mul]], %[[extval]]
! CHECK: %[[cmp:.*]] = icmp sgt i64 %[[mul2]], 0
! CHECK: %[[size:.*]] = select i1 %[[cmp]], i64 %[[mul2]], i64 1
! CHECK: %[[buff:.*]] = call ptr @malloc(i64 %[[size]])
! CHECK: %[[to:.*]] = getelementptr i8, ptr %[[buff]], i64 %
! CHECK: call void @llvm.memmove.p0.p0.i64(ptr %[[to]], ptr %{{.*}}, i64 %{{.*}}, i1 false)
! CHECK: call void @free(ptr %[[buff]])
! CHECK: call ptr @_FortranAioBeginExternalListOutput
