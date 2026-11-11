#pragma once

#include "Token.h"

#include <filesystem>
#include <memory>

namespace crucible {

class DiagnosticSink;

class FileReader {
public:
    // Reads a source file, strips a UTF-8 BOM, normalizes newlines, performs
    // line splicing (except inside raw string literals), and ensures a final
    // newline. Returns nullptr on unrecoverable I/O failure.
    std::shared_ptr<SourceFile> read(const std::filesystem::path& path,
                                     DiagnosticSink& diagnostics);
};

} // namespace crucible
