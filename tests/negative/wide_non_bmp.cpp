// Windows wchar_t is 16-bit; a non-BMP character must not be truncated.
#if L'\U0001F600'
#error wide character unexpectedly accepted
#endif
