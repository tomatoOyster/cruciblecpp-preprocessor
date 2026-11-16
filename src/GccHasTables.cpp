#include "GccHasTables.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>

#include "GccHasData.inc"
#include "GccHasDataGcc13.inc"
#include "GccHasDataGcc14.inc"

namespace crucible {
namespace {

unsigned standardMask(std::string_view standard) {
    if (standard == "c++11") {
        return 1u;
    }
    if (standard == "c++14") {
        return 1u;
    }
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

template <typename Builtins, typename Values>
int builtinValueImpl(const Builtins& builtins, const Values& values,
                     std::string_view name) {
    if (!contains(builtins, name)) {
        return 0;
    }
    const auto begin = std::begin(values);
    const auto end = std::end(values);
    const auto found = std::lower_bound(
        begin, end, name,
        [](const auto& entry, std::string_view value) {
            return entry.name < value;
        });
    if (found == end || found->name != name) {
        return 1;
    }
    return found->value;
}

template <typename Attributes>
bool attributeImpl(const Attributes& attributes, std::string_view name);

std::string normalizeAttribute(std::string_view name) {
    std::string value(name);
    while (value.size() >= 4 && value.rfind("__", 0) == 0 &&
           value.compare(value.size() - 2, 2, "__") == 0) {
        value = value.substr(2, value.size() - 4);
    }
    return value;
}

template <typename Attributes>
bool attributeImpl(const Attributes& attributes, std::string_view name) {
    return contains(attributes, normalizeAttribute(name));
}

template <typename CppAttributes>
int cppAttributeImpl(const CppAttributes& attributes, std::string_view name,
                     std::string_view standard) {
    const std::string normalized = normalizeAttribute(name);
    const unsigned mask = standardMask(standard);
    const auto begin = std::begin(attributes);
    const auto end = std::end(attributes);
    const auto found = std::lower_bound(
        begin, end, normalized,
        [](const auto& entry, std::string_view value) {
            return entry.name < value;
        });
    if (found == end || found->name != normalized) {
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

template <typename Features>
bool featureImpl(const Features& features, std::string_view name,
                 bool extension, std::string_view standard) {
    const unsigned mask = standardMask(standard);
    const auto begin = std::begin(features);
    const auto end = std::end(features);
    const auto found = std::lower_bound(
        begin, end, name,
        [](const auto& entry, std::string_view value) {
            return entry.name < value;
        });
    if (found == end || found->name != name) {
        return false;
    }
    const unsigned supported =
        extension ? found->extensions : found->features;
    return (supported & mask) != 0;
}

} // namespace

bool gccBuiltinAvailable(std::string_view name, int gccMajor) {
    return gccBuiltinValue(name, gccMajor) != 0;
}

int gccBuiltinValue(std::string_view name, int gccMajor) {
    if (gccMajor == 13) {
        return builtinValueImpl(gcc13_data::kBuiltins,
                                gcc13_data::kBuiltinValues, name);
    }
    if (gccMajor == 14) {
        return builtinValueImpl(gcc14_data::kBuiltins,
                                gcc14_data::kBuiltinValues, name);
    }
    return builtinValueImpl(gcc_data::kBuiltins, gcc_data::kBuiltinValues,
                            name);
}

bool gccAttributeAvailable(std::string_view name, int gccMajor) {
    if (gccMajor == 13) {
        return attributeImpl(gcc13_data::kAttributes, name);
    }
    if (gccMajor == 14) {
        return attributeImpl(gcc14_data::kAttributes, name);
    }
    return attributeImpl(gcc_data::kAttributes, name);
}

int gccCppAttributeValue(std::string_view name, std::string_view standard,
                         int gccMajor) {
    if (gccMajor == 13) {
        return cppAttributeImpl(gcc13_data::kCppAttributes, name, standard);
    }
    if (gccMajor == 14) {
        return cppAttributeImpl(gcc14_data::kCppAttributes, name, standard);
    }
    return cppAttributeImpl(gcc_data::kCppAttributes, name, standard);
}

bool gccFeatureAvailable(std::string_view name, bool extension,
                         std::string_view standard, int gccMajor) {
    if (gccMajor == 13) {
        return featureImpl(gcc13_data::kFeatures, name, extension, standard);
    }
    if (gccMajor == 14) {
        return featureImpl(gcc14_data::kFeatures, name, extension, standard);
    }
    return featureImpl(gcc_data::kFeatures, name, extension, standard);
}

} // namespace crucible
