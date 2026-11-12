#include "SpecialOperator.h"

#include "Preprocessor.h"
#include "Tokenizer.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>
#include <utility>

namespace crucible {
namespace {

Token makeToken(TokenKind kind, std::string text, const SourceLocation& loc) {
    Token token;
    token.kind = kind;
    token.text = std::move(text);
    token.loc = loc;
    return token;
}

std::string quoteForDiagnostic(std::string_view value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

} // namespace

SpecialOperator::SpecialOperator(DiagnosticSink& diagnostics)
    : diagnostics_(diagnostics) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    static constexpr std::array<const char*, 12> months = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char dateBuffer[32] = {};
    std::snprintf(dateBuffer, sizeof(dateBuffer), "%s %2d %04d",
                  months[static_cast<std::size_t>(local.tm_mon)],
                  local.tm_mday, local.tm_year + 1900);
    date_ = dateBuffer;

    char timeBuffer[16] = {};
    std::snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d:%02d",
                  local.tm_hour, local.tm_min, local.tm_sec);
    time_ = timeBuffer;
}

bool SpecialOperator::isBuiltinName(std::string_view name) {
    return name == "__FILE__" || name == "__LINE__" || name == "__DATE__" ||
           name == "__TIME__" || name == "__TIMESTAMP__" ||
           name == "__COUNTER__" || name == "__cplusplus" ||
           name == "__STDC_HOSTED__" ||
           name == "__STDCPP_DEFAULT_NEW_ALIGNMENT__" ||
           name == "__INCLUDE_LEVEL__" || name == "__BASE_FILE__";
}

std::optional<std::vector<Token>> SpecialOperator::expandBuiltin(
    const Token& token, const BuiltinContext& context) const {
    if (token.kind != TokenKind::Identifier) {
        return std::nullopt;
    }

    auto number = [&](std::int64_t value) {
        return std::vector<Token>{
            makeToken(TokenKind::PPNumber, std::to_string(value), token.loc)};
    };
    auto string = [&](std::string_view value) {
        return std::vector<Token>{makeToken(TokenKind::StringLiteral,
                                           quoteForDiagnostic(value),
                                           token.loc)};
    };

    if (token.text == "__FILE__") {
        return string(context.file);
    }
    if (token.text == "__LINE__") {
        return number(token.loc.line == 0 ? 1 : token.loc.line);
    }
    if (token.text == "__DATE__") {
        diagnostics_.warning(
            token.loc, "macro '__DATE__' might prevent reproducible builds",
            "-Wdate-time");
        return string(date_);
    }
    if (token.text == "__TIME__") {
        diagnostics_.warning(
            token.loc, "macro '__TIME__' might prevent reproducible builds",
            "-Wdate-time");
        return string(time_);
    }
    if (token.text == "__TIMESTAMP__") {
        diagnostics_.warning(
            token.loc,
            "macro '__TIMESTAMP__' might prevent reproducible builds",
            "-Wdate-time");
        return string(date_ + " " + time_);
    }
    if (token.text == "__COUNTER__") {
        if (!context.counter) {
            return number(0);
        }
        return number((*context.counter)++);
    }
    if (token.text == "__cplusplus") {
        return number(context.stdVersion);
    }
    if (token.text == "__STDC_HOSTED__") {
        return number(1);
    }
    if (token.text == "__STDCPP_DEFAULT_NEW_ALIGNMENT__") {
        return number(16);
    }
    if (token.text == "__INCLUDE_LEVEL__") {
        return number(context.includeLevel);
    }
    if (token.text == "__BASE_FILE__") {
        return string(context.baseFile);
    }
    return std::nullopt;
}

Token SpecialOperator::stringize(const std::vector<Token>& rawArgument,
                                 const SourceLocation& loc) const {
    std::string spelling;
    bool needSpace = false;
    bool first = true;

    for (const auto& token : rawArgument) {
        if (token.kind == TokenKind::Newline ||
            token.kind == TokenKind::Comment) {
            needSpace = true;
            continue;
        }
        if (token.kind == TokenKind::Placemarker) {
            continue;
        }
        if (!first && (needSpace || token.leadingSpace)) {
            spelling.push_back(' ');
        }
        spelling.append(token.text);
        needSpace = false;
        first = false;
    }

    std::string literal = "\"";
    literal.reserve(spelling.size() + 2);
    for (char c : spelling) {
        if (c == '\\' || c == '"') {
            literal.push_back('\\');
        }
        literal.push_back(c);
    }
    literal.push_back('"');
    return makeToken(TokenKind::StringLiteral, std::move(literal), loc);
}

std::optional<Token> SpecialOperator::paste(const Token& left,
                                            const Token& right,
                                            const SourceLocation& loc) {
    if (left.kind == TokenKind::Placemarker &&
        right.kind == TokenKind::Placemarker) {
        return makeToken(TokenKind::Placemarker, "", loc);
    }
    if (left.kind == TokenKind::Placemarker) {
        return right;
    }
    if (right.kind == TokenKind::Placemarker) {
        return left;
    }

    const std::string combined = left.text + right.text;
    Tokenizer tokenizer;
    auto pasted = tokenizer.tokenizeSingle(combined, loc);
    if (!pasted.has_value()) {
        diagnostics_.error(loc, "pasting \"" + left.text + "\" and \"" +
                                   right.text +
                                   "\" does not form a valid token");
        return std::nullopt;
    }
    pasted->leadingSpace = left.leadingSpace;
    return pasted;
}

std::string SpecialOperator::destringize(std::string_view literal) const {
    if (literal.size() >= 2 && literal.front() == '"' &&
        literal.back() == '"') {
        literal = literal.substr(1, literal.size() - 2);
    }
    std::string out;
    out.reserve(literal.size());
    for (std::size_t i = 0; i < literal.size(); ++i) {
        if (literal[i] == '\\' && i + 1 < literal.size()) {
            const char next = literal[i + 1];
            if (next == '\\' || next == '"') {
                out.push_back(next);
                ++i;
                continue;
            }
        }
        out.push_back(literal[i]);
    }
    return out;
}

} // namespace crucible
