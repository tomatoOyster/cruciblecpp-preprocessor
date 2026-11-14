#include <nlohmann/json.hpp>

#include <string>

int main() {
    nlohmann::json value = nlohmann::json::parse(R"({"answer": 42})");
    return value["answer"].get<int>() == 42 ? 0 : 1;
}
