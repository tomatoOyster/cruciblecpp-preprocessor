// Complete GCC 15.2 __has_* coverage: representative long-tail names and
// scoped C++ attributes. Generated GccHasData.inc is tied to GCC 15.2.

#if !__has_builtin(__is_abstract)
#error __is_abstract missing
#endif
#if !__has_builtin(__is_scoped_enum)
#error __is_scoped_enum missing
#endif
#if !__has_builtin(__builtin_acos)
#error __builtin_acos missing
#endif
#if __has_builtin(__builtin_operator_new) != 201802
#error __builtin_operator_new version
#endif
#if __has_builtin(__builtin_operator_delete) != 201802
#error __builtin_operator_delete version
#endif
#if !__has_attribute(no_sanitize_thread)
#error no_sanitize_thread missing
#endif
#if !__has_attribute(__always_inline__)
#error wrapped __always_inline__ missing
#endif
#if __has_cpp_attribute(gnu::always_inline) != 1
#error gnu::always_inline missing
#endif
#if __has_cpp_attribute(nodiscard) < 201907
#error nodiscard value too small
#endif
#if !__has_feature(cxx_rvalue_references)
#error cxx_rvalue_references feature missing
#endif
#if !__has_extension(cxx_rvalue_references)
#error cxx_rvalue_references extension missing
#endif

int main() {
    return 0;
}
