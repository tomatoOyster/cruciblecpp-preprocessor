#pragma once

#include "Token.h"

#include <filesystem>
#include <memory>
#include <string_view>

namespace crucible {

class DiagnosticSink;

class FileReader {
public:
    // Reads a source file, decodes the requested input charset to UTF-8,
    // strips a BOM, normalizes newlines, performs line splicing (except inside
    // raw string literals), and ensures a final newline. Returns nullptr on
    // unrecoverable I/O or charset failure.
    std::shared_ptr<SourceFile> read(const std::filesystem::path& path,
                                     DiagnosticSink& diagnostics,
                                     std::string_view inputCharset = "UTF-8");
};

} // namespace crucible
