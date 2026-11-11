#include "Charset.h"

#include <algorithm>
#include <cctype>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#endif

namespace crucible {
namespace {

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7Fu) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFu) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp <= 0xFFFFu) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

bool isUnicodeScalar(std::uint32_t cp) {
    return cp <= 0x10FFFFu && !(cp >= 0xD800u && cp <= 0xDFFFu);
}

std::string normalizeName(std::string_view name) {
    std::string result;
    for (unsigned char c : name) {
        if (c == '-' || c == '_' || c == ' ' || c == '\t') {
            continue;
        }
        result.push_back(static_cast<char>(std::toupper(c)));
    }
    return result;
}

bool decodeUtf8Strict(std::string_view input, std::string& output,
                      std::string& error) {
    output.clear();
    output.reserve(input.size());
    for (std::size_t i = 0; i < input.size();) {
        const unsigned char first = static_cast<unsigned char>(input[i]);
        std::uint32_t cp = 0;
        std::size_t extra = 0;
        std::uint32_t minimum = 0;
        if (first < 0x80u) {
            cp = first;
        } else if (first >= 0xC2u && first <= 0xDFu) {
            extra = 1;
            cp = first & 0x1Fu;
            minimum = 0x80u;
        } else if (first >= 0xE0u && first <= 0xEFu) {
            extra = 2;
            cp = first & 0x0Fu;
            minimum = 0x800u;
        } else if (first >= 0xF0u && first <= 0xF4u) {
            extra = 3;
            cp = first & 0x07u;
            minimum = 0x10000u;
        } else {
            error = "invalid UTF-8 input";
            return false;
        }
        if (i + extra >= input.size()) {
            error = "truncated UTF-8 input";
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const unsigned char continuation =
                static_cast<unsigned char>(input[i + k]);
            if ((continuation & 0xC0u) != 0x80u) {
                error = "invalid UTF-8 input";
                return false;
            }
            cp = (cp << 6) | (continuation & 0x3Fu);
        }
        if (cp < minimum || !isUnicodeScalar(cp)) {
            error = "invalid UTF-8 input";
            return false;
        }
        output.append(input.substr(i, extra + 1));
        i += extra + 1;
    }
    return true;
}

#ifdef _WIN32

std::wstring utf16ToWide(std::string_view input, bool littleEndian,
                         std::string& error) {
    std::wstring result;
    result.reserve(input.size() / 2);
    const auto unitAt = [&](std::size_t offset) -> std::uint16_t {
        const unsigned char a = static_cast<unsigned char>(input[offset]);
        const unsigned char b = static_cast<unsigned char>(input[offset + 1]);
        return littleEndian
                   ? static_cast<std::uint16_t>(a | (b << 8))
                   : static_cast<std::uint16_t>((a << 8) | b);
    };
    if (input.size() % 2 != 0) {
        error = "odd number of bytes in UTF-16 input";
        return {};
    }
    for (std::size_t i = 0; i < input.size(); i += 2) {
        std::uint16_t unit = unitAt(i);
        if (unit >= 0xD800u && unit <= 0xDBFFu) {
            if (i + 3 >= input.size()) {
                error = "truncated UTF-16 surrogate pair";
                return {};
            }
            const std::uint16_t low = unitAt(i + 2);
            if (low < 0xDC00u || low > 0xDFFFu) {
                error = "invalid UTF-16 surrogate pair";
                return {};
            }
            const std::uint32_t cp =
                0x10000u + ((static_cast<std::uint32_t>(unit) - 0xD800u) << 10) +
                (static_cast<std::uint32_t>(low) - 0xDC00u);
            wchar_t wide[2];
            wide[0] = static_cast<wchar_t>(unit);
            wide[1] = static_cast<wchar_t>(low);
            result.append(wide, 2);
            i += 2;
            (void)cp;
        } else if (unit >= 0xDC00u && unit <= 0xDFFFu) {
            error = "unexpected UTF-16 low surrogate";
            return {};
        } else {
            result.push_back(static_cast<wchar_t>(unit));
        }
    }
    return result;
}

bool decodeCodePage(std::string_view input, int codePage, std::string& output,
                    std::string& error) {
    if (input.empty()) {
        output.clear();
        return true;
    }
    // MB_ERR_INVALID_CHARS is not accepted by every ANSI code page.
    const DWORD flags = 0;
    const int wideLength =
        MultiByteToWideChar(codePage, flags, input.data(),
                            static_cast<int>(input.size()), nullptr, 0);
    if (wideLength <= 0) {
        error = "input is not valid in the selected charset";
        return false;
    }
    std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
    MultiByteToWideChar(codePage, flags, input.data(),
                        static_cast<int>(input.size()), wide.data(),
                        wideLength);
    const int utf8Length =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, nullptr, 0,
                            nullptr, nullptr);
    if (utf8Length <= 0) {
        error = "failed to convert input to UTF-8";
        return false;
    }
    output.assign(static_cast<std::size_t>(utf8Length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wideLength, output.data(),
                        utf8Length, nullptr, nullptr);
    return true;
}

#endif

} // namespace

bool parseCharset(std::string_view name, CharsetEncoding& out,
                  std::string& error) {
    const std::string normalized = normalizeName(name);
    out = {};
    out.name = std::string(name);
    if (normalized.empty() || normalized == "UTF8" ||
        normalized == "CP65001") {
        out.kind = CharsetEncoding::Kind::Utf8;
        out.codePage = 65001;
        return true;
    }
    if (normalized == "UTF16" || normalized == "UTF16LE") {
        out.kind = CharsetEncoding::Kind::Utf16LE;
        out.codePage = 1200;
        return true;
    }
    if (normalized == "UTF16BE") {
        out.kind = CharsetEncoding::Kind::Utf16BE;
        out.codePage = 1201;
        return true;
    }

    int codePage = -1;
    if (normalized == "ASCII" || normalized == "USASCII" ||
        normalized == "CP20127") {
        codePage = 20127;
    } else if (normalized == "ISO88591" || normalized == "LATIN1" ||
               normalized == "CP28591") {
        codePage = 28591;
    } else if (normalized == "GBK" || normalized == "GB2312" ||
               normalized == "CP936" || normalized == "WINDOWS936") {
        codePage = 936;
    } else if (normalized == "WINDOWS1252" || normalized == "CP1252") {
        codePage = 1252;
    } else if (normalized == "SYSTEM" || normalized == "ANSI" ||
               normalized == "ACP" || normalized == "CPACP") {
        codePage = CP_ACP;
    } else if (normalized.rfind("CP", 0) == 0) {
        const std::string digits = normalized.substr(2);
        if (!digits.empty() &&
            std::all_of(digits.begin(), digits.end(), [](unsigned char c) {
                return std::isdigit(c) != 0;
            })) {
            try {
                codePage = std::stoi(digits);
            } catch (...) {
                codePage = -1;
            }
        }
    }

    if (codePage < 0) {
        error = "unsupported charset '" + std::string(name) +
                "' (supported: UTF-8, UTF-16, GBK, CP936, CP1252, "
                "ISO-8859-1, ASCII, CP<number>)";
        return false;
    }
    out.kind = CharsetEncoding::Kind::CodePage;
    out.codePage = codePage;
    return true;
}

bool decodeToUtf8(std::string_view input, const CharsetEncoding& encoding,
                  std::string& output, std::string& error) {
    output.clear();
    if (encoding.kind == CharsetEncoding::Kind::Utf8) {
        if (input.size() >= 3 &&
            static_cast<unsigned char>(input[0]) == 0xEF &&
            static_cast<unsigned char>(input[1]) == 0xBB &&
            static_cast<unsigned char>(input[2]) == 0xBF) {
            input.remove_prefix(3);
        }
        return decodeUtf8Strict(input, output, error);
    }
#ifdef _WIN32
    if (encoding.kind == CharsetEncoding::Kind::Utf16LE ||
        encoding.kind == CharsetEncoding::Kind::Utf16BE) {
        bool littleEndian = encoding.kind == CharsetEncoding::Kind::Utf16LE;
        if (input.size() >= 2) {
            const unsigned char a = static_cast<unsigned char>(input[0]);
            const unsigned char b = static_cast<unsigned char>(input[1]);
            if (a == 0xFF && b == 0xFE) {
                littleEndian = true;
                input.remove_prefix(2);
            } else if (a == 0xFE && b == 0xFF) {
                littleEndian = false;
                input.remove_prefix(2);
            }
        }
        const std::wstring wide = utf16ToWide(input, littleEndian, error);
        if (!error.empty()) {
            return false;
        }
        if (wide.empty()) {
            return true;
        }
        const int utf8Length =
            WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                static_cast<int>(wide.size()), nullptr, 0,
                                nullptr, nullptr);
        if (utf8Length <= 0) {
            error = "failed to convert UTF-16 input to UTF-8";
            return false;
        }
        output.assign(static_cast<std::size_t>(utf8Length), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                            static_cast<int>(wide.size()), output.data(),
                            utf8Length, nullptr, nullptr);
        return true;
    }
    return decodeCodePage(input, encoding.codePage, output, error);
#else
    error = "charset conversion is only implemented on Windows";
    return false;
#endif
}

bool encodeCodePoint(std::uint32_t cp, const CharsetEncoding& encoding,
                     std::vector<std::uint32_t>& units, std::string& error) {
    units.clear();
    if (!isUnicodeScalar(cp)) {
        error = "character is not a Unicode scalar value";
        return false;
    }
    if (encoding.kind == CharsetEncoding::Kind::Utf8) {
        std::string bytes;
        appendUtf8(bytes, cp);
        for (unsigned char c : bytes) {
            units.push_back(c);
        }
        return true;
    }
    if (encoding.kind == CharsetEncoding::Kind::Utf16LE ||
        encoding.kind == CharsetEncoding::Kind::Utf16BE) {
        error = "UTF-16 cannot be used as an execution charset for char constants";
        return false;
    }
#ifdef _WIN32
    std::wstring wide;
    if (cp <= 0xFFFFu) {
        wide.push_back(static_cast<wchar_t>(cp));
    } else {
        const std::uint32_t v = cp - 0x10000u;
        wide.push_back(static_cast<wchar_t>(0xD800u + (v >> 10)));
        wide.push_back(static_cast<wchar_t>(0xDC00u + (v & 0x3FFu)));
    }
    BOOL usedDefault = FALSE;
    const DWORD flags = WC_NO_BEST_FIT_CHARS;
    const int length =
        WideCharToMultiByte(encoding.codePage, flags, wide.data(),
                            static_cast<int>(wide.size()), nullptr, 0, nullptr,
                            &usedDefault);
    if (length <= 0 || usedDefault) {
        error = "character is not encodable in the execution charset";
        return false;
    }
    usedDefault = FALSE;
    std::string bytes(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(encoding.codePage, flags, wide.data(),
                        static_cast<int>(wide.size()), bytes.data(), length,
                        nullptr, &usedDefault);
    if (usedDefault) {
        error = "character is not encodable in the execution charset";
        return false;
    }
    for (unsigned char c : bytes) {
        units.push_back(c);
    }
    return true;
#else
    error = "charset conversion is only implemented on Windows";
    return false;
#endif
}

} // namespace crucible
