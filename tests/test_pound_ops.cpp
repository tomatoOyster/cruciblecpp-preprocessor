// # and ## operators, empty arguments, placemarkers.
#define STR(x) #x
#define CAT(a, b) a##b
#define CAT2(a, b) CAT(a, b)
#define X 7
#define EMPTY

constexpr char spaced[] = STR(a   b);
static_assert(spaced[0] == 'a' && spaced[1] == ' ' && spaced[2] == 'b' &&
                  spaced[3] == '\0',
              "stringize collapses whitespace");

constexpr char empty_string[] = STR();
static_assert(empty_string[0] == '\0', "stringize empty argument");

constexpr char quoted[] = STR("a");
static_assert(quoted[0] == '"' && quoted[1] == 'a' && quoted[2] == '"' &&
                  quoted[3] == '\0',
              "stringize escapes quotes");

static_assert(CAT(1, 2) == 12, "paste numbers");
static_assert(CAT2(X, 8) == 78, "two-level paste expands arguments");
static_assert(CAT(, 5) == 5, "empty left operand");
static_assert(CAT(5, ) == 5, "empty right operand");
static_assert(CAT2(EMPTY, 1) == 1, "expanded empty argument");

CAT(,);

constexpr int CAT(foo, bar) = 3;
static_assert(foobar == 3, "paste identifiers");

int main() {
    return 0;
}
