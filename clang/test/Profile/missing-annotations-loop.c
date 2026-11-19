// Test missing-annotations remarks for branches in and around loops.

// RUN: rm -rf %t && split-file %s %t
// RUN: llvm-profdata merge %t/a.proftext -o %t/profdata
// RUN: %clang_cc1 %t/a.c -O2 -o /dev/null -emit-llvm -fprofile-instrument-use=clang \
// RUN:   -fprofile-instrument-use-path=%t/profdata -verify \
// RUN:   -fdiagnostics-missing-annotations -Rpass=missing-annotations

//--- a.c
int work(int);
int is_rare(int);

int loop(int n) {
  int sum = 0;
  // FIXME: The loop condition shouldn't be reported.
  for (int i = 0; i < n; ++i) { // expected-remark {{Extremely hot condition. Consider adding llvm.expect intrinsic}}
    if (is_rare(i)) // expected-remark {{Extremely hot condition. Consider adding llvm.expect intrinsic}}
      sum -= 1;
    else
      sum += work(i);
  }
  return sum;
}

//--- a.proftext
loop
# Func Hash:
19458656375896
# Num Counters:
3
# Counter Values:
1
200000
0
