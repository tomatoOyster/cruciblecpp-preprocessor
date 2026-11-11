#include "MacroRegistry.h"

#include "Preprocessor.h"

#include <algorithm>
#include <unordered_set>

namespace crucible {
namespace {

bool isParameter(const MacroDefinition& macro, std::string_view name) {
    for (const auto& param : macro.params) {
        if (param == name) {
            return true;
        }
    }
    return macro.variadic && macro.variadicName == name;
}

bool validReplacement(const MacroDefinition& macro, DiagnosticSink& diagnostics) {
    const auto& replacement = macro.replacement;
    std::unordered_set<std::string> params;
    for (const auto& param : macro.params) {
        if (!params.insert(param).second) {
            diagnostics.error(macro.definitionLoc,
                              "duplicate macro parameter '" + param + "'");
            return false;
        }
    }

    if (macro.variadic && !macro.variadicName.empty()) {
        if (!params.insert(macro.variadicName).second) {
            diagnostics.error(macro.definitionLoc,
                              "duplicate variadic parameter name '" +
                                  macro.variadicName + "'");
            return false;
        }
    }

    for (std::size_t i = 0; i < replacement.size(); ++i) {
        const Token& token = replacement[i];
        if (token.isPunct("#")) {
            if (i + 1 >= replacement.size()) {
                diagnostics.error(token.loc, "'#' is not followed by a macro parameter");
                return false;
            }
            const Token& operand = replacement[i + 1];
            if (operand.kind != TokenKind::Identifier ||
                !isParameter(macro, operand.text)) {
                diagnostics.error(token.loc,
                                  "'#' must be followed by a macro parameter");
                return false;
            }
        } else if (token.isPunct("##")) {
            if (i == 0 || i + 1 >= replacement.size()) {
                diagnostics.error(token.loc,
                                  "'##' cannot appear at either end of a macro replacement list");
                return false;
            }
        } else if (token.kind == TokenKind::Identifier) {
            if (token.text == "__VA_ARGS__" && !macro.variadic) {
                diagnostics.error(
                    token.loc,
                    "'__VA_ARGS__' may only appear in a variadic macro");
                return false;
            }
        }
    }

    // __VA_OPT__ must be followed by a parenthesized token sequence. This is
    // validated here; the expander does the actual substitution.
    for (std::size_t i = 0; i < replacement.size(); ++i) {
        if (!replacement[i].isIdentifier("__VA_OPT__")) {
            continue;
        }
        if (!macro.variadic) {
            diagnostics.error(replacement[i].loc,
                              "'__VA_OPT__' may only appear in a variadic macro");
            return false;
        }
        if (i + 1 >= replacement.size() ||
            !replacement[i + 1].isPunct("(")) {
            diagnostics.error(replacement[i].loc,
                              "'__VA_OPT__' must be followed by '('");
            return false;
        }
        int depth = 0;
        bool closed = false;
        for (std::size_t j = i + 1; j < replacement.size(); ++j) {
            if (replacement[j].isPunct("(")) {
                ++depth;
            } else if (replacement[j].isPunct(")")) {
                --depth;
                if (depth == 0) {
                    closed = true;
                    break;
                }
            } else if (replacement[j].isIdentifier("__VA_OPT__")) {
                diagnostics.error(replacement[j].loc,
                                  "nested '__VA_OPT__' is not allowed");
                return false;
            }
        }
        if (!closed) {
            diagnostics.error(replacement[i].loc,
                              "unterminated '__VA_OPT__'");
            return false;
        }
    }
    return true;
}

} // namespace

bool MacroRegistry::definitionsEquivalent(const MacroDefinition& a,
                                          const MacroDefinition& b) {
    if (a.functionLike != b.functionLike || a.params != b.params ||
        a.variadic != b.variadic || a.variadicName != b.variadicName ||
        a.replacement.size() != b.replacement.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.replacement.size(); ++i) {
        if (!tokensEquivalent(a.replacement[i], b.replacement[i])) {
            return false;
        }
    }
    return true;
}

bool MacroRegistry::define(const MacroDefinition& definition,
                           DiagnosticSink& diagnostics) {
    if (definition.name.empty()) {
        diagnostics.error(definition.definitionLoc, "empty macro name");
        return false;
    }
    if (definition.name == "defined" ||
        definition.name == "__VA_ARGS__" ||
        definition.name == "__VA_OPT__") {
        diagnostics.error(definition.definitionLoc,
                          "cannot define the reserved name '" +
                              definition.name + "'");
        return false;
    }
    if (definition.variadic && definition.variadicName.empty()) {
        diagnostics.error(definition.definitionLoc,
                          "variadic macro needs a parameter name");
        return false;
    }
    if (!validReplacement(definition, diagnostics)) {
        return false;
    }

    auto it = macros_.find(definition.name);
    if (it != macros_.end()) {
        if (definitionsEquivalent(it->second, definition)) {
            return true;
        }
        diagnostics.error(definition.definitionLoc,
                          "macro '" + definition.name +
                              "' redefined with a different replacement list");
        return false;
    }

    macros_.emplace(definition.name, definition);
    return true;
}

bool MacroRegistry::undef(std::string_view name) {
    return macros_.erase(std::string(name)) != 0;
}

const MacroDefinition* MacroRegistry::find(std::string_view name) const {
    const auto it = macros_.find(std::string(name));
    return it == macros_.end() ? nullptr : &it->second;
}

bool MacroRegistry::contains(std::string_view name) const {
    return macros_.find(std::string(name)) != macros_.end();
}

void MacroRegistry::pushMacro(std::string_view name) {
    const std::string key(name);
    std::optional<MacroDefinition> saved;
    const auto it = macros_.find(key);
    if (it != macros_.end()) {
        saved = it->second;
    }
    pushed_[key].push_back(std::move(saved));
}

void MacroRegistry::popMacro(std::string_view name, DiagnosticSink& diagnostics) {
    const std::string key(name);
    const auto it = pushed_.find(key);
    if (it == pushed_.end() || it->second.empty()) {
        diagnostics.warning({}, "pop_macro without matching push_macro for '" +
                                    key + "'");
        return;
    }
    std::optional<MacroDefinition> saved = std::move(it->second.back());
    it->second.pop_back();
    if (it->second.empty()) {
        pushed_.erase(it);
    }
    if (saved.has_value()) {
        macros_[key] = std::move(*saved);
    } else {
        macros_.erase(key);
    }
}

} // namespace crucible
