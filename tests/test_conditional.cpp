// Conditional inclusion, expression evaluation, short-circuiting.
#define VALUE 3

#if VALUE == 3
#define RESULT 1
#else
#define RESULT 0
#endif

static_assert(RESULT == 1, "#if");

#if VALUE == 1
#define CHAIN 1
#elif VALUE == 3
#define CHAIN 3
#else
#define CHAIN 0
#endif

static_assert(CHAIN == 3, "#elif");

#ifdef VALUE
#define DEFINED_VALUE 1
#else
#define DEFINED_VALUE 0
#endif

#ifndef NOT_DEFINED
#define NOT_DEFINED_VALUE 1
#else
#define NOT_DEFINED_VALUE 0
#endif

static_assert(DEFINED_VALUE == 1, "#ifdef");
static_assert(NOT_DEFINED_VALUE == 1, "#ifndef");

#if defined(VALUE) && !defined(NOT_DEFINED)
#define DEFINED_EXPR 1
#else
#define DEFINED_EXPR 0
#endif

static_assert(DEFINED_EXPR == 1, "defined operator");

#ifdef NOT_DEFINED
#define ELIFDEF_VALUE 0
#elifdef VALUE
#define ELIFDEF_VALUE 1
#else
#define ELIFDEF_VALUE 0
#endif

static_assert(ELIFDEF_VALUE == 1, "#elifdef");

#if 0
#error this branch must not be processed
#if garbage
#endif
#endif

#if 0 && (1 / 0)
#error short circuit failed
#endif

#if 1 ? 2 : (1 / 0)
#define TERNARY_VALUE 2
#else
#define TERNARY_VALUE 0
#endif

static_assert(TERNARY_VALUE == 2, "conditional expression");

#if 'A' == 65 && 0b1010 == 10 && 012 == 10 && 0xA == 10
#define LITERAL_VALUE 1
#else
#define LITERAL_VALUE 0
#endif

static_assert(LITERAL_VALUE == 1, "preprocessing integer/character literals");

#if 1 << 2 + 1 == 8
#define PRECEDENCE_VALUE 1
#else
#define PRECEDENCE_VALUE 0
#endif

static_assert(PRECEDENCE_VALUE == 1, "operator precedence");

int main() {
    return 0;
}
