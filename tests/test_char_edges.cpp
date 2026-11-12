// GCC 15.2 x86_64-w64-mingw32 character-constant edge behavior in #if.
// Errors are expected for u8 multi-byte and char16_t non-BMP constants, but
// GCC still evaluates a value and this test checks those values.

#if u8'中' == 0xAD
U8_VALUE_OK
#endif
#if L'😀' == 0xDE00
L_VALUE_OK
#endif
#if u'😀' == 0xDE00
U_VALUE_OK
#endif
#if U'😀' == 0x1F600
U32_VALUE_OK
#endif
#if '\x123456789' == -119
HEX_ORD_OK
#endif
#if L'\x123456789' == 0x6789
HEX_L_OK
#endif
#if u'\x123456789' == 0x6789
HEX_U_OK
#endif
#if U'\x123456789' == 0x23456789
HEX_U32_OK
#endif

int main() {
    return 0;
}
