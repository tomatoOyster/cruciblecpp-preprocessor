// GCC treats a changed macro redefinition as a warning, not an error, and
// the new definition wins.
#define VALUE 1
#define VALUE 2

static_assert(VALUE == 2, "new macro definition wins");

int main() {
    return 0;
}
