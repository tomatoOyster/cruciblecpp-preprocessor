#include "OutputWriter.h"

#include <sstream>

namespace crucibe {

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
    buffer_.append(token.text);
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

} // namespace crucibe
