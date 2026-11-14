#include "OutputWriter.h"

#include <cstdint>
#include <cstdio>
#include <sstream>

namespace crucible {
namespace {

bool parseUcnAt(std::string_view text, std::size_t i, std::size_t& length,
                std::uint32_t& codepoint) {
    if (i + 2 > text.size() || text[i] != '\\' ||
        (text[i + 1] != 'u' && text[i + 1] != 'U')) {
        return false;
    }
    const std::size_t digits = text[i + 1] == 'u' ? 4 : 8;
    if (i + 2 + digits > text.size()) {
        return false;
    }
    std::uint32_t value = 0;
    for (std::size_t k = 0; k < digits; ++k) {
        const char c = text[i + 2 + k];
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            digit = c - 'A' + 10;
        }
        if (digit < 0) {
            return false;
        }
        value = value * 16 + static_cast<std::uint32_t>(digit);
    }
    length = 2 + digits;
    codepoint = value;
    return true;
}

std::string canonicalIdentifier(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        std::size_t length = 0;
        std::uint32_t codepoint = 0;
        if (parseUcnAt(text, i, length, codepoint)) {
            if (codepoint >= 0x20u && codepoint <= 0x7Eu &&
                codepoint != '\\') {
                out.push_back(static_cast<char>(codepoint));
            } else {
                char buffer[16];
                std::snprintf(buffer, sizeof(buffer), "\\U%08x",
                              static_cast<unsigned>(codepoint));
                out.append(buffer);
            }
            i += length;
            continue;
        }
        out.push_back(text[i]);
        ++i;
    }
    return out;
}

} // namespace

OutputWriter::OutputWriter(bool lineMarkers) : lineMarkers_(lineMarkers) {}

std::string OutputWriter::escapeFileName(std::string_view file) {
    std::string out;
    out.reserve(file.size() + 2);
    for (char c : file) {
        if (c == '\\' || c == '"') {
            out.push_back('\\');
        }
        out.push_back(c);
    }
    return out;
}

void OutputWriter::writeToken(const Token& token) {
    if (token.kind == TokenKind::Newline) {
        writeNewline();
        return;
    }
    if (token.kind == TokenKind::Placemarker ||
        token.kind == TokenKind::EndOfFile || token.text.empty()) {
        return;
    }

    if (!atLineStart_) {
        // Whitespace between every pair of tokens is always safe and avoids
        // accidental token pasting in the emitted preprocessed source.
        buffer_.push_back(' ');
    }
    if (token.kind == TokenKind::Identifier) {
        buffer_.append(canonicalIdentifier(token.text));
    } else {
        buffer_.append(token.text);
    }
    atLineStart_ = token.text.back() == '\n';
}

void OutputWriter::writeNewline() {
    buffer_.push_back('\n');
    atLineStart_ = true;
}

void OutputWriter::writeLineMarker(std::uint32_t line, std::string_view file) {
    if (!lineMarkers_) {
        return;
    }
    if (!atLineStart_) {
        writeNewline();
    }
    // GNU-style line marker. GCC treats .i input as already preprocessed and
    // does not re-run #line directives, but it does recognize # <line> "...".
    buffer_.append("# ");
    buffer_.append(std::to_string(line));
    buffer_.append(" \"");
    buffer_.append(escapeFileName(file));
    buffer_.append("\"\n");
    atLineStart_ = true;
}

void OutputWriter::writePragma(std::string_view text) {
    if (!atLineStart_) {
        writeNewline();
    }
    buffer_.append("#pragma ");
    buffer_.append(text);
    if (buffer_.empty() || buffer_.back() != '\n') {
        buffer_.push_back('\n');
    }
    atLineStart_ = true;
}

void OutputWriter::writeRaw(std::string_view text) {
    buffer_.append(text);
    if (!text.empty()) {
        atLineStart_ = text.back() == '\n';
    }
}

void OutputWriter::ensureNewline() {
    if (!atLineStart_) {
        writeNewline();
    }
}

void OutputWriter::clear() {
    buffer_.clear();
    atLineStart_ = true;
}

} // namespace crucible
