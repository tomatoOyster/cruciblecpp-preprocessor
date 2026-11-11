// Real libstdc++ headers through explicit -isystem paths from CMake.
#include <cstddef>
#include <type_traits>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <memory>

#if !__has_include(<vector>)
#error <vector> should be found through the explicit -isystem paths
#endif

static_assert(__GNUC__ == 15, "GCC 15.2 profile");
static_assert(std::is_same_v<std::size_t, decltype(sizeof(0))>, "size_t");
static_assert(std::is_same_v<std::vector<int>::value_type, int>, "vector");

int main() {
    std::vector<std::string> values;
    values.emplace_back("hello");
    std::sort(values.begin(), values.end());
    std::unique_ptr<int> value = std::make_unique<int>(7);
    return values.empty() || *value != 7 ? 1 : 0;
}
