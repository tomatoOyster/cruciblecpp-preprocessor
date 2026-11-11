#pragma once

#include "MacroRegistry.h"
#include "Token.h"

#include <cstddef>
#include <deque>
#include <functional>
#include <optional>
#include <vector>

namespace crucible {

class DiagnosticSink;
class SpecialOperator;

class MacroExpander {
public:
    using BuiltinHandler =
        std::function<std::optional<std::vector<Token>>(const Token&)>;

    MacroExpander(MacroRegistry& registry, DiagnosticSink& diagnostics,
                  SpecialOperator& special);

    void setBuiltinHandler(BuiltinHandler handler);

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
        const std::vector<Argument>& arguments);

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

    MacroRegistry& registry_;
    DiagnosticSink& diagnostics_;
    SpecialOperator& special_;
    BuiltinHandler builtinHandler_;
    std::size_t expansionSteps_ = 0;
    int expansionDepth_ = 0;
    static constexpr std::size_t kMaxExpansionSteps = 1000000;
};

} // namespace crucible
