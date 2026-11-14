#include <fmt/format.h>

int main() {
    return fmt::format("{:d}", 42) == "42" ? 0 : 1;
}
