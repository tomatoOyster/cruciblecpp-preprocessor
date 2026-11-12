#include "Tokenizer.h"

#include "Preprocessor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>

namespace crucible {
namespace {

bool isAsciiAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isDigitChar(char c) { return c >= '0' && c <= '9'; }

bool isIdentifierStart(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return c == '_' || isAsciiAlpha(c) || u >= 0x80;
}

bool isIdentifierCont(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return isIdentifierStart(c) || isDigitChar(c) || u >= 0x80;
}

bool isHexDigit(char c) {
    return isDigitChar(c) || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

struct StringStart {
    bool matched = false;
    bool raw = false;
    bool character = false;
    std::size_t quotePos = 0;
};

StringStart detectStringStart(std::string_view s, std::size_t i) {
    StringStart result;
    const std::size_t n = s.size();

    if (i + 4 <= n && s.compare(i, 4, "u8R\"") == 0) {
        return {true, true, false, i + 3};
    }
    if (i + 3 <= n &&
        (s.compare(i, 3, "uR\"") == 0 || s.compare(i, 3, "UR\"") == 0 ||
         s.compare(i, 3, "LR\"") == 0)) {
        return {true, true, false, i + 2};
    }
    if (i + 2 <= n && s.compare(i, 2, "R\"") == 0) {
        return {true, true, false, i + 1};
    }

    if (i + 3 <= n && s.compare(i, 3, "u8\"") == 0) {
        return {true, false, false, i + 2};
    }
    if (i + 2 <= n &&
        (s.compare(i, 2, "u\"") == 0 || s.compare(i, 2, "U\"") == 0 ||
         s.compare(i, 2, "L\"") == 0)) {
        return {true, false, false, i + 1};
    }
    if (i < n && s[i] == '"') {
        return {true, false, false, i};
    }

    if (i + 3 <= n && s.compare(i, 3, "u8'") == 0) {
        return {true, false, true, i + 2};
    }
    if (i + 2 <= n &&
        (s.compare(i, 2, "u'") == 0 || s.compare(i, 2, "U'") == 0 ||
         s.compare(i, 2, "L'") == 0)) {
        return {true, false, true, i + 1};
    }
    if (i < n && s[i] == '\'') {
        return {true, false, true, i};
    }
    return result;
}

std::string canonicalPunct(std::string_view text) {
    if (text == "<%") {
        return "{";
    }
    if (text == "%>") {
        return "}";
    }
    if (text == "<:") {
        return "[";
    }
    if (text == ":>") {
        return "]";
    }
    if (text == "%:") {
        return "#";
    }
    if (text == "%:%:") {
        return "##";
    }
    return std::string(text);
}

const std::array<std::string_view, 44> kPunctuators = {
    "%:%:", ">>=", "<<=", "<=>", "->*", "...", "##",  "::",  "->",  "++",
    "--",   "<<",  ">>",  "<=",  ">=",  "==",  "!=",  "&&",  "||",  "+=",
    "-=",   "*=",  "/=",  "%=",  "&=",  "|=",  "^=",  ".*",  "<%",  "%>",
    "<:",   ":>",  "%:",  "#",   "{",   "}",   "[",   "]",   "(",   ")",
    ";",    ":",   "?",   "."};

const std::array<std::string_view, 24> kSinglePunct = {
    "+", "-", "*", "/", "%", "^", "&", "|", "~", "!", "=", "<",
    ">", ",", "#", "(", ")", "[", "]", "{", "}", ";", ":", "?"};

bool isSingleCharPunct(char c) {
    for (const auto p : kSinglePunct) {
        if (p.size() == 1 && p[0] == c) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<Token> Tokenizer::tokenize(
    std::shared_ptr<const SourceFile> file, DiagnosticSink* diagnostics,
    bool keepComments) const {
    std::vector<Token> tokens;
    if (!file) {
        return tokens;
    }

    const std::string& s = file->text;
    const std::size_t n = s.size();

    if (diagnostics) {
        std::uint32_t scanLine = 1;
        std::size_t lineStart = 0;
        for (std::size_t p = 0; p + 2 < n; ++p) {
            if (s[p] == '\n') {
                ++scanLine;
                lineStart = p + 1;
                continue;
            }
            if (s[p] != '?' || s[p + 1] != '?') {
                continue;
            }
            char spelling = s[p + 2];
            if (spelling != '=' && spelling != '/' && spelling != '\'' &&
                spelling != '(' && spelling != ')' && spelling != '!' &&
                spelling != '<' && spelling != '>' && spelling != '-') {
                continue;
            }
            SourceLocation loc;
            loc.file = file;
            loc.line = scanLine;
            loc.column = static_cast<std::uint32_t>(p - lineStart + 1);
            loc.offset = p;
            std::string trigraph = "??";
            trigraph.push_back(spelling);
            diagnostics->warning(
                loc,
                "trigraph '" + trigraph +
                    "' ignored, use '-trigraphs' to enable",
                "-Wtrigraphs");
        }
    }

    std::size_t i = 0;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    bool atLineStart = true;
    bool pendingSpace = false;

    auto makeToken = [&](TokenKind kind, std::string text, std::uint32_t tokLine,
                         std::uint32_t tokColumn, std::size_t offset,
                         bool leading, bool atStart) {
        Token token;
        token.kind = kind;
        token.text = std::move(text);
        token.loc.file = file;
        const auto spliceCount =
            std::upper_bound(file->spliceOffsets.begin(),
                             file->spliceOffsets.end(), offset) -
            file->spliceOffsets.begin();
        token.loc.line =
            tokLine + static_cast<std::uint32_t>(spliceCount);
        token.loc.column = tokColumn;
        token.loc.offset = offset;
        token.leadingSpace = leading;
        token.atLineStart = atStart;
        return token;
    };

    auto pushNewline = [&](std::uint32_t nlLine, std::uint32_t nlColumn,
                           std::size_t offset) {
        tokens.push_back(
            makeToken(TokenKind::Newline, "\n", nlLine, nlColumn, offset,
                      false, true));
        atLineStart = true;
        pendingSpace = false;
    };

    while (i < n) {
        const char c = s[i];

        // Contextual header-name lexing: inside #include/#include_next <...>
        // the content is a header-name, not ordinary tokens. This keeps
        // Windows backslashes and "//" intact and avoids bogus diagnostics.
        if (atLineStart && c == '#') {
            std::size_t p = i + 1;
            while (p < n && (s[p] == ' ' || s[p] == '\t')) {
                ++p;
            }
            const std::size_t nameStart = p;
            while (p < n && isIdentifierCont(s[p])) {
                ++p;
            }
            const std::string_view directive(s.data() + nameStart,
                                             p - nameStart);
            if (directive == "include" || directive == "include_next") {
                std::size_t q = p;
                while (q < n && (s[q] == ' ' || s[q] == '\t')) {
                    ++q;
                }
                if (q < n && s[q] == '<') {
                    const std::size_t close = s.find('>', q + 1);
                    const std::size_t newline = s.find('\n', q + 1);
                    if (close != std::string::npos &&
                        (newline == std::string::npos || close < newline)) {
                        const std::uint32_t startLine = line;
                        const std::uint32_t startColumn = column;
                        const std::uint32_t consumed =
                            static_cast<std::uint32_t>((close + 1) - i);
                        tokens.push_back(makeToken(
                            TokenKind::Punctuator, "#", startLine,
                            startColumn, i, pendingSpace, true));
                        tokens.push_back(makeToken(
                            TokenKind::Identifier,
                            std::string(directive), startLine,
                            startColumn + static_cast<std::uint32_t>(
                                               nameStart - i),
                            nameStart, nameStart > i + 1, false));
                        tokens.push_back(makeToken(
                            TokenKind::HeaderName,
                            s.substr(q, close - q + 1), startLine,
                            startColumn + static_cast<std::uint32_t>(q - i), q,
                            q > p, false));
                        atLineStart = false;
                        pendingSpace = false;
                        i = close + 1;
                        column = startColumn + consumed;
                        continue;
                    }
                }
            }
        }

        if (c == '\n') {
            pushNewline(line, column, i);
            ++i;
            ++line;
            column = 1;
            continue;
        }

        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            pendingSpace = true;
            ++i;
            ++column;
            continue;
        }

        if (c == '/' && i + 1 < n && s[i + 1] == '/') {
            const std::size_t start = i;
            const std::uint32_t startLine = line;
            const std::uint32_t startColumn = column;
            std::size_t j = i + 2;
            while (j < n && s[j] != '\n') {
                ++j;
            }
            if (keepComments) {
                tokens.push_back(makeToken(
                    TokenKind::Comment, s.substr(start, j - start), startLine,
                    startColumn, start, pendingSpace, atLineStart));
                atLineStart = false;
            }
            column += static_cast<std::uint32_t>(j - start);
            i = j;
            pendingSpace = true;
            continue;
        }

        if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            const std::size_t start = i;
            std::size_t j = i + 2;
            bool closed = false;
            while (j < n) {
                if (s[j] == '*' && j + 1 < n && s[j + 1] == '/') {
                    j += 2;
                    closed = true;
                    break;
                }
                ++j;
            }
            if (!closed && diagnostics) {
                SourceLocation loc;
                loc.file = file;
                loc.line = line;
                loc.column = column;
                loc.offset = start;
                diagnostics->error(loc, "unterminated block comment");
            }

            // Walk the comment to preserve newline structure and, optionally,
            // the comment text. Splitting at newlines keeps directive line
            // tracking correct.
            std::size_t k = start;
            std::size_t segmentStart = start;
            std::uint32_t currentLine = line;
            std::uint32_t currentColumn = column;
            std::uint32_t segmentLine = line;
            std::uint32_t segmentColumn = column;
            bool nestedCommentWarned = false;

            while (k < j) {
                if (!nestedCommentWarned && diagnostics && s[k] == '/' &&
                    k + 1 < j && s[k + 1] == '*') {
                    SourceLocation loc;
                    loc.file = file;
                    loc.line = currentLine;
                    loc.column = currentColumn;
                    loc.offset = k;
                    diagnostics->warning(loc, "'/*' within comment",
                                         "-Wcomment");
                    nestedCommentWarned = true;
                }
                if (s[k] == '\n') {
                    if (keepComments && segmentStart < k) {
                        tokens.push_back(makeToken(
                            TokenKind::Comment,
                            s.substr(segmentStart, k - segmentStart),
                            segmentLine, segmentColumn, segmentStart,
                            pendingSpace, atLineStart));
                        atLineStart = false;
                    }
                    Token commentNewline =
                        makeToken(TokenKind::Newline, "\n", currentLine,
                                  currentColumn, k, false, true);
                    commentNewline.softNewline = true;
                    tokens.push_back(std::move(commentNewline));
                    atLineStart = true;
                    pendingSpace = false;
                    ++currentLine;
                    currentColumn = 1;
                    ++k;
                    segmentStart = k;
                    segmentLine = currentLine;
                    segmentColumn = currentColumn;
                    continue;
                }
                ++k;
                ++currentColumn;
            }
            if (keepComments && segmentStart < j) {
                tokens.push_back(makeToken(
                    TokenKind::Comment, s.substr(segmentStart, j - segmentStart),
                    segmentLine, segmentColumn, segmentStart, pendingSpace,
                    atLineStart));
                atLineStart = false;
            }
            line = currentLine;
            column = currentColumn;
            i = j;
            pendingSpace = true;
            continue;
        }

        const StringStart literal = detectStringStart(s, i);
        if (literal.matched) {
            const std::size_t start = i;
            const std::uint32_t startLine = line;
            const std::uint32_t startColumn = column;
            const std::size_t quote = literal.quotePos;
            std::size_t end = n;
            bool closed = false;

            if (literal.raw) {
                std::size_t p = quote + 1;
                std::string delimiter;
                bool delimiterOk = true;
                while (p < n && s[p] != '(') {
                    const char d = s[p];
                    if (d == ' ' || d == '\t' || d == '\r' || d == '\n' ||
                        d == '\\' || d == ')' || delimiter.size() >= 16) {
                        delimiterOk = false;
                        break;
                    }
                    delimiter.push_back(d);
                    ++p;
                }
                if (p >= n || s[p] != '(' || !delimiterOk) {
                    if (diagnostics) {
                        SourceLocation loc;
                        loc.file = file;
                        loc.line = startLine;
                        loc.column = startColumn;
                        loc.offset = start;
                        diagnostics->error(loc, "invalid raw string delimiter");
                    }
                    // Best effort: consume to the next quote or newline.
                    end = start + 1;
                    while (end < n && s[end] != '"' && s[end] != '\n') {
                        ++end;
                    }
                    if (end < n && s[end] == '"') {
                        ++end;
                    }
                } else {
                    const std::string closing = ")" + delimiter + "\"";
                    const std::size_t close = s.find(closing, p + 1);
                    if (close == std::string::npos) {
                        if (diagnostics) {
                            SourceLocation loc;
                            loc.file = file;
                            loc.line = startLine;
                            loc.column = startColumn;
                            loc.offset = start;
                            diagnostics->error(loc,
                                               "unterminated raw string literal");
                        }
                        end = n;
                    } else {
                        end = close + closing.size();
                        closed = true;
                    }
                }
            } else if (literal.character) {
                std::size_t j = quote + 1;
                if (j < n && s[j] == '\'') {
                    if (diagnostics) {
                        SourceLocation loc;
                        loc.file = file;
                        loc.line = startLine;
                        loc.column = startColumn;
                        loc.offset = start;
                        diagnostics->error(loc, "empty character literal");
                    }
                }
                while (j < n) {
                    if (s[j] == '\\' && j + 1 < n) {
                        j += 2;
                        continue;
                    }
                    if (s[j] == '\'') {
                        end = j + 1;
                        closed = true;
                        break;
                    }
                    if (s[j] == '\n') {
                        end = j;
                        break;
                    }
                    ++j;
                }
            } else {
                std::size_t j = quote + 1;
                while (j < n) {
                    if (s[j] == '\\' && j + 1 < n) {
                        j += 2;
                        continue;
                    }
                    if (s[j] == '"') {
                        end = j + 1;
                        closed = true;
                        break;
                    }
                    if (s[j] == '\n') {
                        end = j;
                        break;
                    }
                    ++j;
                }
            }

            // A user-defined literal suffix is part of the same preprocessing
            // token: "text"_suffix, L"text"_suffix, 'c'_suffix, R"(...)"_suffix.
            if (closed && end < n && isIdentifierStart(s[end])) {
                std::size_t suffixEnd = end;
                while (suffixEnd < n && isIdentifierCont(s[suffixEnd])) {
                    ++suffixEnd;
                }
                end = suffixEnd;
            }

            if (end <= i) {
                end = i + 1;
            }
            if (!closed && !literal.raw && diagnostics) {
                SourceLocation loc;
                loc.file = file;
                loc.line = startLine;
                loc.column = startColumn;
                loc.offset = start;
                diagnostics->error(
                    loc, literal.character ? "unterminated character literal"
                                           : "unterminated string literal");
            }
            Token token = makeToken(
                literal.character ? TokenKind::CharLiteral
                                  : TokenKind::StringLiteral,
                s.substr(start, end - start), startLine, startColumn, start,
                pendingSpace, atLineStart);
            tokens.push_back(std::move(token));
            atLineStart = false;
            pendingSpace = false;

            // Advance line/column over the literal, including embedded raw
            // string newlines.
            for (std::size_t k = start; k < end; ++k) {
                if (s[k] == '\n') {
                    ++line;
                    column = 1;
                } else {
                    ++column;
                }
            }
            atLineStart = false;
            i = end;
            continue;
        }

        if (isIdentifierStart(c) || (c == '\\' && i + 1 < n &&
                                    (s[i + 1] == 'u' || s[i + 1] == 'U'))) {
            const std::size_t start = i;
            const std::uint32_t startLine = line;
            const std::uint32_t startColumn = column;
            while (i < n) {
                if (isIdentifierCont(s[i])) {
                    ++i;
                    ++column;
                    continue;
                }
                if (s[i] == '\\' && i + 1 < n &&
                    (s[i + 1] == 'u' || s[i + 1] == 'U')) {
                    const bool universal = s[i + 1] == 'u';
                    const std::size_t digits = universal ? 4 : 8;
                    if (i + 2 + digits <= n) {
                        bool valid = true;
                        for (std::size_t k = 0; k < digits; ++k) {
                            if (!isHexDigit(s[i + 2 + k])) {
                                valid = false;
                                break;
                            }
                        }
                        if (valid) {
                            i += 2 + digits;
                            column += static_cast<std::uint32_t>(2 + digits);
                            continue;
                        }
                    }
                    if (diagnostics) {
                        SourceLocation loc;
                        loc.file = file;
                        loc.line = line;
                        loc.column = column;
                        loc.offset = i;
                        diagnostics->error(loc,
                                           "invalid universal character name");
                    }
                    // Consume at least the backslash so the outer loop always
                    // makes progress, even for malformed UCNs.
                    ++i;
                    ++column;
                    break;
                }
                break;
            }
            tokens.push_back(makeToken(TokenKind::Identifier,
                                       s.substr(start, i - start), startLine,
                                       startColumn, start, pendingSpace,
                                       atLineStart));
            atLineStart = false;
            pendingSpace = false;
            continue;
        }

        if (isDigitChar(c) || (c == '.' && i + 1 < n && isDigitChar(s[i + 1]))) {
            const std::size_t start = i;
            const std::uint32_t startLine = line;
            const std::uint32_t startColumn = column;
            ++i;
            ++column;
            while (i < n) {
                const char d = s[i];
                if (isIdentifierCont(d) || d == '.' || d == '\'') {
                    ++i;
                    ++column;
                    continue;
                }
                if ((d == '+' || d == '-') && i > start) {
                    const char prev = s[i - 1];
                    if (prev == 'e' || prev == 'E' || prev == 'p' ||
                        prev == 'P') {
                        ++i;
                        ++column;
                        continue;
                    }
                }
                break;
            }
            tokens.push_back(makeToken(TokenKind::PPNumber,
                                       s.substr(start, i - start), startLine,
                                       startColumn, start, pendingSpace,
                                       atLineStart));
            atLineStart = false;
            pendingSpace = false;
            continue;
        }

        bool punctMatched = false;
        for (const auto p : kPunctuators) {
            if (i + p.size() <= n && s.compare(i, p.size(), p) == 0) {
                const std::size_t start = i;
                const std::uint32_t startLine = line;
                const std::uint32_t startColumn = column;
                tokens.push_back(makeToken(
                    TokenKind::Punctuator, canonicalPunct(p), startLine,
                    startColumn, start, pendingSpace, atLineStart));
                atLineStart = false;
                pendingSpace = false;
                i += p.size();
                column += static_cast<std::uint32_t>(p.size());
                punctMatched = true;
                break;
            }
        }
        if (punctMatched) {
            continue;
        }

        if (isSingleCharPunct(c)) {
            const std::size_t start = i;
            const std::uint32_t startLine = line;
            const std::uint32_t startColumn = column;
            tokens.push_back(makeToken(TokenKind::Punctuator,
                                       std::string(1, c), startLine, startColumn,
                                       start, pendingSpace, atLineStart));
            atLineStart = false;
            pendingSpace = false;
            ++i;
            ++column;
            continue;
        }

        if (diagnostics) {
            SourceLocation loc;
            loc.file = file;
            loc.line = line;
            loc.column = column;
            loc.offset = i;
            diagnostics->error(loc, "unexpected character in source file");
        }
        ++i;
        ++column;
    }

    return tokens;
}

std::optional<Token> Tokenizer::tokenizeSingle(std::string_view text,
                                               const SourceLocation& loc) const {
    if (text.empty()) {
        return std::nullopt;
    }
    // Leading/trailing whitespace is not a valid single-token paste result.
    // Whitespace *inside* a string literal is legal and must not be rejected.
    if (text.front() == ' ' || text.front() == '\t' ||
        text.front() == '\n' || text.front() == '\r' ||
        text.back() == ' ' || text.back() == '\t' ||
        text.back() == '\n' || text.back() == '\r') {
        return std::nullopt;
    }

    auto file = std::make_shared<SourceFile>();
    file->displayPath = "<paste>";
    file->physicalPath = "<paste>";
    file->text = std::string(text);
    file->lineOffsets.push_back(0);

    std::vector<Token> tokens = tokenize(file, nullptr, false);
    std::vector<Token> filtered;
    for (auto& token : tokens) {
        if (token.kind != TokenKind::Newline &&
            token.kind != TokenKind::Comment) {
            filtered.push_back(std::move(token));
        }
    }
    if (filtered.size() != 1) {
        return std::nullopt;
    }
    const bool digraph =
        text == "<:" || text == ":>" || text == "<%" || text == "%>" ||
        text == "%:" || text == "%:%:";
    if (filtered[0].text != text && !digraph) {
        return std::nullopt;
    }
    filtered[0].loc = loc;
    return filtered[0];
}

bool Tokenizer::parseHeaderName(std::string_view text, std::size_t& pos,
                                std::string& name, bool& angled) {
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' ||
            text[pos] == '\r')) {
        ++pos;
    }
    if (pos >= text.size()) {
        return false;
    }

    if (text[pos] == '"') {
        const std::size_t end = text.find('"', pos + 1);
        if (end == std::string_view::npos) {
            return false;
        }
        name = std::string(text.substr(pos + 1, end - pos - 1));
        angled = false;
        pos = end + 1;
        return true;
    }
    if (text[pos] == '<') {
        const std::size_t end = text.find('>', pos + 1);
        if (end == std::string_view::npos) {
            return false;
        }
        name = std::string(text.substr(pos + 1, end - pos - 1));
        angled = true;
        pos = end + 1;
        return true;
    }
    return false;
}

} // namespace crucible
