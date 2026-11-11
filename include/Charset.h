#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crucible {

struct CharsetEncoding {
    enum class Kind { Utf8, Utf16LE, Utf16BE, CodePage };

    Kind kind = Kind::Utf8;
    int codePage = 65001;
    std::string name = "UTF-8";
};

// Resolve the GCC-style charset names used by -finput-charset /
// -fexec-charset on the target. Unsupported names are reported as errors.
bool parseCharset(std::string_view name, CharsetEncoding& out,
                  std::string& error);

// Decode a whole source file to UTF-8. UTF-16 BOMs are honored.
bool decodeToUtf8(std::string_view input, const CharsetEncoding& encoding,
                  std::string& output, std::string& error);

// Encode one Unicode scalar as execution-charset code units. The returned
// units are bytes for UTF-8/code pages; UTF-16 execution is not supported.
bool encodeCodePoint(std::uint32_t cp, const CharsetEncoding& encoding,
                     std::vector<std::uint32_t>& units, std::string& error);

} // namespace crucible
