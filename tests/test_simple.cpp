// Basic object-like macros, undefinition, predefined macros and #line.
#define ANSWER 42
#define ALIAS ANSWER
#define EMPTY
#define REDEFINED 1
#define REDEFINED 1

EMPTY

static_assert(ANSWER == 42, "object macro");
static_assert(ALIAS == 42, "nested object macro");
static_assert(REDEFINED == 1, "identical redefinition");

#undef ANSWER
#define ANSWER 43
static_assert(ANSWER == 43, "undef and redefine");

// A block comment containing a newline does not terminate a directive.
#define COMMENTED 1 /* comment
*/ + 2
static_assert(COMMENTED == 3, "directive across a block comment");

int before_comment;
/* comment
*/ #define AFTER_COMMENT 4
static_assert(AFTER_COMMENT == 4, "directive after a block comment");

static_assert(__cplusplus >= 201703L, "__cplusplus");
static_assert(__LINE__ > 0, "__LINE__");
const char* source_name = __FILE__;

#line 500 "test_simple_virtual.cpp"
static_assert(__LINE__ == 500, "#line sets the next line number");
const char* virtual_name = __FILE__;

int main() {
    return 0;
}
