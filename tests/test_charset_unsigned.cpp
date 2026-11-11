#if !defined(__CHAR_UNSIGNED__)
#error __CHAR_UNSIGNED__ missing
#endif
#if '\xFF' != 255
#error unsigned char constant
#endif
#if '\x80' != 128
#error unsigned char constant 0x80
#endif
int main() { return 0; }
