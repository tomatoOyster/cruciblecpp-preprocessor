#include <vector>
#include <string>
#include <iostream>

int main() {
    std::vector<std::string> values{"a", "b"};
    std::cout << values.size();
    return values.size() == 2 ? 0 : 1;
}
