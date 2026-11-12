#include "PchCache.h"

#include "Sha256.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>

namespace crucible {
namespace {

constexpr std::string_view kSchema = "1";
constexpr std::string_view kCacheVersion = "1";

std::string pathText(const std::filesystem::path& path) {
    const std::u8string value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()),
                       value.size());
}

std::filesystem::path canonicalPath(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path value = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        value = std::filesystem::absolute(path, ec);
    }
    if (ec) {
        value = path.lexically_normal();
    }
    return value;
}

std::string canonicalKeyPath(const std::filesystem::path& path) {
    std::string value = pathText(canonicalPath(path));
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
#endif
    return value;
}

std::optional<std::uintmax_t> fileSize(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? std::nullopt : std::optional<std::uintmax_t>(size);
}

std::optional<long long> writeTime(const std::filesystem::path& path) {
    std::error_code ec;
    const auto value = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return std::nullopt;
    }
    return static_cast<long long>(
        value.time_since_epoch().count());
}

std::string encodeField(std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size());
    for (const unsigned char c : value) {
        if (c == '%' || c == '\n' || c == '\r' || c == '=') {
            result.push_back('%');
            result.push_back(kHex[c >> 4]);
            result.push_back(kHex[c & 0x0F]);
        } else {
            result.push_back(static_cast<char>(c));
        }
    }
    return result;
}

std::optional<std::string> decodeField(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') {
            result.push_back(value[i]);
            continue;
        }
        if (i + 2 >= value.size()) {
            return std::nullopt;
        }
        const auto hex = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int hi = hex(value[i + 1]);
        const int lo = hex(value[i + 2]);
        if (hi < 0 || lo < 0) {
            return std::nullopt;
        }
        result.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
    }
    return result;
}

std::string makeEscape(std::string_view value, bool target) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const char c : value) {
        if (c == '\\') {
            result.push_back('/');
        } else if (c == ' ' || c == '#') {
            result.push_back('\\');
            result.push_back(c);
        } else if (c == '$' && target) {
            result.append("$$");
        } else {
            result.push_back(c);
        }
    }
    return result;
}

std::string tempPathFor(const std::filesystem::path& target) {
    static std::uint64_t counter = 0;
    ++counter;
    return pathText(target) + ".tmp." +
           std::to_string(
               std::chrono::steady_clock::now().time_since_epoch().count()) +
           "." + std::to_string(counter);
}

bool writeFileAtomic(const std::filesystem::path& target,
                     std::string_view content, std::string& error) {
    std::error_code ec;
    const auto parent = target.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "cannot create cache directory '" + pathText(parent) +
                    "': " + ec.message();
            return false;
        }
    }
    const std::filesystem::path temp = tempPathFor(target);
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write temporary cache file '" + pathText(temp) + "'";
            return false;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            error = "failed while writing cache file '" + pathText(temp) + "'";
            out.close();
            std::filesystem::remove(temp, ec);
            return false;
        }
    }
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::filesystem::remove(target, ec);
        ec.clear();
        std::filesystem::rename(temp, target, ec);
    }
    if (ec) {
        error = "cannot replace cache file '" + pathText(target) +
                "': " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

bool copyFileAtomic(const std::filesystem::path& source,
                    const std::filesystem::path& target,
                    std::string& hash, std::uintmax_t& size,
                    std::string& error) {
    std::ifstream in(source, std::ios::binary);
    if (!in) {
        error = "cannot read PCH artifact '" + pathText(source) + "'";
        return false;
    }
    std::error_code ec;
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    const std::filesystem::path temp = tempPathFor(target);
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "cannot write PCH cache artifact '" + pathText(temp) + "'";
        return false;
    }
    Sha256 sha;
    std::array<char, 64 * 1024> buffer{};
    size = 0;
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = in.gcount();
        if (count <= 0) {
            break;
        }
        out.write(buffer.data(), count);
        sha.update(buffer.data(), static_cast<std::size_t>(count));
        size += static_cast<std::uintmax_t>(count);
    }
    if (!in.eof() || !out) {
        error = "I/O failure while copying PCH artifact '" +
                pathText(source) + "'";
        out.close();
        std::filesystem::remove(temp, ec);
        return false;
    }
    out.flush();
    if (!out) {
        error = "failed while writing PCH cache artifact '" +
                pathText(target) + "'";
        out.close();
        std::filesystem::remove(temp, ec);
        return false;
    }
    out.close();
    hash = sha.hexDigest();
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::filesystem::remove(target, ec);
        ec.clear();
        std::filesystem::rename(temp, target, ec);
    }
    if (ec) {
        error = "cannot replace PCH cache artifact '" + pathText(target) +
                "': " + ec.message();
        std::filesystem::remove(temp, ec);
        return false;
    }
    return true;
}

std::optional<std::string> hashFile(const std::filesystem::path& path,
                                    std::uintmax_t* size = nullptr) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    Sha256 sha;
    std::array<char, 64 * 1024> buffer{};
    std::uintmax_t total = 0;
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = in.gcount();
        if (count <= 0) {
            break;
        }
        sha.update(buffer.data(), static_cast<std::size_t>(count));
        total += static_cast<std::uintmax_t>(count);
    }
    if (!in.eof()) {
        return std::nullopt;
    }
    if (size) {
        *size = total;
    }
    return sha.hexDigest();
}

std::string boolText(bool value) {
    return value ? "1" : "0";
}

bool hasGccPchMagic(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    char header[32]{};
    in.read(header, sizeof(header));
    const std::streamsize count = in.gcount();
    if (count < 9) {
        return false;
    }
    std::string_view magic(header, static_cast<std::size_t>(count));
    if (magic.substr(0, 5) != "gpch+") {
        return false;
    }
    const std::size_t colon = magic.find(':');
    return colon != std::string_view::npos && colon >= 6;
}

} // namespace

struct PchCache::Manifest {
    std::string key;
    std::string header;
    std::string headerHash;
    std::uintmax_t headerSize = 0;
    long long headerMtime = 0;
    std::string gch;
    std::string gchHash;
    std::uintmax_t gchSize = 0;
    long long gchMtime = 0;
    std::string compiler;
    std::string compilerVersion;
    std::string compilerHash;
    std::uintmax_t compilerSize = 0;
    long long compilerMtime = 0;
    std::string optionsHash;
    std::string snapshot;
    std::string snapshotHash;
    std::uintmax_t snapshotSize = 0;
    std::string toolVersion;
};

struct PchCache::Lock {
    std::filesystem::path path;
    bool held = false;
    ~Lock() {
        if (held) {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    }
};

PchCache::PchCache(PchCacheOptions options, PchCacheContext context)
    : options_(std::move(options)), context_(std::move(context)),
      toolVersion_(context_.toolVersion) {
    std::string error;
    enabled_ = initialize(error);
    if (!enabled_ && !error.empty()) {
        ++stats_.errors;
        logVerbose("PCH cache disabled: " + error);
    }
}

PchCache::~PchCache() {
    finish();
}

bool PchCache::initialize(std::string& error) {
    if (options_.directory.empty()) {
        return false;
    }
    root_ = canonicalPath(options_.directory);
    manifestsDir_ = root_ / "manifests";
    artifactsDir_ = root_ / "artifacts";
    dependenciesDir_ = root_ / "deps";
    stampsDir_ = root_ / "stamps";
    locksDir_ = root_ / "locks";
    tempDir_ = root_ / "tmp";
    std::error_code ec;
    for (const auto& dir : {root_, manifestsDir_, artifactsDir_,
                            dependenciesDir_, stampsDir_, locksDir_,
                            tempDir_}) {
        std::filesystem::create_directories(dir, ec);
        if (ec) {
            error = "cannot create PCH cache directory '" + pathText(dir) +
                    "': " + ec.message();
            return false;
        }
    }
    return true;
}

std::string PchCache::cacheKey(const std::filesystem::path& header,
                               const std::filesystem::path& gch) const {
    return sha256Hex(std::string(kCacheVersion) + "\n" +
                     canonicalKeyPath(header) + "\n" +
                     canonicalKeyPath(gch));
}

std::string PchCache::optionsHash() const {
    std::ostringstream text;
    text << "pch-options-1\n";
    text << "tool=" << context_.toolVersion << '\n';
    text << "compiler="
         << (context_.compiler.empty()
                 ? std::string()
                 : canonicalKeyPath(context_.compiler))
         << '\n';
    text << "compilerVersion=" << context_.compilerVersion << '\n';
    text << "standard=" << context_.standard << '\n';
    text << "inputCharset=" << context_.inputCharset << '\n';
    text << "executionCharset=" << context_.executionCharset << '\n';
    text << "unsigned=" << boolText(context_.unsignedChar) << '\n';
    for (const auto& dir : context_.includeDirs) {
        text << "I=" << canonicalKeyPath(dir) << '\n';
    }
    for (const auto& dir : context_.systemDirs) {
        text << "S=" << canonicalKeyPath(dir) << '\n';
    }
    for (const auto& file : context_.forcedIncludes) {
        text << "F=" << canonicalKeyPath(file) << '\n';
    }
    for (const auto& action : context_.commandActions) {
        text << "D=" << action << '\n';
    }
    return sha256Hex(text.str());
}

std::filesystem::path PchCache::manifestPath(std::string_view key) const {
    return manifestsDir_ / (std::string(key) + ".meta");
}

std::filesystem::path PchCache::artifactPath(std::string_view key) const {
    return artifactsDir_ / (std::string(key) + ".gch");
}

std::filesystem::path PchCache::dependencyPath(std::string_view key) const {
    return dependenciesDir_ / (std::string(key) + ".d");
}

std::filesystem::path PchCache::optionsStampPath(std::string_view key) const {
    return stampsDir_ / (std::string(key) + ".options");
}

std::filesystem::path PchCache::lockPath(std::string_view key) const {
    return locksDir_ / (std::string(key) + ".lock");
}

bool PchCache::loadManifest(std::string_view key, Manifest& manifest) const {
    const auto path = manifestPath(key);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::map<std::string, std::string> fields;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::size_t equal = line.find('=');
        if (equal == std::string::npos) {
            return false;
        }
        const auto value = decodeField(line.substr(equal + 1));
        if (!value) {
            return false;
        }
        fields.emplace(line.substr(0, equal), *value);
    }
    if (fields["schema"] != kSchema || fields["key"] != key) {
        return false;
    }
    const auto required = [&fields](std::string_view name) {
        return fields.find(std::string(name)) != fields.end();
    };
    for (const std::string_view name :
         {"header", "headerHash", "headerSize", "headerMtime", "gch",
          "gchHash", "gchSize", "gchMtime", "compiler", "compilerVersion",
          "compilerHash", "compilerSize", "compilerMtime", "optionsHash",
          "snapshot",
          "snapshotHash", "snapshotSize", "toolVersion"}) {
        if (!required(name)) {
            return false;
        }
    }
    try {
        manifest.key = fields["key"];
        manifest.header = fields["header"];
        manifest.headerHash = fields["headerHash"];
        manifest.headerSize = std::stoull(fields["headerSize"]);
        manifest.headerMtime = std::stoll(fields["headerMtime"]);
        manifest.gch = fields["gch"];
        manifest.gchHash = fields["gchHash"];
        manifest.gchSize = std::stoull(fields["gchSize"]);
        manifest.gchMtime = std::stoll(fields["gchMtime"]);
        manifest.compiler = fields["compiler"];
        manifest.compilerVersion = fields["compilerVersion"];
        manifest.compilerHash = fields["compilerHash"];
        manifest.compilerSize = std::stoull(fields["compilerSize"]);
        manifest.compilerMtime = std::stoll(fields["compilerMtime"]);
        manifest.optionsHash = fields["optionsHash"];
        manifest.snapshot = fields["snapshot"];
        manifest.snapshotHash = fields["snapshotHash"];
        manifest.snapshotSize = std::stoull(fields["snapshotSize"]);
        manifest.toolVersion = fields["toolVersion"];
    } catch (...) {
        return false;
    }
    return true;
}

bool PchCache::writeManifest(std::string_view key, const Manifest& manifest,
                             std::string& error) const {
    std::ostringstream text;
    text << "schema=" << kSchema << '\n';
    const auto field = [&text](std::string_view name, const auto& value) {
        text << name << '=' << encodeField(std::to_string(value)) << '\n';
    };
    const auto stringField = [&text](std::string_view name,
                                     std::string_view value) {
        text << name << '=' << encodeField(value) << '\n';
    };
    stringField("key", manifest.key);
    stringField("header", manifest.header);
    stringField("headerHash", manifest.headerHash);
    field("headerSize", manifest.headerSize);
    field("headerMtime", manifest.headerMtime);
    stringField("gch", manifest.gch);
    stringField("gchHash", manifest.gchHash);
    field("gchSize", manifest.gchSize);
    field("gchMtime", manifest.gchMtime);
    stringField("compiler", manifest.compiler);
    stringField("compilerVersion", manifest.compilerVersion);
    stringField("compilerHash", manifest.compilerHash);
    field("compilerSize", manifest.compilerSize);
    field("compilerMtime", manifest.compilerMtime);
    stringField("optionsHash", manifest.optionsHash);
    stringField("snapshot", manifest.snapshot);
    stringField("snapshotHash", manifest.snapshotHash);
    field("snapshotSize", manifest.snapshotSize);
    stringField("toolVersion", manifest.toolVersion);
    return writeFileAtomic(manifestPath(key), text.str(), error);
}

bool PchCache::writeArtifact(std::string_view key,
                             const std::filesystem::path& gch,
                             std::string& hash, std::string& error) const {
    std::uintmax_t size = 0;
    if (!copyFileAtomic(gch, artifactPath(key), hash, size, error)) {
        return false;
    }
    return true;
}

bool PchCache::writeDependency(std::string_view key,
                               const std::filesystem::path& header,
                               const std::filesystem::path& gch,
                               std::string& error) const {
    const auto stamp = optionsStampPath(key);
    const auto manifest = manifestPath(key);
    std::ostringstream text;
    text << makeEscape(pathText(canonicalPath(gch)), true) << ": "
         << makeEscape(pathText(canonicalPath(header)), false);
    if (!context_.compiler.empty()) {
        text << ' ' << makeEscape(pathText(context_.compiler), false);
    }
    text << ' ' << makeEscape(pathText(stamp), false)
         << ' ' << makeEscape(pathText(manifest), false) << '\n';
    return writeFileAtomic(dependencyPath(key), text.str(), error);
}

bool PchCache::writeOptionsStamp(std::string_view key,
                                 std::string& error) const {
    return writeFileAtomic(optionsStampPath(key),
                           optionsHash() + "\n", error);
}

void PchCache::updateStats(const PchCacheStats& delta) {
    stats_.lookups += delta.lookups;
    stats_.hits += delta.hits;
    stats_.misses += delta.misses;
    stats_.stale += delta.stale;
    stats_.corrupt += delta.corrupt;
    stats_.errors += delta.errors;
    stats_.created += delta.created;
    stats_.updated += delta.updated;
    stats_.evicted += delta.evicted;
    stats_.lockWaits += delta.lockWaits;
    stats_.lockTimeouts += delta.lockTimeouts;
}

bool PchCache::acquireLock(std::string_view key, Lock& lock) {
    const auto path = lockPath(key);
    const auto start = std::chrono::steady_clock::now();
    bool waited = false;
    while (true) {
        std::error_code ec;
        if (std::filesystem::create_directory(path, ec) && !ec) {
            if (waited) {
                ++stats_.lockWaits;
            }
            const std::filesystem::path owner = path / "owner";
            std::ofstream out(owner, std::ios::binary | std::ios::trunc);
            if (out) {
                out << "pid=" << std::hash<std::thread::id>{}(
                                        std::this_thread::get_id())
                    << "\n";
            }
            lock.path = path;
            lock.held = true;
            return true;
        }
        waited = true;
        std::error_code timeError;
        const auto modified = std::filesystem::last_write_time(path, timeError);
        const auto age = std::chrono::steady_clock::now() - start;
        if (!timeError) {
            const auto now = std::filesystem::file_time_type::clock::now();
            const auto lockAge = now - modified;
            // A lock is stolen only after a considerably longer grace period
            // than the normal wait timeout. Otherwise two processes could
            // repeatedly steal each other's just-created locks.
            const auto staleAfter = std::chrono::milliseconds(
                std::max(60000, options_.lockTimeoutMs * 10));
            if (lockAge > staleAfter) {
                std::filesystem::remove_all(path, timeError);
                continue;
            }
        }
        if (age > std::chrono::milliseconds(options_.lockTimeoutMs)) {
            ++stats_.lockTimeouts;
            ++stats_.errors;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void PchCache::logVerbose(const std::string& message) const {
    if (options_.verbose) {
        std::cerr << "cruciblecpp: pch-cache: " << message << '\n';
    }
}

PchCacheResult PchCache::observe(const std::filesystem::path& header,
                                 const std::filesystem::path& gch) {
    PchCacheResult result;
    if (!enabled_) {
        return result;
    }
    ++stats_.lookups;
    const std::string key = cacheKey(header, gch);
    Lock lock;
    if (!acquireLock(key, lock)) {
        result.status = PchCacheStatus::Error;
        result.detail = "could not acquire PCH cache lock";
        return result;
    }

    const auto headerSize = fileSize(header);
    const auto gchSize = fileSize(gch);
    const auto headerMtime = writeTime(header);
    const auto gchMtime = writeTime(gch);
    if (!headerSize || !gchSize || !headerMtime || !gchMtime) {
        ++stats_.stale;
        result.status = PchCacheStatus::Stale;
        result.detail = "header or PCH is missing";
        return result;
    }
    if (*gchSize == 0 || !hasGccPchMagic(gch)) {
        ++stats_.corrupt;
        result.status = PchCacheStatus::Corrupt;
        result.detail = "PCH is truncated or has an invalid GCC PCH header";
        return result;
    }
    if (*headerMtime > *gchMtime) {
        ++stats_.stale;
        result.status = PchCacheStatus::Stale;
        result.detail = "PCH is older than its header";
        return result;
    }

    const std::string currentOptions = optionsHash();
    const std::string currentCompiler =
        context_.compiler.empty() ? std::string()
                                  : canonicalKeyPath(context_.compiler);
    const auto compilerSize =
        context_.compiler.empty() ? std::optional<std::uintmax_t>(0)
                                  : fileSize(context_.compiler);
    const auto compilerMtime =
        context_.compiler.empty()
            ? std::optional<long long>(0)
            : writeTime(context_.compiler);
    const auto compilerHash =
        context_.compiler.empty()
            ? std::optional<std::string>(std::string())
            : hashFile(context_.compiler);
    if (!compilerHash) {
        ++stats_.errors;
        result.status = PchCacheStatus::Error;
        result.detail = "cannot hash compiler executable";
        return result;
    }
    Manifest manifest;
    const bool hasManifest = loadManifest(key, manifest);
    bool artifactsChanged = false;
    bool manifestUsable = hasManifest;
    const bool gchStatChanged =
        hasManifest &&
        (manifest.gchSize != *gchSize || manifest.gchMtime != *gchMtime);
    if (manifestUsable) {
        if (manifest.snapshot != pathText(artifactPath(key)) ||
            manifest.optionsHash != currentOptions ||
            manifest.toolVersion != toolVersion_ ||
            manifest.compiler != currentCompiler ||
            manifest.compilerVersion != context_.compilerVersion ||
            !compilerHash || manifest.compilerHash != *compilerHash ||
            manifest.compilerSize != compilerSize.value_or(0) ||
            manifest.compilerMtime != compilerMtime.value_or(0)) {
            manifestUsable = false;
            artifactsChanged = gchStatChanged;
        }
    }

    if (manifestUsable) {
        const auto hash = hashFile(header);
        if (!hash || *hash != manifest.headerHash) {
            manifestUsable = false;
            artifactsChanged = gchStatChanged;
        }
    }

    if (manifestUsable) {
        // Always verify the local PCH bytes. A same-size/same-mtime
        // corruption must not produce a false hit.
        const auto hash = hashFile(gch);
        if (!hash) {
            ++stats_.errors;
            result.status = PchCacheStatus::Error;
            result.detail = "cannot hash PCH artifact";
            return result;
        }
        if (*hash != manifest.gchHash) {
            manifestUsable = false;
            if (gchStatChanged) {
                artifactsChanged = true;
            } else {
                ++stats_.corrupt;
                result.status = PchCacheStatus::Corrupt;
                result.detail = "PCH checksum mismatch without a file change";
            }
        }
    }

    if (manifestUsable) {
        const auto snapshot = artifactPath(key);
        const auto hash = hashFile(snapshot);
        const auto snapshotSize = fileSize(snapshot);
        if (!hash || !snapshotSize ||
            *snapshotSize != manifest.snapshotSize ||
            *hash != manifest.snapshotHash) {
            manifestUsable = false;
            ++stats_.corrupt;
            result.status = PchCacheStatus::Corrupt;
            result.detail = "PCH cache snapshot checksum mismatch";
        }
    }

    if (manifestUsable) {
        ++stats_.hits;
        result.status = PchCacheStatus::Hit;
        result.detail = "cache hit";
        std::string error;
        if (!writeDependency(key, header, gch, error) ||
            !writeOptionsStamp(key, error)) {
            ++stats_.errors;
            logVerbose(error);
        }
        logVerbose("hit " + pathText(gch));
        return result;
    }

    if (hasManifest && !artifactsChanged &&
        result.status != PchCacheStatus::Corrupt) {
        ++stats_.stale;
        result.status = PchCacheStatus::Stale;
        result.detail = "compiler, header, or preprocessing options changed";
        logVerbose("stale " + pathText(gch) + ": " + result.detail);
        return result;
    }
    if (result.status == PchCacheStatus::Corrupt && !artifactsChanged) {
        logVerbose("corrupt " + pathText(gch) + ": " + result.detail);
        return result;
    }

    // No usable manifest, or the artifact was regenerated. Populate a fresh
    // entry from the bytes currently on disk. The header-newer check above
    // prevents adopting a stale PCH.
    std::string artifactHash;
    std::string error;
    if (!writeArtifact(key, gch, artifactHash, error)) {
        ++stats_.errors;
        result.status = PchCacheStatus::Error;
        result.detail = error;
        return result;
    }
    const auto headerHash = hashFile(header);
    if (!headerHash) {
        ++stats_.errors;
        result.status = PchCacheStatus::Error;
        result.detail = "cannot hash PCH header";
        return result;
    }
    const auto snapshotSize = fileSize(artifactPath(key));
    if (!snapshotSize) {
        ++stats_.errors;
        result.status = PchCacheStatus::Error;
        result.detail = "cannot stat PCH cache snapshot";
        return result;
    }
    Manifest fresh;
    fresh.key = key;
    fresh.header = canonicalKeyPath(header);
    fresh.headerHash = *headerHash;
    fresh.headerSize = *headerSize;
    fresh.headerMtime = *headerMtime;
    fresh.gch = canonicalKeyPath(gch);
    fresh.gchHash = artifactHash;
    fresh.gchSize = *gchSize;
    fresh.gchMtime = *gchMtime;
    fresh.compiler = currentCompiler;
    fresh.compilerVersion = context_.compilerVersion;
    fresh.compilerHash = compilerHash.value_or(std::string());
    fresh.compilerSize = compilerSize.value_or(0);
    fresh.compilerMtime = compilerMtime.value_or(0);
    fresh.optionsHash = currentOptions;
    fresh.snapshot = pathText(artifactPath(key));
    fresh.snapshotHash = artifactHash;
    fresh.snapshotSize = *snapshotSize;
    fresh.toolVersion = toolVersion_;
    if (!writeManifest(key, fresh, error) ||
        !writeOptionsStamp(key, error) ||
        !writeDependency(key, header, gch, error)) {
        ++stats_.errors;
        result.status = PchCacheStatus::Error;
        result.detail = error;
        return result;
    }
    ++stats_.misses;
    if (hasManifest) {
        ++stats_.updated;
    } else {
        ++stats_.created;
    }
    result.status = PchCacheStatus::Miss;
    result.detail = hasManifest ? "cache refreshed" : "cache populated";
    logVerbose(result.detail + " " + pathText(gch));
    return result;
}
std::uintmax_t PchCache::entrySize(std::string_view key) const {
    std::uintmax_t total = 0;
    for (const auto& path : {artifactPath(key), manifestPath(key),
                             dependencyPath(key), optionsStampPath(key)}) {
        const auto size = fileSize(path);
        if (size) {
            total += *size;
        }
    }
    return total;
}

void PchCache::gc() {
    std::error_code ec;
    struct Entry {
        std::string key;
        std::filesystem::path artifact;
        std::uintmax_t bytes = 0;
        std::filesystem::file_time_type access;
    };
    std::vector<Entry> entries;
    for (const auto& item :
         std::filesystem::directory_iterator(artifactsDir_, ec)) {
        if (ec || !item.is_regular_file()) {
            continue;
        }
        const std::string name = item.path().filename().string();
        if (name.size() <= 4 || name.substr(name.size() - 4) != ".gch") {
            continue;
        }
        const std::string key = name.substr(0, name.size() - 4);
        if (std::filesystem::exists(lockPath(key))) {
            continue;
        }
        const auto access = std::filesystem::last_write_time(item.path(), ec);
        entries.push_back({key, item.path(), entrySize(key),
                           ec ? std::filesystem::file_time_type::min() : access});
    }
    std::sort(entries.begin(), entries.end(),
              [](const Entry& left, const Entry& right) {
                  return left.access < right.access;
              });

    const auto now = std::filesystem::file_time_type::clock::now();
    const auto maxAge = std::chrono::hours(24 * std::max(0, options_.maxAgeDays));
    std::uintmax_t total = 0;
    for (const auto& entry : entries) {
        total += entry.bytes;
    }
    std::size_t count = entries.size();

    auto removeEntry = [&](const Entry& entry) {
        std::error_code removeError;
        std::filesystem::remove(entry.artifact, removeError);
        std::filesystem::remove(manifestPath(entry.key), removeError);
        std::filesystem::remove(dependencyPath(entry.key), removeError);
        std::filesystem::remove(optionsStampPath(entry.key), removeError);
        total -= entry.bytes;
        --count;
        ++stats_.evicted;
    };

    for (const auto& entry : entries) {
        if (count > options_.maxEntries || total > options_.maxBytes ||
            (options_.maxAgeDays > 0 && now - entry.access > maxAge)) {
            removeEntry(entry);
        }
    }

    // Remove manifests/dependencies/stamps whose artifact no longer exists.
    // This keeps the cache directory bounded even after interrupted writes.
    std::unordered_set<std::string> liveKeys;
    {
        std::error_code keyError;
        for (const auto& item :
             std::filesystem::directory_iterator(artifactsDir_, keyError)) {
            if (keyError || !item.is_regular_file()) {
                break;
            }
            const std::string name = item.path().filename().string();
            if (name.size() > 4 &&
                name.compare(name.size() - 4, 4, ".gch") == 0) {
                liveKeys.insert(name.substr(0, name.size() - 4));
            }
        }
    }
    auto removeOrphan = [&](const std::filesystem::path& directory,
                            std::string_view suffix) {
        std::error_code orphanError;
        for (const auto& item :
             std::filesystem::directory_iterator(directory, orphanError)) {
            if (orphanError || !item.is_regular_file()) {
                break;
            }
            const std::string name = item.path().filename().string();
            if (name.size() <= suffix.size() ||
                name.compare(name.size() - suffix.size(), suffix.size(),
                             suffix) != 0) {
                continue;
            }
            const std::string key =
                name.substr(0, name.size() - suffix.size());
            if (liveKeys.find(key) == liveKeys.end()) {
                std::error_code removeError;
                std::filesystem::remove(item.path(), removeError);
            }
        }
    };
    removeOrphan(manifestsDir_, ".meta");
    removeOrphan(dependenciesDir_, ".d");
    removeOrphan(stampsDir_, ".options");

    // Remove stale lock directories left by crashed processes and old temp
    // files. Live locks are left alone.
    for (const auto& item : std::filesystem::directory_iterator(locksDir_, ec)) {
        if (ec) {
            break;
        }
        const auto time = std::filesystem::last_write_time(item.path(), ec);
        if (!ec &&
            std::filesystem::file_time_type::clock::now() - time >
                std::chrono::milliseconds(options_.lockTimeoutMs * 4)) {
            std::filesystem::remove_all(item.path(), ec);
        }
    }
    for (const auto& item : std::filesystem::directory_iterator(tempDir_, ec)) {
        if (ec) {
            break;
        }
        const auto time = std::filesystem::last_write_time(item.path(), ec);
        if (!ec &&
            std::filesystem::file_time_type::clock::now() - time >
                std::chrono::hours(1)) {
            std::filesystem::remove_all(item.path(), ec);
        }
    }

    stats_.entries = count;
    stats_.bytes = total;
}

void PchCache::finish() {
    if (!enabled_ || finished_) {
        return;
    }
    finished_ = true;
    gc();
    std::string error;
    if (!writeStats(error)) {
        logVerbose(error);
    }
    if (options_.verbose) {
        logVerbose("lookups=" + std::to_string(stats_.lookups) +
                   " hits=" + std::to_string(stats_.hits) +
                   " misses=" + std::to_string(stats_.misses) +
                   " stale=" + std::to_string(stats_.stale) +
                   " corrupt=" + std::to_string(stats_.corrupt) +
                   " hitRate=" + std::to_string(stats_.hitRate()));
    }
}

bool PchCache::writeStats(std::string& error) const {
    if (options_.statsFile.empty()) {
        return true;
    }
    std::ostringstream text;
    text << "{\"schema\":1"
         << ",\"lookups\":" << stats_.lookups
         << ",\"hits\":" << stats_.hits
         << ",\"misses\":" << stats_.misses
         << ",\"stale\":" << stats_.stale
         << ",\"corrupt\":" << stats_.corrupt
         << ",\"errors\":" << stats_.errors
         << ",\"created\":" << stats_.created
         << ",\"updated\":" << stats_.updated
         << ",\"evicted\":" << stats_.evicted
         << ",\"lockWaits\":" << stats_.lockWaits
         << ",\"lockTimeouts\":" << stats_.lockTimeouts
         << ",\"entries\":" << stats_.entries
         << ",\"bytes\":" << stats_.bytes
         << ",\"hitRate\":" << std::fixed << std::setprecision(6)
         << stats_.hitRate() << "}\n";
    return writeFileAtomic(options_.statsFile, text.str(), error);
}

} // namespace crucible
