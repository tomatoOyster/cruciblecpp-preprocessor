#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
int main(){ std::vector<std::string> v{"b","a"}; std::sort(v.begin(),v.end()); std::cout << v[0] << "\n"; return 0; }
