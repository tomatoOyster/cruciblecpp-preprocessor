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

} // namespace crucible
