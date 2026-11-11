#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace crucible {

enum class TokenKind {
    Identifier,
    PPNumber,
    StringLiteral,
    CharLiteral,
    Punctuator,
    HeaderName,
    Comment,
    Newline,
    Placemarker,
    EndOfFile
};

struct SourceFile {
    std::string displayPath;
    std::string physicalPath;
    std::string text;
    std::vector<std::size_t> lineOffsets;
    // Output offsets at which a backslash-newline splice was removed. Used to
    // map logical lines back to physical source lines for diagnostics and
    // __LINE__.
    std::vector<std::size_t> spliceOffsets;
};

struct SourceLocation {
    std::shared_ptr<const SourceFile> file;
    std::string presumedFile;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    std::size_t offset = 0;

    bool valid() const { return file != nullptr; }
};

using HideSet = std::unordered_set<std::string>;

struct Token {
    TokenKind kind = TokenKind::Identifier;
    std::string text;
    SourceLocation loc;
    bool leadingSpace = false;
    bool atLineStart = false;
    bool fromMacro = false;
    // A newline that originated inside a block comment. It advances source
    // line numbers, but does not terminate a preprocessing directive that is
    // already in progress.
    bool softNewline = false;
    std::shared_ptr<const HideSet> hideSet;

    bool isPunct(std::string_view spelling) const {
        return kind == TokenKind::Punctuator && text == spelling;
    }

    bool isIdentifier(std::string_view spelling) const {
        return kind == TokenKind::Identifier && text == spelling;
    }

    bool isTrivia() const {
        return kind == TokenKind::Newline || kind == TokenKind::Comment;
    }
};

const char* tokenKindName(TokenKind kind);
bool tokensEquivalent(const Token& a, const Token& b);
bool isHidden(const Token& token, std::string_view macroName);

} // namespace crucible
