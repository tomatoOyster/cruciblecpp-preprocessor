#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

// Flip one byte after the GCC PCH magic while preserving size and mtime.
// This specifically tests content-hash corruption detection, not the easier
// "truncated file" path.
int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
#ifdef _WIN32
    (void)argv;
#endif
    std::filesystem::path path;
#ifdef _WIN32
    int wideArgc = 0;
    wchar_t** wideArgv =
        CommandLineToArgvW(GetCommandLineW(), &wideArgc);
    if (!wideArgv || wideArgc != 2) {
        if (wideArgv) {
            LocalFree(wideArgv);
        }
        return 2;
    }
    path = std::filesystem::path(wideArgv[1]);
    LocalFree(wideArgv);
#else
    path = std::filesystem::path(argv[1]);
#endif
    std::error_code ec;
    const auto oldTime = std::filesystem::last_write_time(path, ec);
    if (ec) {
        return 3;
    }
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> bytes(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    if (bytes.size() < 16) {
        return 4;
    }
    bytes[12] ^= 0xFF;
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            return 5;
        }
    }
    std::filesystem::last_write_time(path, oldTime, ec);
    return ec ? 6 : 0;
}
