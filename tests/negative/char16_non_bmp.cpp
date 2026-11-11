// char16_t is 16-bit; a non-BMP character must not be truncated.
#if u'\U0001F600'
#error char16 character unexpectedly accepted
#endif
