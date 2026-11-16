#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace crucible {

struct GccDiscovery {
    bool found = false;
    std::filesystem::path compiler;
    std::string version;
    std::vector<std::filesystem::path> includeDirs;
    std::string error;
};

// Locate g++/gcc on PATH and query its implicit C++ include search list.
GccDiscovery discoverGccFromPath();

// Query a specific compiler path.
GccDiscovery discoverGcc(const std::filesystem::path& compiler);

// All compiler paths found on PATH, in PATH order.
std::vector<std::filesystem::path> gccCandidatesFromPath();

} // namespace crucible
