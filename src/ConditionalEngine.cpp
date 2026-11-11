#include "ConditionalEngine.h"

#include "Preprocessor.h"

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace crucibe {
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

bool decodeEscape(std::string_view text, std::size_t& i, unsigned& value) {
    // i points at the backslash.
    if (i + 1 >= text.size()) {
        return false;
    }
    const char c = text[i + 1];
    i += 2;
    switch (c) {
    case 'a':
        value = 7;
        return true;
    case 'b':
        value = 8;
        return true;
    case 'f':
        value = 12;
        return true;
    case 'n':
        value = 10;
        return true;
    case 'r':
        value = 13;
        return true;
    case 't':
        value = 9;
        return true;
    case 'v':
        value = 11;
        return true;
    case '\\':
        value = '\\';
        return true;
    case '\'':
        value = '\'';
        return true;
    case '"':
        value = '"';
        return true;
    case '?':
        value = '?';
        return true;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7': {
        unsigned result = static_cast<unsigned>(c - '0');
        int count = 1;
        while (count < 3 && i < text.size() && text[i] >= '0' &&
               text[i] <= '7') {
            result = result * 8 + static_cast<unsigned>(text[i] - '0');
            ++i;
            ++count;
        }
        value = result;
        return true;
    }
    case 'x': {
        unsigned result = 0;
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
            result = result * 16 + static_cast<unsigned>(digit);
            any = true;
            ++i;
        }
        if (!any) {
            return false;
        }
        value = result;
        return true;
    }
    case 'u':
    case 'U': {
        const std::size_t digits = c == 'u' ? 4 : 8;
        if (i + digits > text.size()) {
            return false;
        }
        unsigned result = 0;
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
                return false;
            }
            result = result * 16 + static_cast<unsigned>(digit);
        }
        i += digits;
        value = result;
        return true;
    }
    default:
        value = static_cast<unsigned char>(c);
        return true;
    }
}

bool parseCharacterLiteral(std::string_view text, PPValue& out,
                           std::string& error) {
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

    std::string_view body = text.substr(quote + 1, end - quote - 1);
    if (body.empty()) {
        error = "empty character literal";
        return false;
    }

    std::uint64_t value = 0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < body.size();) {
        unsigned byte = 0;
        if (body[i] == '\\') {
            if (!decodeEscape(body, i, byte)) {
                error = "invalid escape sequence in character literal";
                return false;
            }
        } else {
            byte = static_cast<unsigned char>(body[i]);
            ++i;
        }
        value = (value << 8) | (byte & 0xFFu);
        ++count;
    }
    if (count == 0) {
        error = "empty character literal";
        return false;
    }
    out.bits = value;
    out.isUnsigned = false;
    return true;
}

class ExpressionParser {
public:
    ExpressionParser(const std::vector<Token>& tokens, DiagnosticSink& diagnostics)
        : tokens_(tokens), diagnostics_(diagnostics) {}

    bool evaluate() {
        if (tokens_.empty()) {
            if (!tokens_.empty()) {
                // unreachable; keeps the empty-expression path explicit
            }
            diagnostics_.error({}, "empty #if expression");
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
                    diagnostics_.error(tokens_[index_ - 1].loc,
                                       "shift count is too large");
                    ok_ = false;
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
            if (!eval) {
                return makeSigned(0);
            }
            PPValue value;
            std::string error;
            if (!parseCharacterLiteral(token.text, value, error)) {
                diagnostics_.error(token.loc, error);
                ok_ = false;
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
        diagnostics_.error(loc, "#elif without matching #if");
        return;
    }
    Frame& frame = top();
    if (frame.seenElse) {
        diagnostics_.error(loc, "#elif after #else");
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
        diagnostics_.error(loc, "#elifdef without matching #if");
        return;
    }
    Frame& frame = top();
    if (frame.seenElse) {
        diagnostics_.error(loc, "#elifdef after #else");
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
        diagnostics_.error(loc, "#else without matching #if");
        return;
    }
    Frame& frame = top();
    if (frame.seenElse) {
        diagnostics_.error(loc, "duplicate #else");
        return;
    }
    frame.thisActive = frame.parentActive && !frame.anyTaken;
    frame.anyTaken = true;
    frame.seenElse = true;
}

void ConditionalEngine::onEndif(const SourceLocation& loc) {
    if (stack_.empty()) {
        diagnostics_.error(loc, "#endif without matching #if");
        return;
    }
    stack_.pop_back();
}

void ConditionalEngine::reportUnclosed() {
    for (const auto& frame : stack_) {
        diagnostics_.error(frame.loc, "unterminated conditional directive");
    }
}

bool ConditionalEngine::evaluateExpression(
    const std::vector<Token>& tokens, const SourceLocation& loc,
    DiagnosticSink& diagnostics) {
    if (tokens.empty()) {
        diagnostics.error(loc, "empty #if expression");
        return false;
    }
    ExpressionParser parser(tokens, diagnostics);
    return parser.evaluate();
}

} // namespace crucibe
