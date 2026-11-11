#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace crucibe {

class IncludeHandler {
public:
    IncludeHandler(std::vector<std::filesystem::path> userDirs,
                   std::vector<std::filesystem::path> systemDirs);

    std::optional<std::filesystem::path> resolve(
        std::string_view name, bool angled,
        const std::filesystem::path& currentFile,
        bool includeNext = false) const;

    bool exists(std::string_view name, bool angled,
                const std::filesystem::path& currentFile,
                bool includeNext = false) const;

    std::string canonicalKey(const std::filesystem::path& path) const;

    bool markOnce(const std::filesystem::path& path);
    bool isOnce(const std::filesystem::path& path) const;

    const std::vector<std::filesystem::path>& userDirs() const {
        return userDirs_;
    }
    const std::vector<std::filesystem::path>& systemDirs() const {
        return systemDirs_;
    }

private:
    std::vector<std::filesystem::path> userDirs_;
    std::vector<std::filesystem::path> systemDirs_;
    std::unordered_set<std::string> onceFiles_;
};

} // namespace crucibe
