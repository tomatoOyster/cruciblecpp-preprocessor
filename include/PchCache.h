#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace crucible {

struct PchCacheOptions {
    std::filesystem::path directory;
    std::filesystem::path statsFile;
    std::size_t maxEntries = 256;
    std::uintmax_t maxBytes = 512ULL * 1024 * 1024;
    int maxAgeDays = 30;
    int lockTimeoutMs = 10000;
    bool cleanup = false;
    bool verbose = false;
};

struct PchCacheContext {
    std::filesystem::path compiler;
    std::string compilerVersion;
    std::string toolVersion;
    std::string standard;
    std::string inputCharset;
    std::string executionCharset;
    bool unsignedChar = false;
    std::vector<std::filesystem::path> includeDirs;
    std::vector<std::filesystem::path> systemDirs;
    std::vector<std::filesystem::path> forcedIncludes;
    std::vector<std::string> commandActions;
};

enum class PchCacheStatus {
    Disabled,
    Hit,
    Miss,
    Stale,
    Corrupt,
    Error,
};

struct PchCacheResult {
    PchCacheStatus status = PchCacheStatus::Disabled;
    std::string detail;

    bool usable() const {
        return status == PchCacheStatus::Hit ||
               status == PchCacheStatus::Miss;
    }
};

struct PchCacheStats {
    std::uint64_t lookups = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t stale = 0;
    std::uint64_t corrupt = 0;
    std::uint64_t errors = 0;
    std::uint64_t created = 0;
    std::uint64_t updated = 0;
    std::uint64_t evicted = 0;
    std::uint64_t lockWaits = 0;
    std::uint64_t lockTimeouts = 0;
    std::uint64_t entries = 0;
    std::uintmax_t bytes = 0;

    double hitRate() const {
        return lookups == 0
                   ? 0.0
                   : static_cast<double>(hits) / static_cast<double>(lookups);
    }
};

class PchCache {
public:
    PchCache(PchCacheOptions options, PchCacheContext context);
    ~PchCache();

    PchCache(const PchCache&) = delete;
    PchCache& operator=(const PchCache&) = delete;

    bool enabled() const { return enabled_; }
    PchCacheResult observe(const std::filesystem::path& header,
                           const std::filesystem::path& gch);
    void finish();
    const PchCacheStats& stats() const { return stats_; }
    bool writeStats(std::string& error) const;

private:
    struct Manifest;
    struct Lock;

    bool initialize(std::string& error);
    std::string cacheKey(const std::filesystem::path& header,
                         const std::filesystem::path& gch) const;
    std::string optionsHash() const;
    std::filesystem::path manifestPath(std::string_view key) const;
    std::filesystem::path artifactPath(std::string_view key) const;
    std::filesystem::path dependencyPath(std::string_view key) const;
    std::filesystem::path optionsStampPath(std::string_view key) const;
    std::filesystem::path lockPath(std::string_view key) const;
    bool loadManifest(std::string_view key, Manifest& manifest) const;
    bool writeManifest(std::string_view key, const Manifest& manifest,
                       std::string& error) const;
    bool writeArtifact(std::string_view key, const std::filesystem::path& gch,
                       std::string& hash, std::string& error) const;
    bool writeDependency(std::string_view key,
                         const std::filesystem::path& header,
                         const std::filesystem::path& gch,
                         std::string& error) const;
    bool writeOptionsStamp(std::string_view key,
                           std::string& error) const;
    void updateStats(const PchCacheStats& delta);
    void gc();
    std::uintmax_t entrySize(std::string_view key) const;
    bool acquireLock(std::string_view key, Lock& lock);
    void logVerbose(const std::string& message) const;

    PchCacheOptions options_;
    PchCacheContext context_;
    PchCacheStats stats_;
    std::filesystem::path root_;
    std::filesystem::path manifestsDir_;
    std::filesystem::path artifactsDir_;
    std::filesystem::path dependenciesDir_;
    std::filesystem::path stampsDir_;
    std::filesystem::path locksDir_;
    std::filesystem::path tempDir_;
    std::string toolVersion_;
    bool enabled_ = false;
    bool finished_ = false;
};

} // namespace crucible
