#pragma once

#include "MacroRegistry.h"
#include "Token.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crucible {

class DiagnosticSink;
class SpecialOperator;

struct MacroTraceArgument {
    std::string raw;
    std::string expanded;
};

struct MacroTraceStart {
    int id = 0;
    int parentId = -1;
    std::string macro;
    SourceLocation invocation;
    SourceLocation definition;
    std::string invocationText;
    std::string sourceLine;
    std::string definitionText;
    int offset = 0;
    int length = 0;
    int depth = 0;
    bool functionLike = false;
    std::vector<MacroTraceArgument> arguments;
};

struct MacroTraceStep {
    int parentId = -1;
    std::string operation;
    std::string before;
    std::string after;
};

class MacroExpander {
public:
    using BuiltinHandler =
        std::function<std::optional<std::vector<Token>>(const Token&)>;
    using TraceStartHandler =
        std::function<void(const MacroTraceStart&)>;
    using TraceEndHandler = std::function<void(
        int id, const std::vector<Token>& replacement,
        const std::vector<MacroTraceArgument>& arguments)>;
    using TraceStepHandler =
        std::function<void(const MacroTraceStep&)>;

    MacroExpander(MacroRegistry& registry, DiagnosticSink& diagnostics,
                  SpecialOperator& special);

    void setBuiltinHandler(BuiltinHandler handler);
    void setTraceHandlers(TraceStartHandler start, TraceEndHandler end,
                          TraceStepHandler step);

    std::vector<Token> expand(const std::vector<Token>& input);

private:
    struct Argument {
        std::vector<Token> raw;
        std::vector<Token> expanded;
        Token separator;
        bool hasSeparator = false;
        bool empty = true;
        bool expandedReady = false;
    };

    std::vector<Token> substituteObject(const MacroDefinition& macro,
                                        const Token& invocation);
    std::vector<Token> substituteFunction(
        const MacroDefinition& macro, const Token& invocation,
        std::vector<Argument>& arguments);

    bool collectArguments(const std::deque<Token>& input, std::size_t openIndex,
                          const MacroDefinition& macro,
                          std::vector<Argument>& arguments,
                          std::size_t& closeIndex);

    std::vector<Token> processReplacement(const MacroDefinition& macro,
                                          const Token& invocation,
                                          const std::vector<Argument>& arguments);

    void applyHideSet(Token& token,
                      const std::shared_ptr<const HideSet>& invocationHide,
                      std::string_view macroName) const;

    bool variadicArgumentsEmpty(const MacroDefinition& macro,
                                const std::vector<Argument>& arguments) const;

    int beginTrace(const MacroDefinition& macro, const Token& invocation,
                   const std::vector<Argument>& arguments,
                   std::string invocationText, std::string sourceLine,
                   int offset, int length, bool functionLike);
    void endTrace(int id, const std::vector<Token>& replacement,
                  const std::vector<Argument>& arguments);
    void traceStep(std::string operation, std::string before,
                   std::string after);
    std::vector<MacroTraceArgument> traceArguments(
        const std::vector<Argument>& arguments) const;

    MacroRegistry& registry_;
    DiagnosticSink& diagnostics_;
    SpecialOperator& special_;
    BuiltinHandler builtinHandler_;
    TraceStartHandler traceStart_;
    TraceEndHandler traceEnd_;
    TraceStepHandler traceStep_;
    std::vector<int> traceStack_;
    int nextTraceId_ = 1;
    std::size_t expansionSteps_ = 0;
    int expansionDepth_ = 0;
    static constexpr std::size_t kMaxExpansionSteps = 1000000;
};

} // namespace crucible
