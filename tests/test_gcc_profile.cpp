// Hardcoded GCC 15.2 profile and operator compatibility.
#if !defined(__GNUC__)
#error missing __GNUC__
#endif

#ifdef __has_builtin
static_assert(true, "__has_builtin is visible to #ifdef");
#else
#error __has_builtin must be visible to #ifdef
#endif

#if !__has_builtin(__builtin_launder)
#error missing __builtin_launder
#endif

#if !__has_builtin(__is_aggregate)
#error missing __is_aggregate
#endif

#if !__has_attribute(always_inline)
#error missing always_inline
#endif

#if __has_cpp_attribute(nodiscard) < 201907
#error nodiscard attribute version
#endif

static_assert(__GNUC__ == 15, "GCC major version profile");
static_assert(_WIN32 == 1, "Windows target macro");
static_assert(__x86_64__ == 1, "x86_64 target macro");
static_assert(__SIZEOF_POINTER__ == 8, "pointer size profile");
static_assert(__cplusplus >= 202002L, "C++ standard macro");

int main() {
    return 0;
}
