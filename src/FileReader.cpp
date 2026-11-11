#include "FileReader.h"

#include "Charset.h"
#include "Preprocessor.h"

#include <fstream>
#include <sstream>
#include <string_view>

namespace crucible {
namespace {

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()),
                       value.size());
}

bool isIdentifierChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return c == '_' || (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || u >= 0x80;
}

// If a raw string literal starts at `pos`, returns its prefix length (the
// number of characters before the delimiter). Otherwise returns 0.
std::size_t rawStringPrefixLength(std::string_view text, std::size_t pos) {
    if (pos > 0 && isIdentifierChar(text[pos - 1])) {
        return 0;
    }

    std::size_t prefixLength = 0;
    if (text.compare(pos, 4, "u8R\"") == 0) {
        prefixLength = 4;
    } else if (text.compare(pos, 3, "uR\"") == 0 ||
               text.compare(pos, 3, "UR\"") == 0 ||
               text.compare(pos, 3, "LR\"") == 0) {
        prefixLength = 3;
    } else if (text.compare(pos, 2, "R\"") == 0) {
        prefixLength = 2;
    }
    if (prefixLength == 0) {
        return 0;
    }

    std::size_t p = pos + prefixLength;
    std::size_t delimiterLength = 0;
    while (p < text.size() && text[p] != '(') {
        const char c = text[p];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\\' ||
            c == ')' || delimiterLength >= 16) {
            return 0;
        }
        ++p;
        ++delimiterLength;
    }
    if (p >= text.size() || text[p] != '(') {
        return 0;
    }
    return prefixLength + delimiterLength + 1;
}

std::string normalizeNewlines(std::string input) {
    std::string out;
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\r') {
            if (i + 1 < input.size() && input[i + 1] == '\n') {
                ++i;
            }
            out.push_back('\n');
        } else {
            out.push_back(input[i]);
        }
    }
    return out;
}

} // namespace

std::shared_ptr<SourceFile> FileReader::read(const std::filesystem::path& path,
                                             DiagnosticSink& diagnostics,
                                             std::string_view inputCharset) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        diagnostics.error({}, "cannot open source file '" + pathToUtf8(path) +
                                  "'");
        return nullptr;
    }

    std::ostringstream stream;
    stream << in.rdbuf();
    if (!in.good() && !in.eof()) {
        diagnostics.error({}, "failed while reading '" + pathToUtf8(path) + "'");
        return nullptr;
    }

    std::string input = stream.str();

    CharsetEncoding encoding;
    std::string charsetError;
    if (!parseCharset(inputCharset, encoding, charsetError)) {
        diagnostics.error({}, charsetError);
        return nullptr;
    }
    std::string decoded;
    if (!decodeToUtf8(input, encoding, decoded, charsetError)) {
        diagnostics.error({}, "'" + pathToUtf8(path) + "': " + charsetError);
        return nullptr;
    }
    input = std::move(decoded);

    input = normalizeNewlines(std::move(input));

    std::string text;
    text.reserve(input.size());
    std::vector<std::size_t> spliceOffsets;
    for (std::size_t i = 0; i < input.size();) {
        // Raw string bodies are not subject to translation phase 2 splicing.
        const std::size_t rawPrefix = rawStringPrefixLength(input, i);
        if (rawPrefix != 0) {
            // rawPrefix includes the opening parenthesis.
            const std::size_t openParen = i + rawPrefix - 1;
            const std::size_t quote = input.find('"', i);
            std::string delim;
            if (quote != std::string::npos && quote < openParen) {
                delim = input.substr(quote + 1, openParen - quote - 1);
            }
            const std::string closing = ")" + delim + "\"";
            const std::size_t close = input.find(closing, openParen + 1);
            if (close == std::string::npos) {
                text.append(input, i, std::string::npos);
                i = input.size();
                break;
            }
            const std::size_t end = close + closing.size();
            text.append(input, i, end - i);
            i = end;
            continue;
        }

        if (input[i] == '\\' && i + 1 < input.size() &&
            input[i + 1] == '\n') {
            spliceOffsets.push_back(text.size());
            i += 2;
            continue;
        }
        text.push_back(input[i]);
        ++i;
    }

    if (!text.empty() && text.back() != '\n') {
        diagnostics.warning({}, "'" + pathToUtf8(path) +
                                    "' does not end in a newline; adding one");
        text.push_back('\n');
    }

    auto file = std::make_shared<SourceFile>();
    file->displayPath = pathToUtf8(path);
    file->physicalPath = pathToUtf8(path);
    file->text = std::move(text);
    file->spliceOffsets = std::move(spliceOffsets);
    file->lineOffsets.push_back(0);
    for (std::size_t i = 0; i < file->text.size(); ++i) {
        if (file->text[i] == '\n') {
            file->lineOffsets.push_back(i + 1);
        }
    }
    return file;
}

} // namespace crucible
