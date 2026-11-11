// Function-like macros, recursion, argument prescan, variadic macros.
#define ADD(a, b) ((a) + (b))
#define ID(x) x
#define SELF SELF
#define SELF_FN(x) SELF_FN(x)
#define FIRST(x, ...) x
#define OPTIONAL(x, ...) (x __VA_OPT__(+ __VA_ARGS__))
#define STR(x) #x
#define CAT(a, b) a##b

static_assert(ADD(1, 2) == 3, "function macro");
static_assert(ADD((1, 2), 3) == 5, "parenthesized comma argument");
static_assert(ID(ID(4)) == 4, "nested prescan");
static_assert(FIRST(7, 8, 9) == 7, "variadic first");
static_assert(OPTIONAL(5) == 5, "empty __VA_OPT__");
static_assert(OPTIONAL(5, 6) == 11, "non-empty __VA_OPT__");

constexpr int SELF = 1;
static_assert(SELF == 1, "self-referential object macro is blue-painted");

constexpr int CAT(func, tion) = 2;
static_assert(function == 2, "token pasting");

constexpr char text[] = STR(a   b);
static_assert(text[0] == 'a' && text[1] == ' ' && text[2] == 'b' &&
                  text[3] == '\0',
              "stringization");

int main() {
    return 0;
}
