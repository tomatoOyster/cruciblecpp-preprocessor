#pragma once

#include "Token.h"

#include <string>
#include <string_view>

namespace crucible {

class OutputWriter {
public:
    explicit OutputWriter(bool lineMarkers);

    void writeToken(const Token& token);
    void writeNewline();
    void writeLineMarker(std::uint32_t line, std::string_view file);
    void writePragma(std::string_view text);
    void writeRaw(std::string_view text);
    void ensureNewline();

    const std::string& buffer() const { return buffer_; }
    void clear();

private:
    static std::string escapeFileName(std::string_view file);

    bool lineMarkers_;
    std::string buffer_;
    bool atLineStart_ = true;
};

} // namespace crucible
