#pragma once

#include "Token.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace crucible {

class DiagnosticSink;

struct MacroDefinition {
    std::string name;
    bool functionLike = false;
    std::vector<std::string> params;
    bool variadic = false;
    std::string variadicName = "__VA_ARGS__";
    std::vector<Token> replacement;
    SourceLocation definitionLoc;
    bool isBuiltin = false;
};

class MacroRegistry {
public:
    bool define(const MacroDefinition& definition, DiagnosticSink& diagnostics);
    bool undef(std::string_view name);
    const MacroDefinition* find(std::string_view name) const;
    bool contains(std::string_view name) const;

    void pushMacro(std::string_view name);
    void popMacro(std::string_view name, DiagnosticSink& diagnostics);

    const std::unordered_map<std::string, MacroDefinition>& all() const {
        return macros_;
    }

    static bool definitionsEquivalent(const MacroDefinition& a,
                                      const MacroDefinition& b);

private:
    std::unordered_map<std::string, MacroDefinition> macros_;
    std::unordered_map<std::string,
                       std::vector<std::optional<MacroDefinition>>> pushed_;
};

} // namespace crucible
