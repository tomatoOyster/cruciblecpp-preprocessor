#include <filesystem>
#include <chrono>
#include <string>

int main() {
    std::filesystem::path path = "x";
    auto now = std::chrono::steady_clock::now();
    (void)now;
    return path.empty() ? 1 : 0;
}
