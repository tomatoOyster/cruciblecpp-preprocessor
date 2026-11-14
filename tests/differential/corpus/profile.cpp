#if __cplusplus == 201103L
static_assert(__cpp_constexpr == 200704L, "C++11 constexpr");
static_assert(__cpp_lambdas == 200907L, "C++11 lambdas");
#elif __cplusplus == 201703L
static_assert(__cpp_constexpr == 201603L, "C++17 constexpr");
static_assert(__cpp_inline_variables == 201606L, "C++17 inline variables");
#elif __cplusplus == 202002L
static_assert(__cpp_constexpr == 202002L, "C++20 constexpr");
static_assert(__cpp_concepts == 202002L, "C++20 concepts");
#else
#error unexpected __cplusplus
#endif

int main() {
    return 0;
}
