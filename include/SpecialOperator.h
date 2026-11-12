#pragma once

#include "Token.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crucible {

class DiagnosticSink;

struct BuiltinContext {
    std::string file;
    std::string baseFile;
    int includeLevel = 0;
    int stdVersion = 202002;
    int* counter = nullptr;
};

class SpecialOperator {
public:
    explicit SpecialOperator(DiagnosticSink& diagnostics);

    std::optional<std::vector<Token>> expandBuiltin(
        const Token& token, const BuiltinContext& context) const;

    Token stringize(const std::vector<Token>& rawArgument,
                    const SourceLocation& loc) const;

    std::optional<Token> paste(const Token& left, const Token& right,
                               const SourceLocation& loc);

    std::string destringize(std::string_view literal) const;

    static bool isBuiltinName(std::string_view name);

private:
    DiagnosticSink& diagnostics_;
    std::string date_;
    std::string time_;
};

} // namespace crucible
