// Character constants in #if, aligned with GCC 15.2 on Windows x64.
#include <cstdint>

#if 'a' != 97
#error ordinary character constant
#endif
#if '\x41' != 65
#error hex escape
#endif
#if '\101' != 65
#error octal escape
#endif
#if '\0' != 0
#error nul escape
#endif
#if '\x80' != -128
#error signed ordinary char
#endif
#if '\xFF' != -1
#error signed ordinary char 0xFF
#endif
#if '\777' != -1
#error octal overflow
#endif
#if 'ab' != 0x6162
#error two character constant
#endif
#if 'abcde' != 0x62636465
#error five character constant truncation
#endif
#if '\u4E2D' != 0xE4B8AD
#error UCN ordinary encoding
#endif
#if '\U0001F600' != 0xF09F9880
#error UCN ordinary non-BMP encoding
#endif
#if L'\u4E2D' != 0x4E2D
#error wide UCN
#endif
#if u'\u4E2D' != 0x4E2D
#error char16 UCN
#endif
#if U'\U0001F600' != 0x1F600
#error char32 UCN
#endif
#if u8'a' != 97
#error u8 ASCII
#endif
#if u8'\u0041' != 65
#error u8 UCN ASCII
#endif
#if u8'\x41' != 65
#error u8 hex ASCII
#endif

static_assert('a' == 97, "ordinary");
static_assert('\x41' == 65, "hex");
static_assert('\101' == 65, "octal");
static_assert('\0' == 0, "nul");
static_assert('\x80' == -128, "signed char");
static_assert('\xFF' == -1, "signed char FF");
static_assert('\777' == -1, "octal overflow");
static_assert('ab' == 0x6162, "multi-character");
static_assert('abcde' == 0x62636465, "multi-character truncation");
static_assert('\u4E2D' == 0xE4B8AD, "UCN ordinary");
static_assert('\U0001F600' == 0xF09F9880, "UCN ordinary non-BMP");
static_assert(L'\u4E2D' == 0x4E2D, "wide UCN");
static_assert(u'\u4E2D' == 0x4E2D, "char16 UCN");
static_assert(U'\U0001F600' == 0x1F600, "char32 UCN");
static_assert(u8'a' == 97, "u8 ASCII");
static_assert(u8'\u0041' == 65, "u8 UCN ASCII");
static_assert(u8'\x41' == 65, "u8 hex ASCII");

int main() {
    return 0;
}
