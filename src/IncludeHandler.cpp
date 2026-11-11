#include "IncludeHandler.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace crucibe {
namespace {

std::filesystem::path pathFromUtf8(std::string_view name) {
    const std::u8string value(
        reinterpret_cast<const char8_t*>(name.data()), name.size());
    return std::filesystem::path(value);
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()),
                       value.size());
}

bool isRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec &&
           std::filesystem::is_regular_file(path, ec) && !ec;
}

} // namespace

IncludeHandler::IncludeHandler(std::vector<std::filesystem::path> userDirs,
                               std::vector<std::filesystem::path> systemDirs)
    : userDirs_(std::move(userDirs)), systemDirs_(std::move(systemDirs)) {}

std::string IncludeHandler::canonicalKey(
    const std::filesystem::path& path) const {
    std::error_code ec;
    std::filesystem::path canonical =
        std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        canonical = path.lexically_normal();
    }
    std::string key = pathToUtf8(canonical);
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
    return key;
}

std::optional<std::filesystem::path> IncludeHandler::resolve(
    std::string_view name, bool angled,
    const std::filesystem::path& currentFile, bool includeNext) const {
    const std::filesystem::path requested = pathFromUtf8(name);
    if (requested.is_absolute()) {
        if (isRegularFile(requested)) {
            return requested;
        }
        return std::nullopt;
    }

    auto checkDir = [&](const std::filesystem::path& dir)
        -> std::optional<std::filesystem::path> {
        if (dir.empty()) {
            return std::nullopt;
        }
        const std::filesystem::path candidate = dir / requested;
        if (isRegularFile(candidate)) {
            return candidate;
        }
        return std::nullopt;
    };

    if (!angled && !includeNext && !currentFile.empty()) {
        const std::filesystem::path currentDir =
            currentFile.parent_path().empty() ? std::filesystem::path(".")
                                              : currentFile.parent_path();
        if (auto found = checkDir(currentDir)) {
            return found;
        }
    }

    // For #include_next, skip search directories up to and including the
    // directory that contains the current file, when it can be located.
    std::size_t startIndex = 0;
    std::vector<std::filesystem::path> searchDirs;
    searchDirs.reserve(userDirs_.size() + systemDirs_.size());
    searchDirs.insert(searchDirs.end(), userDirs_.begin(), userDirs_.end());
    searchDirs.insert(searchDirs.end(), systemDirs_.begin(), systemDirs_.end());

    if (includeNext) {
        const std::string currentKey =
            currentFile.empty()
                ? std::string()
                : canonicalKey(currentFile.parent_path().empty()
                                   ? std::filesystem::path(".")
                                   : currentFile.parent_path());
        for (std::size_t i = 0; i < searchDirs.size(); ++i) {
            if (!currentKey.empty() &&
                canonicalKey(searchDirs[i]) == currentKey) {
                startIndex = i + 1;
                break;
            }
        }
    }

    for (std::size_t i = startIndex; i < searchDirs.size(); ++i) {
        if (auto found = checkDir(searchDirs[i])) {
            return found;
        }
    }
    return std::nullopt;
}

bool IncludeHandler::exists(std::string_view name, bool angled,
                            const std::filesystem::path& currentFile,
                            bool includeNext) const {
    return resolve(name, angled, currentFile, includeNext).has_value();
}

bool IncludeHandler::markOnce(const std::filesystem::path& path) {
    return onceFiles_.insert(canonicalKey(path)).second;
}

bool IncludeHandler::isOnce(const std::filesystem::path& path) const {
    return onceFiles_.find(canonicalKey(path)) != onceFiles_.end();
}

} // namespace crucibe
