#include "ConditionalEngine.h"

#include "Charset.h"
#include "Preprocessor.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace crucible {
namespace {

struct PPValue {
    std::uint64_t bits = 0;
    bool isUnsigned = false;
};

PPValue makeSigned(std::int64_t value) {
    return {static_cast<std::uint64_t>(value), false};
}

PPValue makeUnsigned(std::uint64_t value) { return {value, true}; }

PPValue makeBool(bool value) { return makeSigned(value ? 1 : 0); }

bool isTrue(const PPValue& value) { return value.bits != 0; }

std::int64_t asSigned(const PPValue& value) {
    return static_cast<std::int64_t>(value.bits);
}

bool bothSigned(const PPValue& a, const PPValue& b) {
    return !a.isUnsigned && !b.isUnsigned;
}

std::string normalizeOperator(const Token& token) {
    if (token.kind == TokenKind::Identifier) {
        if (token.text == "and") {
            return "&&";
        }
        if (token.text == "or") {
            return "||";
        }
        if (token.text == "not") {
            return "!";
        }
        if (token.text == "bitand") {
            return "&";
        }
        if (token.text == "bitor") {
            return "|";
        }
        if (token.text == "xor") {
            return "^";
        }
        if (token.text == "compl") {
            return "~";
        }
        if (token.text == "not_eq") {
            return "!=";
        }
    }
    return token.text;
}

bool parseIntegerLiteral(std::string_view text, PPValue& out,
                         std::string& error) {
    std::string spelling;
    spelling.reserve(text.size());
    for (char c : text) {
        if (c != '\'') {
            spelling.push_back(c);
        }
    }
    if (spelling.empty()) {
        error = "empty integer literal";
        return false;
    }

    std::size_t i = 0;
    int base = 10;
    if (spelling.size() >= 2 && spelling[0] == '0' &&
        (spelling[1] == 'x' || spelling[1] == 'X')) {
        base = 16;
        i = 2;
    } else if (spelling.size() >= 2 && spelling[0] == '0' &&
               (spelling[1] == 'b' || spelling[1] == 'B')) {
        base = 2;
        i = 2;
    } else if (spelling[0] == '0') {
        base = 8;
        i = 1;
    }

    auto digitValue = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    };

    std::uint64_t value = 0;
    bool sawDigit = false;
    if (base == 8 && i == 1) {
        // A bare leading zero is a valid octal literal.
        sawDigit = true;
    }
    while (i < spelling.size()) {
        const int digit = digitValue(spelling[i]);
        if (digit < 0 || digit >= base) {
            break;
        }
        sawDigit = true;
        if (value >
            (std::numeric_limits<std::uint64_t>::max() -
             static_cast<std::uint64_t>(digit)) /
                static_cast<std::uint64_t>(base)) {
            error = "integer literal is too large";
            return false;
        }
        value = value * static_cast<std::uint64_t>(base) +
                static_cast<std::uint64_t>(digit);
        ++i;
    }
    if (!sawDigit) {
        error = "invalid integer literal";
        return false;
    }

    bool isUnsigned = false;
    std::string suffix = spelling.substr(i);
    for (char c : suffix) {
        if (c == 'u' || c == 'U') {
            isUnsigned = true;
        } else if (c == 'l' || c == 'L' || c == 'z' || c == 'Z') {
            // length/size suffix; accepted
        } else {
            error = "invalid suffix on integer literal";
            return false;
        }
    }

    out.bits = value;
    if (!isUnsigned &&
        value > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max())) {
        // The literal fits uintmax_t but not intmax_t; its type is unsigned.
        isUnsigned = true;
    }
    out.isUnsigned = isUnsigned;
    return true;
}

enum class CharacterPrefix { Ordinary, Wide, Utf16, Utf32, Utf8 };

bool isUnicodeScalar(std::uint32_t cp) {
    return cp <= 0x10FFFFu && !(cp >= 0xD800u && cp <= 0xDFFFu);
}

bool decodeUtf8(std::string_view text, std::size_t& i, std::uint32_t& cp,
                std::string& error) {
    const unsigned char first = static_cast<unsigned char>(text[i]);
    if (first < 0x80u) {
        cp = first;
        ++i;
        return true;
    }

    std::size_t extra = 0;
    std::uint32_t value = 0;
    std::uint32_t minimum = 0;
    if (first >= 0xC2u && first <= 0xDFu) {
        extra = 1;
        value = first & 0x1Fu;
        minimum = 0x80u;
    } else if (first >= 0xE0u && first <= 0xEFu) {
        extra = 2;
        value = first & 0x0Fu;
        minimum = 0x800u;
    } else if (first >= 0xF0u && first <= 0xF4u) {
        extra = 3;
        value = first & 0x07u;
        minimum = 0x10000u;
    } else {
        error = "invalid UTF-8 sequence in character literal";
        return false;
    }

    if (i + extra >= text.size()) {
        error = "truncated UTF-8 sequence in character literal";
        return false;
    }
    for (std::size_t k = 1; k <= extra; ++k) {
        const unsigned char continuation =
            static_cast<unsigned char>(text[i + k]);
        if ((continuation & 0xC0u) != 0x80u) {
            error = "invalid UTF-8 sequence in character literal";
            return false;
        }
        value = (value << 6) | (continuation & 0x3Fu);
    }
    if (value < minimum || !isUnicodeScalar(value)) {
        error = "invalid UTF-8 sequence in character literal";
        return false;
    }
    cp = value;
    i += extra + 1;
    return true;
}

void appendUtf8(std::vector<std::uint32_t>& units, std::uint32_t cp) {
    if (cp <= 0x7Fu) {
        units.push_back(cp);
    } else if (cp <= 0x7FFu) {
        units.push_back(0xC0u | (cp >> 6));
        units.push_back(0x80u | (cp & 0x3Fu));
    } else if (cp <= 0xFFFFu) {
        units.push_back(0xE0u | (cp >> 12));
        units.push_back(0x80u | ((cp >> 6) & 0x3Fu));
        units.push_back(0x80u | (cp & 0x3Fu));
    } else {
        units.push_back(0xF0u | (cp >> 18));
        units.push_back(0x80u | ((cp >> 12) & 0x3Fu));
        units.push_back(0x80u | ((cp >> 6) & 0x3Fu));
        units.push_back(0x80u | (cp & 0x3Fu));
    }
}

struct EscapeValue {
    std::uint64_t value = 0;
    bool universal = true;
    bool hexadecimal = false;
    bool octal = false;
    bool overflow = false;
};

bool decodeEscape(std::string_view text, std::size_t& i, EscapeValue& out,
                  std::string& error) {
    // i points at the backslash.
    if (i + 1 >= text.size()) {
        error = "incomplete escape sequence in character literal";
        return false;
    }
    const char c = text[i + 1];
    i += 2;
    out = {};
    switch (c) {
    case 'a':
        out.value = 7;
        return true;
    case 'b':
        out.value = 8;
        return true;
    case 'e':
        out.value = 27; // GNU extension, matches GCC on Windows.
        return true;
    case 'f':
        out.value = 12;
        return true;
    case 'n':
        out.value = 10;
        return true;
    case 'r':
        out.value = 13;
        return true;
    case 't':
        out.value = 9;
        return true;
    case 'v':
        out.value = 11;
        return true;
    case '\\':
        out.value = '\\';
        return true;
    case '\'':
        out.value = '\'';
        return true;
    case '"':
        out.value = '"';
        return true;
    case '?':
        out.value = '?';
        return true;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7': {
        out.octal = true;
        out.universal = false;
        std::uint32_t result = static_cast<std::uint32_t>(c - '0');
        int count = 1;
        while (count < 3 && i < text.size() && text[i] >= '0' &&
               text[i] <= '7') {
            result = result * 8 + static_cast<std::uint32_t>(text[i] - '0');
            ++i;
            ++count;
        }
        out.value = result;
        return true;
    }
    case 'x': {
        out.hexadecimal = true;
        out.universal = false;
        std::uint32_t result = 0;
        bool any = false;
        while (i < text.size()) {
            const char h = text[i];
            int digit = -1;
            if (h >= '0' && h <= '9') {
                digit = h - '0';
            } else if (h >= 'a' && h <= 'f') {
                digit = h - 'a' + 10;
            } else if (h >= 'A' && h <= 'F') {
                digit = h - 'A' + 10;
            }
            if (digit < 0) {
                break;
            }
            // Keep the low 32 bits while remembering that the source value
            // exceeded the range. GCC diagnoses the overflow but still uses
            // these low bits for U'\x...'.
            if (result > (0xFFFFFFFFu - static_cast<std::uint32_t>(digit)) /
                             16u) {
                out.overflow = true;
            }
            result = result * 16u + static_cast<std::uint32_t>(digit);
            any = true;
            ++i;
        }
        if (!any) {
            error = "\\x used with no following hex digits";
            return false;
        }
        out.value = result;
        return true;
    }
    case 'u':
    case 'U': {
        const std::size_t digits = c == 'u' ? 4 : 8;
        if (i + digits > text.size()) {
            error = "incomplete universal character name";
            return false;
        }
        std::uint32_t result = 0;
        for (std::size_t k = 0; k < digits; ++k) {
            const char h = text[i + k];
            int digit = -1;
            if (h >= '0' && h <= '9') {
                digit = h - '0';
            } else if (h >= 'a' && h <= 'f') {
                digit = h - 'a' + 10;
            } else if (h >= 'A' && h <= 'F') {
                digit = h - 'A' + 10;
            }
            if (digit < 0) {
                error = "invalid universal character name";
                return false;
            }
            result = result * 16 + static_cast<std::uint32_t>(digit);
        }
        if (!isUnicodeScalar(result)) {
            error = "invalid universal character name";
            return false;
        }
        i += digits;
        out.value = result;
        return true;
    }
    default:
        // GCC accepts unknown escapes with a warning and uses the character
        // itself. Keep the value; no warning channel is needed for #if.
        out.value = static_cast<unsigned char>(c);
        return true;
    }
}

bool parseCharacterLiteral(std::string_view text, PPValue& out,
                           std::string& error,
                           std::vector<std::string>& warnings,
                           std::vector<std::string>& errors,
                           const CharacterOptions& characterOptions) {
    const std::size_t quote = text.find('\'');
    if (quote == std::string_view::npos || text.size() < quote + 2) {
        error = "invalid character literal";
        return false;
    }
    const std::size_t end = text.rfind('\'');
    if (end <= quote) {
        error = "invalid character literal";
        return false;
    }

    const std::string_view prefixText = text.substr(0, quote);
    CharacterPrefix prefix = CharacterPrefix::Ordinary;
    if (prefixText == "L") {
        prefix = CharacterPrefix::Wide;
    } else if (prefixText == "u") {
        prefix = CharacterPrefix::Utf16;
    } else if (prefixText == "U") {
        prefix = CharacterPrefix::Utf32;
    } else if (prefixText == "u8") {
        prefix = CharacterPrefix::Utf8;
    } else if (!prefixText.empty()) {
        error = "invalid character literal prefix";
        return false;
    }

    CharsetEncoding executionCharset;
    if (!parseCharset(characterOptions.executionCharset, executionCharset,
                      error)) {
        return false;
    }

    const std::string_view body = text.substr(quote + 1, end - quote - 1);
    if (body.empty()) {
        error = "empty character literal";
        return false;
    }

    std::vector<std::uint32_t> units;
    for (std::size_t i = 0; i < body.size();) {
        EscapeValue escaped;
        bool universal = false;
        if (body[i] == '\\') {
            if (!decodeEscape(body, i, escaped, error)) {
                return false;
            }
            universal = escaped.universal;
        } else {
            std::uint32_t cp = 0;
            if (!decodeUtf8(body, i, cp, error)) {
                return false;
            }
            escaped.value = cp;
            universal = true;
        }

        if (universal) {
            const std::uint32_t cp = escaped.value;
            if (prefix == CharacterPrefix::Ordinary) {
                // GCC 15.2 x86_64-w64-mingw32 defaults to UTF-8 as the
                // execution charset; -fexec-charset can select a code page.
                // Ordinary UTF-8/UCN characters become execution-charset
                // code units (e.g. '中' == 0xE4B8AD under UTF-8).
                std::vector<std::uint32_t> encoded;
                if (!encodeCodePoint(cp, executionCharset, encoded, error)) {
                    return false;
                }
                units.insert(units.end(), encoded.begin(), encoded.end());
            } else if (prefix == CharacterPrefix::Wide ||
                       prefix == CharacterPrefix::Utf16) {
                // GCC 15.2 x86_64-w64-mingw32: wchar_t/char16_t are 16-bit.
                // A non-BMP source character is diagnosed, but GCC still
                // evaluates it as the low 16 bits (0xDE00 for U+1F600).
                if (cp > 0xFFFFu) {
                    if (prefix == CharacterPrefix::Wide) {
                        warnings.push_back(
                            "character not encodable in a single code unit");
                    } else {
                        errors.push_back(
                            "character not encodable in a single code unit");
                    }
                    const std::uint32_t offset = cp - 0x10000u;
                    units.push_back(0xDC00u + (offset & 0x3FFu));
                } else {
                    units.push_back(cp);
                }
            } else if (prefix == CharacterPrefix::Utf32) {
                units.push_back(cp);
            } else { // u8
                // C++20 u8 character literals have one UTF-8 code unit.
                // GCC diagnoses multi-byte characters but still evaluates the
                // last UTF-8 code unit ('中' -> 0xAD).
                std::vector<std::uint32_t> encoded;
                appendUtf8(encoded, cp);
                if (encoded.size() != 1) {
                    errors.push_back(
                        "character not encodable in a single code unit");
                    units.push_back(encoded.back());
                } else {
                    units.push_back(encoded.front());
                }
            }
        } else {
            // Numeric escapes are code-unit values, not Unicode scalars.
            // GCC truncates to the target code unit and warns.
            std::uint64_t value = escaped.value;
            std::uint64_t maximum = 0xFFu;
            if (prefix == CharacterPrefix::Wide ||
                prefix == CharacterPrefix::Utf16) {
                maximum = 0xFFFFu;
            } else if (prefix == CharacterPrefix::Utf32) {
                maximum = 0xFFFFFFFFu;
            }
            if (value > maximum || escaped.overflow) {
                warnings.push_back(escaped.hexadecimal
                                       ? "hex escape sequence out of range"
                                       : "octal escape sequence out of range");
                value &= maximum;
            }
            units.push_back(static_cast<std::uint32_t>(value));
        }
    }

    if (units.empty()) {
        error = "empty character literal";
        return false;
    }

    std::uint64_t value = 0;
    if (prefix == CharacterPrefix::Ordinary) {
        // GCC 15.2 x86_64-w64-mingw32 multi-character constants are
        // implementation-defined: bytes are packed most-significant first
        // (so 'ab' is 0x6162), and more than four bytes keep the last four
        // with a warning ('abcde' is 0x62636465).
        const std::size_t originalCount = units.size();
        if (units.size() > 4) {
            warnings.push_back(
                "multi-character literal with " +
                std::to_string(originalCount) +
                " characters exceeds 'int' size of 4 bytes");
            units.erase(units.begin(),
                        units.end() - static_cast<std::ptrdiff_t>(4));
        } else if (units.size() > 1) {
            warnings.push_back("multi-character character constant");
        }
        for (const std::uint32_t unit : units) {
            value = (value << 8) | (unit & 0xFFu);
        }
        // GCC 15.2 x86_64-w64-mingw32 has signed plain char by default, so
        // '\x80' promotes to -128 and '\xFF' to -1 in #if. -funsigned-char
        // disables this sign extension.
        if (!characterOptions.unsignedChar && units.size() == 1 &&
            (value & 0x80u) != 0) {
            value = static_cast<std::uint64_t>(
                static_cast<std::int64_t>(
                    static_cast<std::int8_t>(value & 0xFFu)));
        }
    } else if (prefix == CharacterPrefix::Wide) {
        if (units.size() > 1) {
            warnings.push_back(
                "multi-character literal cannot have an encoding prefix");
            value = units.back();
        } else {
            value = units.front();
        }
    } else {
        if (units.size() > 1) {
            error = "multi-character literal cannot have an encoding prefix";
            return false;
        }
        value = units.front();
    }

    out.bits = value;
    out.isUnsigned = false;
    return true;
}
class ExpressionParser {
public:
    ExpressionParser(const std::vector<Token>& tokens,
                     DiagnosticSink& diagnostics,
                     const CharacterOptions& characterOptions)
        : tokens_(tokens),
          diagnostics_(diagnostics),
          characterOptions_(characterOptions) {}

    bool evaluate() {
        if (tokens_.empty()) {
            if (!tokens_.empty()) {
                // unreachable; keeps the empty-expression path explicit
            }
            diagnostics_.error({}, "#if with no expression");
            return false;
        }
        PPValue value = parseConditional(true);
        skipIgnored();
        if (index_ != tokens_.size()) {
            diagnostics_.error(tokens_[index_].loc,
                               "unexpected token in #if expression: '" +
                                   tokens_[index_].text + "'");
            ok_ = false;
        }
        return ok_ && isTrue(value);
    }

private:
    bool atEnd() const { return index_ >= tokens_.size(); }

    const Token* peek() const { return atEnd() ? nullptr : &tokens_[index_]; }

    std::string operatorText() const {
        if (atEnd()) {
            return {};
        }
        return normalizeOperator(tokens_[index_]);
    }

    bool match(std::string_view text) {
        if (atEnd()) {
            return false;
        }
        if (operatorText() != text) {
            return false;
        }
        ++index_;
        return true;
    }

    void skipIgnored() {
        while (!atEnd() && tokens_[index_].isTrivia()) {
            ++index_;
        }
    }

    PPValue parseConditional(bool eval) {
        skipIgnored();
        PPValue condition = parseLogicalOr(eval);
        skipIgnored();
        if (!match("?")) {
            return condition;
        }

        const bool conditionTrue = isTrue(condition);
        const bool trueEval = eval && conditionTrue;
        PPValue trueValue = parseConditional(trueEval);
        skipIgnored();
        if (!match(":")) {
            if (!atEnd()) {
                diagnostics_.error(tokens_[index_].loc,
                                   "expected ':' in conditional expression");
            } else {
                diagnostics_.error({}, "expected ':' in conditional expression");
            }
            ok_ = false;
            return makeSigned(0);
        }
        const bool falseEval = eval && !conditionTrue;
        PPValue falseValue = parseConditional(falseEval);
        return conditionTrue ? trueValue : falseValue;
    }

    PPValue parseLogicalOr(bool eval) {
        PPValue lhs = parseLogicalAnd(eval);
        while (true) {
            skipIgnored();
            if (!match("||")) {
                break;
            }
            const bool rhsEval = eval && !isTrue(lhs);
            PPValue rhs = parseLogicalAnd(rhsEval);
            if (eval) {
                lhs = makeBool(isTrue(lhs) || isTrue(rhs));
            }
        }
        return lhs;
    }

    PPValue parseLogicalAnd(bool eval) {
        PPValue lhs = parseBitOr(eval);
        while (true) {
            skipIgnored();
            if (!match("&&")) {
                break;
            }
            const bool rhsEval = eval && isTrue(lhs);
            PPValue rhs = parseBitOr(rhsEval);
            if (eval) {
                lhs = makeBool(isTrue(lhs) && isTrue(rhs));
            }
        }
        return lhs;
    }

    PPValue parseBitOr(bool eval) {
        PPValue lhs = parseBitXor(eval);
        while (true) {
            skipIgnored();
            if (!match("|")) {
                break;
            }
            PPValue rhs = parseBitXor(eval);
            if (eval) {
                lhs = {lhs.bits | rhs.bits, lhs.isUnsigned || rhs.isUnsigned};
            }
        }
        return lhs;
    }

    PPValue parseBitXor(bool eval) {
        PPValue lhs = parseBitAnd(eval);
        while (true) {
            skipIgnored();
            if (!match("^")) {
                break;
            }
            PPValue rhs = parseBitAnd(eval);
            if (eval) {
                lhs = {lhs.bits ^ rhs.bits, lhs.isUnsigned || rhs.isUnsigned};
            }
        }
        return lhs;
    }

    PPValue parseBitAnd(bool eval) {
        PPValue lhs = parseEquality(eval);
        while (true) {
            skipIgnored();
            if (!match("&")) {
                break;
            }
            PPValue rhs = parseEquality(eval);
            if (eval) {
                lhs = {lhs.bits & rhs.bits, lhs.isUnsigned || rhs.isUnsigned};
            }
        }
        return lhs;
    }

    PPValue parseEquality(bool eval) {
        PPValue lhs = parseRelational(eval);
        while (true) {
            skipIgnored();
            std::string op = operatorText();
            if (op != "==" && op != "!=") {
                break;
            }
            ++index_;
            PPValue rhs = parseRelational(eval);
            if (eval) {
                const bool equal = bothSigned(lhs, rhs)
                                       ? (asSigned(lhs) == asSigned(rhs))
                                       : (lhs.bits == rhs.bits);
                lhs = makeBool(op == "==" ? equal : !equal);
            }
        }
        return lhs;
    }

    PPValue parseRelational(bool eval) {
        PPValue lhs = parseShift(eval);
        while (true) {
            skipIgnored();
            std::string op = operatorText();
            if (op != "<" && op != ">" && op != "<=" && op != ">=") {
                break;
            }
            ++index_;
            PPValue rhs = parseShift(eval);
            if (eval) {
                bool result = false;
                if (bothSigned(lhs, rhs)) {
                    const auto a = asSigned(lhs);
                    const auto b = asSigned(rhs);
                    result = op == "<"    ? a < b
                             : op == ">"  ? a > b
                             : op == "<=" ? a <= b
                                          : a >= b;
                } else {
                    const auto a = lhs.bits;
                    const auto b = rhs.bits;
                    result = op == "<"    ? a < b
                             : op == ">"  ? a > b
                             : op == "<=" ? a <= b
                                          : a >= b;
                }
                lhs = makeBool(result);
            }
        }
        return lhs;
    }

    PPValue parseShift(bool eval) {
        PPValue lhs = parseAdditive(eval);
        while (true) {
            skipIgnored();
            std::string op = operatorText();
            if (op != "<<" && op != ">>") {
                break;
            }
            ++index_;
            PPValue rhs = parseAdditive(eval);
            if (eval) {
                if (rhs.bits >= 64) {
                    diagnostics_.warning(
                        tokens_[index_ - 1].loc,
                        "integer overflow in preprocessor expression",
                        "-Woverflow");
                    lhs = makeSigned(0);
                } else if (op == "<<") {
                    lhs = {lhs.bits << rhs.bits, lhs.isUnsigned};
                } else if (lhs.isUnsigned) {
                    lhs = {lhs.bits >> rhs.bits, true};
                } else {
                    lhs = makeSigned(asSigned(lhs) >> rhs.bits);
                }
            }
        }
        return lhs;
    }

    PPValue parseAdditive(bool eval) {
        PPValue lhs = parseMultiplicative(eval);
        while (true) {
            skipIgnored();
            std::string op = operatorText();
            if (op != "+" && op != "-") {
                break;
            }
            ++index_;
            PPValue rhs = parseMultiplicative(eval);
            if (eval) {
                const bool uns = lhs.isUnsigned || rhs.isUnsigned;
                lhs = {op == "+" ? lhs.bits + rhs.bits : lhs.bits - rhs.bits,
                       uns};
            }
        }
        return lhs;
    }

    PPValue parseMultiplicative(bool eval) {
        PPValue lhs = parseUnary(eval);
        while (true) {
            skipIgnored();
            std::string op = operatorText();
            if (op != "*" && op != "/" && op != "%") {
                break;
            }
            ++index_;
            PPValue rhs = parseUnary(eval);
            if (eval) {
                const bool uns = lhs.isUnsigned || rhs.isUnsigned;
                if (rhs.bits == 0) {
                    diagnostics_.error(tokens_[index_ - 1].loc,
                                       "division by zero in #if expression");
                    ok_ = false;
                    lhs = makeSigned(0);
                } else if (op == "*") {
                    lhs = {lhs.bits * rhs.bits, uns};
                } else if (uns) {
                    lhs = {op == "/" ? lhs.bits / rhs.bits
                                     : lhs.bits % rhs.bits,
                           true};
                } else {
                    const auto a = asSigned(lhs);
                    const auto b = asSigned(rhs);
                    if (b == -1 && a == std::numeric_limits<std::int64_t>::min()) {
                        lhs = makeUnsigned(static_cast<std::uint64_t>(a));
                    } else {
                        lhs = makeSigned(op == "/" ? a / b : a % b);
                    }
                }
            }
        }
        return lhs;
    }

    PPValue parseUnary(bool eval) {
        skipIgnored();
        if (atEnd()) {
            diagnostics_.error({}, "unexpected end of #if expression");
            ok_ = false;
            return makeSigned(0);
        }
        const std::string op = operatorText();
        if (op == "+" || op == "-" || op == "!" || op == "~") {
            ++index_;
            PPValue operand = parseUnary(eval);
            if (!eval) {
                return makeSigned(0);
            }
            if (op == "+") {
                return operand;
            }
            if (op == "-") {
                return {0u - operand.bits, operand.isUnsigned};
            }
            if (op == "!") {
                return makeBool(!isTrue(operand));
            }
            return {~operand.bits, operand.isUnsigned};
        }
        return parsePrimary(eval);
    }

    PPValue parsePrimary(bool eval) {
        skipIgnored();
        if (atEnd()) {
            diagnostics_.error({}, "unexpected end of #if expression");
            ok_ = false;
            return makeSigned(0);
        }

        const Token& token = tokens_[index_];
        if (token.isPunct("(")) {
            ++index_;
            PPValue value = parseConditional(eval);
            skipIgnored();
            if (!match(")")) {
                diagnostics_.error(token.loc, "expected ')' in #if expression");
                ok_ = false;
            }
            return value;
        }

        if (token.kind == TokenKind::PPNumber) {
            ++index_;
            if (!eval) {
                return makeSigned(0);
            }
            PPValue value;
            std::string error;
            if (!parseIntegerLiteral(token.text, value, error)) {
                diagnostics_.error(token.loc, error);
                ok_ = false;
                return makeSigned(0);
            }
            return value;
        }

        if (token.kind == TokenKind::CharLiteral) {
            ++index_;
            PPValue value;
            std::string error;
            std::vector<std::string> warnings;
            std::vector<std::string> errors;
            if (!parseCharacterLiteral(token.text, value, error, warnings,
                                       errors, characterOptions_)) {
                diagnostics_.error(token.loc, error);
                ok_ = false;
                return makeSigned(0);
            }
            // Character literals are validated even in short-circuited
            // branches, matching GCC's diagnostic behavior. Some GCC errors
            // (u8 multi-byte, char16_t non-BMP) still produce a value and do
            // not force the condition to false.
            for (const auto& warning : warnings) {
                const bool multichar =
                    warning.find("multi-character") != std::string::npos;
                diagnostics_.warning(token.loc, warning,
                                     multichar ? "-Wmultichar"
                                               : "-Woverflow");
            }
            for (const auto& diagnostic : errors) {
                diagnostics_.error(token.loc, diagnostic);
            }
            if (!eval) {
                return makeSigned(0);
            }
            return value;
        }

        if (token.kind == TokenKind::Identifier) {
            ++index_;
            if (!eval) {
                return makeSigned(0);
            }
            if (token.text == "true") {
                return makeSigned(1);
            }
            if (token.text == "false") {
                return makeSigned(0);
            }
            // All other identifiers have already been replaced by 0 by the
            // preprocessor; this is a fallback for direct unit-test use.
            return makeSigned(0);
        }

        diagnostics_.error(token.loc,
                           "unexpected token in #if expression: '" + token.text +
                               "'");
        ok_ = false;
        ++index_;
        return makeSigned(0);
    }

    const std::vector<Token>& tokens_;
    DiagnosticSink& diagnostics_;
    CharacterOptions characterOptions_;
    std::size_t index_ = 0;
    bool ok_ = true;
};

} // namespace

ConditionalEngine::ConditionalEngine(DiagnosticSink& diagnostics,
                                     Evaluator evaluator,
                                     DefinedCheck definedCheck)
    : diagnostics_(diagnostics),
      evaluator_(std::move(evaluator)),
      definedCheck_(std::move(definedCheck)) {}

bool ConditionalEngine::active() const {
    return stack_.empty() || stack_.back().thisActive;
}

bool ConditionalEngine::hasOpenFrames() const { return !stack_.empty(); }

ConditionalEngine::Frame& ConditionalEngine::top() { return stack_.back(); }

const ConditionalEngine::Frame& ConditionalEngine::top() const {
    return stack_.back();
}

void ConditionalEngine::pushFrame(bool condition, const SourceLocation& loc) {
    const bool parentActive = active();
    Frame frame;
    frame.parentActive = parentActive;
    frame.thisActive = parentActive && condition;
    frame.anyTaken = frame.thisActive;
    frame.loc = loc;
    stack_.push_back(frame);
}

void ConditionalEngine::onIf(const std::vector<Token>& expression,
                             const SourceLocation& loc) {
    const bool parentActive = active();
    bool condition = false;
    if (parentActive) {
        condition = evaluator_ ? evaluator_(expression, loc) : false;
    }
    pushFrame(condition, loc);
}

void ConditionalEngine::onIfdef(std::string_view name, bool negate,
                                const SourceLocation& loc) {
    const bool parentActive = active();
    bool condition = false;
    if (parentActive && definedCheck_) {
        condition = definedCheck_(name);
    }
    pushFrame(negate ? !condition : condition, loc);
}

void ConditionalEngine::onElif(const std::vector<Token>& expression,
                               const SourceLocation& loc) {
    if (stack_.empty()) {
        diagnostics_.error(loc, "'#elif' without '#if'");
        return;
    }
    Frame& frame = top();
    if (frame.seenElse) {
        diagnostics_.error(loc, "'#elif' after '#else'");
        return;
    }
    if (!frame.parentActive || frame.anyTaken) {
        frame.thisActive = false;
        return;
    }
    const bool condition = evaluator_ ? evaluator_(expression, loc) : false;
    frame.thisActive = condition;
    frame.anyTaken = frame.anyTaken || condition;
}

void ConditionalEngine::onElifdef(std::string_view name, bool negate,
                                  const SourceLocation& loc) {
    if (stack_.empty()) {
        diagnostics_.error(loc, "'#elifdef' without '#if'");
        return;
    }
    Frame& frame = top();
    if (frame.seenElse) {
        diagnostics_.error(loc, "'#elifdef' after '#else'");
        return;
    }
    if (!frame.parentActive || frame.anyTaken) {
        frame.thisActive = false;
        return;
    }
    bool condition = false;
    if (definedCheck_) {
        condition = definedCheck_(name);
    }
    if (negate) {
        condition = !condition;
    }
    frame.thisActive = condition;
    frame.anyTaken = frame.anyTaken || condition;
}

void ConditionalEngine::onElse(const SourceLocation& loc) {
    if (stack_.empty()) {
        diagnostics_.error(loc, "'#else' without '#if'");
        return;
    }
    Frame& frame = top();
    if (frame.seenElse) {
        diagnostics_.error(loc, "'#else' after '#else'");
        return;
    }
    frame.thisActive = frame.parentActive && !frame.anyTaken;
    frame.anyTaken = true;
    frame.seenElse = true;
}

void ConditionalEngine::onEndif(const SourceLocation& loc) {
    if (stack_.empty()) {
        diagnostics_.error(loc, "'#endif' without '#if'");
        return;
    }
    stack_.pop_back();
}

void ConditionalEngine::reportUnclosed() {
    for (const auto& frame : stack_) {
        diagnostics_.error(frame.loc, "unterminated #if");
    }
}

bool ConditionalEngine::evaluateExpression(
    const std::vector<Token>& tokens, const SourceLocation& loc,
    DiagnosticSink& diagnostics, const CharacterOptions& characterOptions) {
    if (tokens.empty()) {
        diagnostics.error(loc, "#if with no expression");
        return false;
    }
    ExpressionParser parser(tokens, diagnostics, characterOptions);
    return parser.evaluate();
}

} // namespace crucible
