#include "GccHasTables.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>

#include "GccHasData.inc"

namespace crucible {
namespace {

unsigned standardMask(std::string_view standard) {
    if (standard == "c++17") {
        return 1u;
    }
    if (standard == "c++23") {
        return 4u;
    }
    return 2u;
}

template <std::size_t N>
bool contains(const std::string_view (&values)[N], std::string_view name) {
    return std::binary_search(std::begin(values), std::end(values), name);
}

std::string normalizeAttribute(std::string_view name) {
    std::string value(name);
    while (value.size() >= 4 && value.rfind("__", 0) == 0 &&
           value.compare(value.size() - 2, 2, "__") == 0) {
        value = value.substr(2, value.size() - 4);
    }
    return value;
}

} // namespace

bool gccBuiltinAvailable(std::string_view name) {
    return contains(gcc_data::kBuiltins, name);
}

bool gccAttributeAvailable(std::string_view name) {
    const std::string normalized = normalizeAttribute(name);
    return contains(gcc_data::kAttributes, normalized);
}

int gccCppAttributeValue(std::string_view name, std::string_view standard) {
    const unsigned mask = standardMask(standard);
    const auto begin = std::begin(gcc_data::kCppAttributes);
    const auto end = std::end(gcc_data::kCppAttributes);
    const auto found = std::lower_bound(
        begin, end, name,
        [](const gcc_data::CppAttributeSupport& entry,
           std::string_view value) { return entry.name < value; });
    if (found == end || found->name != name) {
        return 0;
    }
    if (mask == 1u) {
        return found->cxx17;
    }
    if (mask == 4u) {
        return found->cxx23;
    }
    return found->cxx20;
}

bool gccFeatureAvailable(std::string_view name, bool extension,
                         std::string_view standard) {
    const unsigned mask = standardMask(standard);
    const auto begin = std::begin(gcc_data::kFeatures);
    const auto end = std::end(gcc_data::kFeatures);
    const auto found = std::lower_bound(
        begin, end, name,
        [](const gcc_data::FeatureSupport& entry, std::string_view value) {
            return entry.name < value;
        });
    if (found == end || found->name != name) {
        return false;
    }
    const unsigned supported =
        extension ? found->extensions : found->features;
    return (supported & mask) != 0;
}

} // namespace crucible
