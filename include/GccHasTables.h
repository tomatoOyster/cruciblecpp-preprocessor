#pragma once

#include <string_view>

namespace crucible {

bool gccBuiltinAvailable(std::string_view name);
int gccBuiltinValue(std::string_view name);
bool gccAttributeAvailable(std::string_view name);
int gccCppAttributeValue(std::string_view name, std::string_view standard);
bool gccFeatureAvailable(std::string_view name, bool extension,
                         std::string_view standard);

} // namespace crucible
