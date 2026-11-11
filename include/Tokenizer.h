#pragma once

#include "Token.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crucibe {

class DiagnosticSink;

class Tokenizer {
public:
    std::vector<Token> tokenize(std::shared_ptr<const SourceFile> file,
                                DiagnosticSink* diagnostics = nullptr,
                                bool keepComments = false) const;

    // Used by token pasting. The input must tokenize to exactly one token
    // whose spelling is identical to the input (no leading/trailing space).
    std::optional<Token> tokenizeSingle(std::string_view text,
                                        const SourceLocation& loc) const;

    // Parse a header name starting at/after pos. On success, pos is advanced
    // past the closing delimiter and `name` receives the unquoted contents.
    static bool parseHeaderName(std::string_view text, std::size_t& pos,
                                std::string& name, bool& angled);
};

} // namespace crucibe
