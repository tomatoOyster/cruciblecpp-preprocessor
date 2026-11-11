// Stringization, token pasting, variadic macros and __VA_OPT__.
#define STR(x) #x
#define CAT(a, b) a##b
#define CAT2(a, b) CAT(a, b)
#define FIRST(x, ...) x
#define OPTIONAL(x, ...) (x __VA_OPT__(+ __VA_ARGS__))
#define DECLARE_VALUE(name, value) constexpr int name = value

DECLARE_VALUE(CAT(answer, _value), 42);
static_assert(answer_value == 42, "token pasting and object expansion");

static_assert(CAT(1, 2) == 12, "paste numbers");
static_assert(CAT2(answer, _value) == 42, "two-level paste");
static_assert(FIRST(7, 8, 9) == 7, "variadic first argument");
static_assert(OPTIONAL(5) == 5, "empty __VA_OPT__");
static_assert(OPTIONAL(5, 6) == 11, "non-empty __VA_OPT__");

constexpr char text[] = STR(a   b);
static_assert(text[0] == 'a' && text[1] == ' ' && text[2] == 'b' &&
                  text[3] == '\0',
              "stringization");

int main() {
    return answer_value == 42 ? 0 : 1;
}
