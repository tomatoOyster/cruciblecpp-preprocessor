#pragma once

#include <string_view>

namespace crucible {

bool gccBuiltinAvailable(std::string_view name, int gccMajor = 15);
int gccBuiltinValue(std::string_view name, int gccMajor = 15);
bool gccAttributeAvailable(std::string_view name, int gccMajor = 15);
int gccCppAttributeValue(std::string_view name, std::string_view standard,
                         int gccMajor = 15);
bool gccFeatureAvailable(std::string_view name, bool extension,
                         std::string_view standard, int gccMajor = 15);

} // namespace crucible
