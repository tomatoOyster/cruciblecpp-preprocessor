// Trigraph translation is opt-in, matching GCC's -trigraphs mode.
??=include <cstddef>
??=define VALUE 42
??=define SUM 1 ??/
+ 2

int values??(2??) = ??<VALUE, VALUE??>;
static_assert(sizeof(values) == 2 * sizeof(int), "trigraph brackets");
static_assert(SUM == 3, "trigraph backslash-newline splice");
static_assert((1 ??' 1) == 0, "trigraph caret");
static_assert((1 ??! 2) == 3, "trigraph pipe");
static_assert((??-0) == -1, "trigraph tilde");

int main() {
    return values??(0??) == VALUE ? 0 : 1;
}
