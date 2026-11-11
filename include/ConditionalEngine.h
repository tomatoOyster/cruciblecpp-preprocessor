#pragma once

#include "Token.h"

#include <functional>
#include <string_view>
#include <vector>

namespace crucible {

class DiagnosticSink;

struct CharacterOptions {
    std::string executionCharset = "UTF-8";
    bool unsignedChar = false;
};

class ConditionalEngine {
public:
    using Evaluator =
        std::function<bool(const std::vector<Token>&, const SourceLocation&)>;
    using DefinedCheck = std::function<bool(std::string_view)>;

    ConditionalEngine(DiagnosticSink& diagnostics, Evaluator evaluator,
                      DefinedCheck definedCheck);

    void onIf(const std::vector<Token>& expression, const SourceLocation& loc);
    void onIfdef(std::string_view name, bool negate, const SourceLocation& loc);
    void onElif(const std::vector<Token>& expression, const SourceLocation& loc);
    void onElifdef(std::string_view name, bool negate,
                   const SourceLocation& loc);
    void onElse(const SourceLocation& loc);
    void onEndif(const SourceLocation& loc);

    bool active() const;
    bool hasOpenFrames() const;
    void reportUnclosed();

    // Evaluates an already macro-expanded #if expression. Also used by the
    // preprocessor after it has handled defined/__has_include.
    static bool evaluateExpression(
        const std::vector<Token>& tokens, const SourceLocation& loc,
        DiagnosticSink& diagnostics,
        const CharacterOptions& characterOptions = {});

private:
    struct Frame {
        bool parentActive = true;
        bool thisActive = true;
        bool anyTaken = false;
        bool seenElse = false;
        SourceLocation loc;
    };

    void pushFrame(bool condition, const SourceLocation& loc);
    Frame& top();
    const Frame& top() const;

    DiagnosticSink& diagnostics_;
    Evaluator evaluator_;
    DefinedCheck definedCheck_;
    std::vector<Frame> stack_;
};

} // namespace crucible
