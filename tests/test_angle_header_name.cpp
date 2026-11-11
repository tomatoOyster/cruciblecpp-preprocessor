// Windows-style backslash and doubled-slash header names.
#include <test_include\sub.h>
#include <test_include//sub.h>

static_assert(SUB_VALUE == 41, "angle header name spelling");

int main() {
    return 0;
}
