#ifndef TEST_INCLUDE_MAIN_H
#define TEST_INCLUDE_MAIN_H

#include "sub.h"
#include "sub.h"
#include <sub.h>

#define MAIN_VALUE (SUB_VALUE + 1)
static_assert(MAIN_VALUE == 42, "main.h");

#if __has_include("sub.h")
#define HAS_SUB 1
#else
#define HAS_SUB 0
#endif

// This header is only probed, never included. GCC still records it in the
// depfile; the depfile test checks that CrucibleCpp does the same.
#if __has_include("probe.h")
#define HAS_PROBE 1
#else
#define HAS_PROBE 0
#endif
static_assert(HAS_PROBE == 1, "__has_include probe header");

#if __has_include("definitely_missing_header_xyz.h")
#define HAS_MISSING 1
#else
#define HAS_MISSING 0
#endif

static_assert(HAS_SUB == 1, "__has_include existing");
static_assert(HAS_MISSING == 0, "__has_include missing");

#endif
