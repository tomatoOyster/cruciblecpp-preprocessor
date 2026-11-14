static_assert('A' == 65, "ordinary character");
static_assert('\x41' == 65, "hex escape");
static_assert(L'\u4E2D' == 0x4E2D, "wide UCN");
static_assert(u'\u4E2D' == 0x4E2D, "char16 UCN");
static_assert(U'\U0001F600' == 0x1F600, "char32 non-BMP");
static_assert(u8'A' == 65, "u8 character");

int main() {
    return 0;
}
