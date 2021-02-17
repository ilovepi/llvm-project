#ifndef MPK_COMMON_H
#define MPK_COMMON_H

#define HAS_MPK 1
#define PAGE_MPK 0
#define SINGLE_STEP 1

#include "sanitizer_common/sanitizer_common.h"
#include <csignal>
#include <cstddef>
#include <cstdio>
#include <cstring>

#ifdef ENABLE_LOGGING
#define REPORT(...) __sanitizer::Report(__VA_ARGS__)
#else
#define REPORT(...)                                                            \
  do {                                                                         \
  } while (0)
#endif

#endif // MPK_COMMON_H
