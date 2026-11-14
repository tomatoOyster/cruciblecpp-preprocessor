#define CAT(a, b) a##b
#define STR(a) #a
#define ADD(a, b) ((a) + (b))
#define VARIADIC(fmt, ...) fmt

#if ADD(1, 2) != 3
#error arithmetic macro failed
#endif

int CAT(answer, _value) = ADD(40, 2);
const char* basic_string = STR(basic text);
const char* basic_variadic = VARIADIC("ok");

int main() {
    return answer_value == 42 && basic_string[0] == 'b' ? 0 : 1;
}
