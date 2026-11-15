const char* charset_text = "中";

#if '中' != 0xD6D0
#error GBK execution character constant
#endif

int main() {
    return charset_text[0] == '\xD6' ? 0 : 1;
}
