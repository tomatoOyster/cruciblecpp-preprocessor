#include "MacroExpander.h"

#include "Preprocessor.h"
#include "SpecialOperator.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace crucible {
namespace {

bool hasNonTrivia(const std::vector<Token>& tokens) {
    for (const auto& token : tokens) {
        if (!token.isTrivia()) {
            return true;
        }
    }
    return false;
}

Token makePlacemarker(const SourceLocation& loc, std::string text = {}) {
    Token token;
    token.kind = TokenKind::Placemarker;
    token.text = std::move(text);
    token.loc = loc;
    return token;
}

Token makeComma(const SourceLocation& loc) {
    Token token;
    token.kind = TokenKind::Punctuator;
    token.text = ",";
    token.loc = loc;
    return token;
}

struct WorkToken {
    Token token;
    bool replacementOperator = false;
};

std::string joinTokenText(const std::vector<Token>& tokens) {
    std::string text;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::Placemarker ||
            token.kind == TokenKind::Newline ||
            token.kind == TokenKind::Comment || token.text.empty()) {
            continue;
        }
        if (!text.empty()) {
            text.push_back(' ');
        }
        text += token.text;
        if (text.size() > 2000) {
            text += " ...";
            break;
        }
    }
    return text;
}

std::string sourceLineAt(const SourceLocation& loc) {
    if (!loc.file || loc.offset >= loc.file->text.size()) {
        return {};
    }
    const std::string& text = loc.file->text;
    const std::size_t start = text.rfind('\n', loc.offset);
    const std::size_t begin = start == std::string::npos ? 0 : start + 1;
    const std::size_t end = text.find('\n', loc.offset);
    std::string line = text.substr(
        begin, (end == std::string::npos ? text.size() : end) - begin);
    if (line.size() > 500) {
        line.resize(500);
        line += " ...";
    }
    return line;
}

} // namespace

MacroExpander::MacroExpander(MacroRegistry& registry,
                             DiagnosticSink& diagnostics,
                             SpecialOperator& special)
    : registry_(registry), diagnostics_(diagnostics), special_(special) {}

void MacroExpander::setBuiltinHandler(BuiltinHandler handler) {
    builtinHandler_ = std::move(handler);
}

void MacroExpander::setTraceHandlers(TraceStartHandler start,
                                      TraceEndHandler end,
                                      TraceStepHandler step) {
    traceStart_ = std::move(start);
    traceEnd_ = std::move(end);
    traceStep_ = std::move(step);
}

std::vector<MacroTraceArgument> MacroExpander::traceArguments(
    const std::vector<Argument>& arguments) const {
    std::vector<MacroTraceArgument> result;
    result.reserve(arguments.size());
    for (const auto& argument : arguments) {
        MacroTraceArgument trace;
        trace.raw = joinTokenText(argument.raw);
        if (argument.expandedReady) {
            trace.expanded = joinTokenText(argument.expanded);
        }
        result.push_back(std::move(trace));
    }
    return result;
}

int MacroExpander::beginTrace(const MacroDefinition& macro,
                              const Token& invocation,
                              const std::vector<Argument>& arguments,
                              std::string invocationText,
                              std::string sourceLine, int offset, int length,
                              bool functionLike) {
    MacroTraceStart info;
    info.id = nextTraceId_++;
    info.parentId = traceStack_.empty() ? -1 : traceStack_.back();
    info.macro = macro.name;
    info.invocation = invocation.loc;
    info.definition = macro.definitionLoc;
    info.invocationText = std::move(invocationText);
    info.sourceLine =
        sourceLine.empty() ? sourceLineAt(invocation.loc)
                           : std::move(sourceLine);
    info.definitionText = sourceLineAt(macro.definitionLoc);
    info.offset = offset;
    info.length = length;
    info.depth = expansionDepth_;
    info.functionLike = functionLike;
    info.arguments = traceArguments(arguments);
    traceStack_.push_back(info.id);
    if (traceStart_) {
        traceStart_(info);
    }
    return info.id;
}

void MacroExpander::endTrace(
    int id, const std::vector<Token>& replacement,
    const std::vector<Argument>& arguments) {
    if (traceEnd_) {
        traceEnd_(id, replacement, traceArguments(arguments));
    }
    if (!traceStack_.empty() && traceStack_.back() == id) {
        traceStack_.pop_back();
    }
}

void MacroExpander::traceStep(std::string operation, std::string before,
                              std::string after) {
    if (!traceStep_ || traceStack_.empty()) {
        return;
    }
    MacroTraceStep step;
    step.parentId = traceStack_.back();
    step.operation = std::move(operation);
    step.before = std::move(before);
    step.after = std::move(after);
    traceStep_(step);
}

bool MacroExpander::collectArguments(
    const std::deque<Token>& input, std::size_t openIndex,
    const MacroDefinition& macro, std::vector<Argument>& arguments,
    std::size_t& closeIndex) {
    int depth = 1;
    std::vector<Token> current;
    bool sawComma = false;
    std::optional<Token> pendingSeparator;

    auto pushArgument = [&](bool force) {
        if (!force && current.empty()) {
            return;
        }
        Argument argument;
        argument.raw = std::move(current);
        argument.empty = !hasNonTrivia(argument.raw);
        if (pendingSeparator.has_value()) {
            argument.separator = *pendingSeparator;
            argument.hasSeparator = true;
            pendingSeparator.reset();
        }
        arguments.push_back(std::move(argument));
        current.clear();
    };

    for (std::size_t i = openIndex + 1; i < input.size(); ++i) {
        const Token& token = input[i];
        if (token.kind == TokenKind::Comment) {
            // Comments are whitespace during macro argument collection. The
            // following token already carries leadingSpace=true.
            continue;
        }
        if (token.isPunct("(")) {
            ++depth;
            current.push_back(token);
            continue;
        }
        if (token.isPunct(")")) {
            --depth;
            if (depth == 0) {
                const bool needEmptyArgument =
                    hasNonTrivia(current) || sawComma ||
                    (arguments.empty() && !macro.params.empty());
                if (needEmptyArgument) {
                    pushArgument(true);
                }
                closeIndex = i;
                return true;
            }
            current.push_back(token);
            continue;
        }
        if (token.isPunct(",") && depth == 1) {
            pushArgument(true);
            pendingSeparator = token;
            sawComma = true;
            continue;
        }
        current.push_back(token);
    }
    return false;
}

void MacroExpander::applyHideSet(
    Token& token, const std::shared_ptr<const HideSet>& invocationHide,
    std::string_view macroName) const {
    auto hide = std::make_shared<HideSet>();
    if (invocationHide) {
        *hide = *invocationHide;
    }
    hide->insert(std::string(macroName));
    if (token.hideSet) {
        hide->insert(token.hideSet->begin(), token.hideSet->end());
    }
    token.hideSet = std::move(hide);
    token.fromMacro = true;
}

bool MacroExpander::variadicArgumentsEmpty(
    const MacroDefinition& macro,
    const std::vector<Argument>& arguments) const {
    if (!macro.variadic || arguments.empty()) {
        return true;
    }
    return arguments.back().empty;
}

std::vector<Token> MacroExpander::substituteObject(
    const MacroDefinition& macro, const Token& invocation) {
    return processReplacement(macro, invocation, {});
}

std::vector<Token> MacroExpander::substituteFunction(
    const MacroDefinition& macro, const Token& invocation,
    std::vector<Argument>& traceArguments) {
    const std::vector<Argument>& inputArguments = traceArguments;
    const std::size_t fixedCount = macro.params.size();
    std::vector<Argument> arguments;

    if (!macro.variadic) {
        if (inputArguments.size() != fixedCount) {
            diagnostics_.error(
                invocation.loc,
                "macro '" + macro.name + "' expects " +
                    std::to_string(fixedCount) + " argument(s), but " +
                    std::to_string(inputArguments.size()) + " were provided");
        }
        arguments.reserve(fixedCount);
        for (std::size_t i = 0; i < fixedCount; ++i) {
            if (i < inputArguments.size()) {
                arguments.push_back(inputArguments[i]);
            } else {
                arguments.push_back(Argument{});
            }
        }
    } else {
        if (inputArguments.size() < fixedCount) {
            diagnostics_.error(
                invocation.loc,
                "macro '" + macro.name + "' expects at least " +
                    std::to_string(fixedCount) + " argument(s), but " +
                    std::to_string(inputArguments.size()) + " were provided");
        }
        arguments.reserve(fixedCount + 1);
        for (std::size_t i = 0; i < fixedCount; ++i) {
            if (i < inputArguments.size()) {
                arguments.push_back(inputArguments[i]);
            } else {
                arguments.push_back(Argument{});
            }
        }

        Argument variadic;
        for (std::size_t i = fixedCount; i < inputArguments.size(); ++i) {
            if (i > fixedCount) {
                if (inputArguments[i].hasSeparator) {
                    variadic.raw.push_back(inputArguments[i].separator);
                } else {
                    variadic.raw.push_back(makeComma(invocation.loc));
                }
            }
            variadic.raw.insert(variadic.raw.end(), inputArguments[i].raw.begin(),
                                inputArguments[i].raw.end());
        }
        variadic.empty = !hasNonTrivia(variadic.raw);
        arguments.push_back(std::move(variadic));
    }

    // Determine which parameters need pre-expansion. Parameters used only as
    // operands of # or ## are kept raw, exactly as the standard requires.
    std::unordered_set<std::string> expandParameters;
    for (std::size_t i = 0; i < macro.replacement.size(); ++i) {
        const Token& token = macro.replacement[i];
        if (token.kind != TokenKind::Identifier) {
            continue;
        }
        bool isParameter = false;
        for (const auto& param : macro.params) {
            if (param == token.text) {
                isParameter = true;
                break;
            }
        }
        if (macro.variadic && token.text == macro.variadicName) {
            isParameter = true;
        }
        if (!isParameter) {
            continue;
        }
        const bool prevStringize = i > 0 && macro.replacement[i - 1].isPunct("#");
        const bool prevPaste = i > 0 && macro.replacement[i - 1].isPunct("##");
        const bool nextPaste =
            i + 1 < macro.replacement.size() &&
            macro.replacement[i + 1].isPunct("##");
        if (!prevStringize && !prevPaste && !nextPaste) {
            expandParameters.insert(token.text);
        }
    }

    for (std::size_t i = 0; i < arguments.size(); ++i) {
        std::string name;
        if (i < macro.params.size()) {
            name = macro.params[i];
        } else if (macro.variadic) {
            name = macro.variadicName;
        } else {
            continue;
        }
        if (expandParameters.find(name) != expandParameters.end() &&
            !arguments[i].empty) {
            arguments[i].expanded = expand(arguments[i].raw);
            arguments[i].expandedReady = true;
        }
    }

    traceArguments = arguments;
    return processReplacement(macro, invocation, arguments);
}

std::vector<Token> MacroExpander::processReplacement(
    const MacroDefinition& macro, const Token& invocation,
    const std::vector<Argument>& arguments) {
    auto argumentFor = [&](std::string_view name) -> const Argument* {
        for (std::size_t i = 0; i < macro.params.size(); ++i) {
            if (macro.params[i] == name) {
                return i < arguments.size() ? &arguments[i] : nullptr;
            }
        }
        if (macro.variadic && macro.variadicName == name && !arguments.empty()) {
            return &arguments.back();
        }
        return nullptr;
    };

    const bool variadicEmpty = variadicArgumentsEmpty(macro, arguments);

    // Expand __VA_OPT__ and mark #/## tokens that belong to the replacement
    // list. Tokens coming from arguments must never be reinterpreted as
    // operators.
    std::vector<WorkToken> work;
    for (std::size_t i = 0; i < macro.replacement.size(); ++i) {
        const Token& token = macro.replacement[i];
        if (token.isIdentifier("__VA_OPT__") && i + 1 < macro.replacement.size() &&
            macro.replacement[i + 1].isPunct("(")) {
            int depth = 0;
            std::size_t close = i + 1;
            for (; close < macro.replacement.size(); ++close) {
                if (macro.replacement[close].isPunct("(")) {
                    ++depth;
                } else if (macro.replacement[close].isPunct(")")) {
                    --depth;
                    if (depth == 0) {
                        break;
                    }
                }
            }
            if (close >= macro.replacement.size()) {
                diagnostics_.error(token.loc, "unterminated '__VA_OPT__'");
                work.push_back({token, false});
                continue;
            }
            if (!variadicEmpty) {
                for (std::size_t j = i + 2; j < close; ++j) {
                    const Token& inner = macro.replacement[j];
                    const bool op =
                        inner.isPunct("#") || inner.isPunct("##");
                    work.push_back({inner, op});
                }
            } else {
                work.push_back(
                    {makePlacemarker(token.loc, macro.variadicName), false});
            }
            i = close;
            continue;
        }

        const bool op = token.isPunct("#") || token.isPunct("##");
        work.push_back({token, op});
    }

    // GNU extension: , ## __VA_ARGS__ removes the comma only when the
    // variadic argument is empty. With a non-empty variadic argument the comma
    // is kept and no pasting is attempted.
    if (macro.variadic) {
        std::vector<WorkToken> rewritten;
        for (std::size_t i = 0; i < work.size(); ++i) {
            if (i + 2 < work.size() && work[i].token.isPunct(",") &&
                work[i + 1].replacementOperator &&
                work[i + 1].token.isPunct("##") &&
                work[i + 2].token.kind == TokenKind::Identifier &&
                work[i + 2].token.text == macro.variadicName) {
                const Argument* variadic = argumentFor(macro.variadicName);
                if (!variadic || variadic->empty) {
                    i += 2;
                    continue;
                }
                rewritten.push_back(work[i]);
                for (const auto& token : variadic->raw) {
                    rewritten.push_back({token, false});
                }
                i += 2;
                continue;
            }
            rewritten.push_back(work[i]);
        }
        work = std::move(rewritten);
    }

    std::vector<WorkToken> substituted;
    for (std::size_t i = 0; i < work.size(); ++i) {
        const WorkToken& item = work[i];
        const Token& token = item.token;

        if (item.replacementOperator && token.isPunct("#")) {
            if (i + 1 < work.size() &&
                work[i + 1].token.kind == TokenKind::Identifier) {
                const Argument* argument = argumentFor(work[i + 1].token.text);
                if (argument) {
                    Token stringized =
                        special_.stringize(argument->raw, token.loc);
                    traceStep("stringize", joinTokenText(argument->raw),
                              stringized.text);
                    substituted.push_back({std::move(stringized), false});
                    ++i;
                    continue;
                }
            }
            substituted.push_back(item);
            continue;
        }

        if (token.kind == TokenKind::Identifier) {
            const Argument* argument = argumentFor(token.text);
            if (argument) {
                const bool adjacentPaste =
                    (i > 0 && work[i - 1].replacementOperator &&
                     work[i - 1].token.isPunct("##")) ||
                    (i + 1 < work.size() && work[i + 1].replacementOperator &&
                     work[i + 1].token.isPunct("##"));
                if (adjacentPaste) {
                    if (argument->empty) {
                        const std::string marker =
                            (macro.variadic &&
                             token.text == macro.variadicName)
                                ? macro.variadicName
                                : std::string();
                        traceStep("argument", token.text + " = ", "(empty)");
                        substituted.push_back(
                            {makePlacemarker(token.loc, marker), false});
                    } else {
                        traceStep("argument",
                                  token.text + " = " +
                                      joinTokenText(argument->raw),
                                  joinTokenText(argument->raw));
                        for (const auto& argumentToken : argument->raw) {
                            substituted.push_back({argumentToken, false});
                        }
                    }
                } else if (!argument->empty) {
                    const std::vector<Token>& expanded =
                        argument->expandedReady ? argument->expanded
                                                : argument->raw;
                    traceStep("argument",
                              token.text + " = " +
                                  joinTokenText(argument->raw),
                              joinTokenText(expanded));
                    for (const auto& argumentToken : expanded) {
                        substituted.push_back({argumentToken, false});
                    }
                }
                continue;
            }
        }

        substituted.push_back(item);
    }

    // Process ## left-to-right. Placemarkers implement empty arguments.
    std::vector<WorkToken> combined;
    for (std::size_t i = 0; i < substituted.size(); ++i) {
        const WorkToken& item = substituted[i];
        if (item.replacementOperator && item.token.isPunct("##")) {
            if (combined.empty() || i + 1 >= substituted.size()) {
                diagnostics_.error(item.token.loc,
                                   "'##' cannot appear at either end of a replacement list");
                continue;
            }
            WorkToken right = substituted[++i];
            WorkToken& left = combined.back();
            if (left.token.kind == TokenKind::Placemarker &&
                right.token.kind == TokenKind::Placemarker) {
                continue;
            }
            if (left.token.kind == TokenKind::Placemarker) {
                left = std::move(right);
                continue;
            }
            if (right.token.kind == TokenKind::Placemarker) {
                continue;
            }
            const std::string pasteBefore =
                left.token.text + " ## " + right.token.text;
            std::optional<Token> pasted =
                special_.paste(left.token, right.token, left.token.loc);
            if (pasted.has_value()) {
                left.token = std::move(*pasted);
                left.replacementOperator = false;
                traceStep("paste", pasteBefore, left.token.text);
            } else {
                traceStep("paste", pasteBefore,
                          left.token.text + right.token.text);
                combined.push_back(std::move(right));
            }
            continue;
        }
        combined.push_back(item);
    }

    std::vector<Token> result;
    result.reserve(combined.size());
    for (auto& item : combined) {
        if (item.token.kind == TokenKind::Placemarker) {
            continue;
        }
        Token token = std::move(item.token);
        token.loc = invocation.loc;
        token.fromMacro = true;
        applyHideSet(token, invocation.hideSet, macro.name);
        result.push_back(std::move(token));
    }
    return result;
}

std::vector<Token> MacroExpander::expand(const std::vector<Token>& input) {
    if (expansionDepth_ == 0) {
        expansionSteps_ = 0;
    }
    ++expansionDepth_;
    struct DepthGuard {
        int& depth;
        explicit DepthGuard(int& d) : depth(d) {}
        ~DepthGuard() { --depth; }
    } guard(expansionDepth_);

    std::deque<Token> in(input.begin(), input.end());
    std::vector<Token> out;
    out.reserve(input.size());

    while (!in.empty()) {
        if (++expansionSteps_ > kMaxExpansionSteps) {
            diagnostics_.error(in.front().loc,
                               "macro expansion step limit exceeded");
            break;
        }

        Token token = std::move(in.front());
        in.pop_front();

        if (token.kind == TokenKind::Newline ||
            token.kind == TokenKind::Comment) {
            out.push_back(std::move(token));
            continue;
        }

        // `defined` operands are not macro-expanded. This also matters when
        // `defined` is produced by a macro replacement list: MinGW's
        // __INTRINSIC_PROLOG relies on it.
        if (token.isIdentifier("defined")) {
            out.push_back(std::move(token));
            if (!in.empty() && in.front().isPunct("(")) {
                if (in.size() >= 3 && in[1].kind == TokenKind::Identifier &&
                    in[2].isPunct(")")) {
                    out.push_back(std::move(in[0]));
                    out.push_back(std::move(in[1]));
                    out.push_back(std::move(in[2]));
                    in.erase(in.begin(), in.begin() + 3);
                } else {
                    out.push_back(std::move(in.front()));
                    in.pop_front();
                }
            } else if (!in.empty() &&
                       in.front().kind == TokenKind::Identifier) {
                out.push_back(std::move(in.front()));
                in.pop_front();
            }
            continue;
        }

        if (token.kind != TokenKind::Identifier ||
            isHidden(token, token.text)) {
            out.push_back(std::move(token));
            continue;
        }

        const MacroDefinition* macro = registry_.find(token.text);
        if (macro) {
            registry_.markUsed(token.text);
        }
        if (!macro) {
            if (builtinHandler_) {
                if (auto builtin = builtinHandler_(token)) {
                    in.insert(in.begin(), std::make_move_iterator(builtin->begin()),
                              std::make_move_iterator(builtin->end()));
                    continue;
                }
            }
            out.push_back(std::move(token));
            continue;
        }

        if (!macro->functionLike) {
            const int traceId = beginTrace(
                *macro, token, {}, token.text, {}, static_cast<int>(token.loc.offset),
                static_cast<int>(token.text.size()), false);
            std::vector<Token> replacement = substituteObject(*macro, token);
            endTrace(traceId, replacement, {});
            in.insert(in.begin(), std::make_move_iterator(replacement.begin()),
                      std::make_move_iterator(replacement.end()));
            continue;
        }

        std::size_t look = 0;
        while (look < in.size() && in[look].isTrivia()) {
            ++look;
        }
        if (look >= in.size() || !in[look].isPunct("(")) {
            out.push_back(std::move(token));
            continue;
        }

        std::vector<Argument> arguments;
        std::size_t closeIndex = 0;
        if (!collectArguments(in, look, *macro, arguments, closeIndex)) {
            diagnostics_.error(token.loc,
                               "unterminated argument list invoking macro '" +
                                   macro->name + "'");
            out.push_back(std::move(token));
            continue;
        }

        std::string invocationText = token.text;
        int invocationLength = static_cast<int>(token.text.size());
        if (token.loc.file && closeIndex < in.size()) {
            const Token& close = in[closeIndex];
            const std::size_t start = token.loc.offset;
            const std::size_t end = close.loc.offset + close.text.size();
            if (end > start && end <= token.loc.file->text.size()) {
                invocationText = token.loc.file->text.substr(start, end - start);
                invocationLength = static_cast<int>(end - start);
            }
        }
        const int traceId = beginTrace(
            *macro, token, arguments, std::move(invocationText), {},
            static_cast<int>(token.loc.offset), invocationLength, true);
        std::vector<Token> replacement =
            substituteFunction(*macro, token, arguments);
        endTrace(traceId, replacement, arguments);
        in.erase(in.begin(),
                 in.begin() + static_cast<std::ptrdiff_t>(closeIndex + 1));
        in.insert(in.begin(), std::make_move_iterator(replacement.begin()),
                  std::make_move_iterator(replacement.end()));
    }

    return out;
}

} // namespace crucible
