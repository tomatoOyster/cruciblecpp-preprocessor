??=include <cstddef>
??=define TRIGRAPH_VALUE 11
int values??(2??) = ??<TRIGRAPH_VALUE, TRIGRAPH_VALUE??>;
static_assert(sizeof(values) == 2 * sizeof(int), "trigraph brackets");
int main() { return values??(0??) == 11 ? 0 : 1; }
