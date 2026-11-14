#include "include/probe.h"

#if __has_include("include/probe.h")
#define HAS_PROBE 1
#else
#define HAS_PROBE 0
#endif

static_assert(PROBE_VALUE == 7, "probe value");
static_assert(HAS_PROBE == 1, "probe presence");

int main() {
    return 0;
}
