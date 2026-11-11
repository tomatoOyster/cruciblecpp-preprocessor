// MinGW Windows SDK headers: generated `defined` and many pragmas/macros.
#include <windows.h>

int main() {
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors == 0 ? 1 : 0;
}
