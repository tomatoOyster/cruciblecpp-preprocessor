// A small example that can be preprocessed and then compiled.
#define GREETING "hello, preprocessor"
#define SQUARE(x) ((x) * (x))
#define VERSION_MAJOR 1
#define VERSION_MINOR 0

#if __cplusplus >= 202002L
#define LANGUAGE_LEVEL 20
#elif __cplusplus >= 201703L
#define LANGUAGE_LEVEL 17
#else
#define LANGUAGE_LEVEL 11
#endif

constexpr int square_of_three = SQUARE(3);
static_assert(square_of_three == 9, "SQUARE");
static_assert(VERSION_MAJOR == 1 && VERSION_MINOR == 0, "VERSION");
static_assert(LANGUAGE_LEVEL >= 17, "language level");

int main() {
    const char* message = GREETING;
    (void)message;
    return square_of_three == 9 ? 0 : 1;
}
