#include "Token.h"

namespace crucible {

const char* tokenKindName(TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier:
        return "identifier";
    case TokenKind::PPNumber:
        return "number";
    case TokenKind::StringLiteral:
        return "string-literal";
    case TokenKind::CharLiteral:
        return "character-literal";
    case TokenKind::Punctuator:
        return "punctuator";
    case TokenKind::HeaderName:
        return "header-name";
    case TokenKind::Comment:
        return "comment";
    case TokenKind::Newline:
        return "newline";
    case TokenKind::Placemarker:
        return "placemarker";
    case TokenKind::EndOfFile:
        return "end-of-file";
    }
    return "token";
}

bool tokensEquivalent(const Token& a, const Token& b) {
    return a.kind == b.kind && a.text == b.text;
}

bool isHidden(const Token& token, std::string_view macroName) {
    if (!token.hideSet) {
        return false;
    }
    return token.hideSet->find(std::string(macroName)) != token.hideSet->end();
}

} // namespace crucible
