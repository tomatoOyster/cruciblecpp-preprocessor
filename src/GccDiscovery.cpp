#include "GccDiscovery.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <system_error>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace crucible {
namespace {

#ifdef _WIN32

std::wstring utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0);
    UINT codePage = CP_UTF8;
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (size <= 0) {
        codePage = CP_ACP;
        flags = 0;
        size = MultiByteToWideChar(codePage, flags, text.data(),
                                   static_cast<int>(text.size()), nullptr, 0);
    }
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(codePage, flags, text.data(),
                        static_cast<int>(text.size()), result.data(), size);
    return result;
}

std::wstring trimWide(std::wstring text) {
    auto isSpace = [](wchar_t c) {
        return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
    };
    while (!text.empty() && isSpace(text.front())) {
        text.erase(text.begin());
    }
    while (!text.empty() && isSpace(text.back())) {
        text.pop_back();
    }
    return text;
}

bool isRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec &&
           std::filesystem::is_regular_file(path, ec) && !ec;
}

std::filesystem::path findCompiler() {
    std::wstring pathValue;
    DWORD size = GetEnvironmentVariableW(L"PATH", nullptr, 0);
    if (size > 0) {
        pathValue.resize(size);
        DWORD written = GetEnvironmentVariableW(L"PATH", pathValue.data(), size);
        if (written > 0 && written < size) {
            pathValue.resize(written);
        } else {
            pathValue.clear();
        }
    }

    std::vector<std::wstring> names = {L"g++.exe", L"gcc.exe", L"c++.exe"};
    std::size_t start = 0;
    while (start <= pathValue.size()) {
        const std::size_t end = pathValue.find(L';', start);
        std::wstring dir = trimWide(pathValue.substr(
            start, end == std::wstring::npos ? std::wstring::npos : end - start));
        if (dir.size() >= 2 && dir.front() == L'"' && dir.back() == L'"') {
            dir = dir.substr(1, dir.size() - 2);
        }
        if (!dir.empty()) {
            for (const auto& name : names) {
                const std::filesystem::path candidate =
                    std::filesystem::path(dir) / name;
                if (isRegularFile(candidate)) {
                    return candidate;
                }
            }
        }
        if (end == std::wstring::npos) {
            break;
        }
        start = end + 1;
    }

    for (const auto& name : names) {
        wchar_t buffer[MAX_PATH];
        const DWORD length =
            SearchPathW(nullptr, name.c_str(), nullptr, MAX_PATH, buffer,
                        nullptr);
        if (length > 0 && length < MAX_PATH) {
            const std::filesystem::path candidate(std::wstring(buffer, length));
            if (isRegularFile(candidate)) {
                return candidate;
            }
        }
    }
    return {};
}

std::string captureCompilerVerbose(const std::filesystem::path& compiler,
                                   std::string& error) {
    wchar_t tempDir[MAX_PATH + 1];
    const DWORD tempDirLength =
        GetTempPathW(MAX_PATH + 1, tempDir);
    if (tempDirLength == 0 || tempDirLength > MAX_PATH) {
        error = "GetTempPath failed";
        return {};
    }
    wchar_t tempFile[MAX_PATH + 1];
    if (GetTempFileNameW(tempDir, L"ccp", 0, tempFile) == 0) {
        error = "GetTempFileName failed";
        return {};
    }

    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    HANDLE output = CreateFileW(
        tempFile, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &attributes,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
        nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = "failed to create temporary compiler output";
        return {};
    }
    HANDLE input = CreateFileW(L"NUL", GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE) {
        CloseHandle(output);
        error = "failed to open NUL";
        return {};
    }

    std::wstring command = L"\"" + compiler.wstring() +
                           L"\" -std=c++20 -E -v -x c++ -";
    std::vector<wchar_t> commandLine(command.begin(), command.end());
    commandLine.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdInput = input;
    startup.hStdOutput = output;
    startup.hStdError = output;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        compiler.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    if (!created) {
        CloseHandle(input);
        CloseHandle(output);
        error = "failed to run compiler";
        return {};
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, 20000);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        error = "compiler timed out while querying include paths";
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(input);

    SetFilePointer(output, 0, nullptr, FILE_BEGIN);
    std::string text;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(output, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        text.append(buffer, read);
    }
    CloseHandle(output);
    if (waitResult == WAIT_TIMEOUT) {
        return {};
    }
    return text;
}

std::filesystem::path outputPathFromString(const std::string& text) {
    return std::filesystem::path(utf8ToWide(text));
}

#else

std::filesystem::path findCompiler() { return {}; }
std::string captureCompilerVerbose(const std::filesystem::path&,
                                   std::string& error) {
    error = "GCC discovery is only implemented on Windows";
    return {};
}
std::filesystem::path outputPathFromString(const std::string& text) {
    return std::filesystem::path(text);
}

#endif

std::string trim(std::string text) {
    auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() && isSpace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

bool isDirectory(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_directory(path, ec) && !ec;
}

std::string normalizedKey(const std::filesystem::path& path) {
    std::filesystem::path normalized = path.lexically_normal();
    std::string key = normalized.generic_string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
#endif
    return key;
}

} // namespace

GccDiscovery discoverGccFromPath() {
    GccDiscovery result;
    const std::filesystem::path compiler = findCompiler();
    if (compiler.empty()) {
        result.error = "g++/gcc not found in PATH";
        return result;
    }
    result.found = true;
    result.compiler = compiler;

    std::string error;
    const std::string output = captureCompilerVerbose(compiler, error);
    if (output.empty()) {
        result.error = error.empty() ? "compiler produced no output" : error;
        return result;
    }

    std::size_t start = 0;
    bool inList = false;
    std::vector<std::string> seen;
    while (start <= output.size()) {
        const std::size_t end = output.find('\n', start);
        std::string line = trim(output.substr(
            start, end == std::string::npos ? std::string::npos : end - start));
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!inList) {
            if (line.find("gcc version ") != std::string::npos) {
                result.version = trim(line.substr(
                    line.find("gcc version ") + std::strlen("gcc version ")));
            }
            if (line.find("search starts here") != std::string::npos) {
                inList = true;
            }
        } else {
            if (line.find("End of search list") != std::string::npos) {
                break;
            }
            const std::string frameworkSuffix = " (framework directory)";
            if (line.size() >= frameworkSuffix.size() &&
                line.compare(line.size() - frameworkSuffix.size(),
                             frameworkSuffix.size(), frameworkSuffix) == 0) {
                line.erase(line.size() - frameworkSuffix.size());
            }
            const bool absoluteUnix = !line.empty() && line.front() == '/';
            const bool absoluteWindows =
                line.size() >= 3 &&
                std::isalpha(static_cast<unsigned char>(line[0])) &&
                line[1] == ':';
            if (!absoluteUnix && !absoluteWindows) {
                if (end == std::string::npos) {
                    break;
                }
                start = end + 1;
                continue;
            }
            std::filesystem::path dir = outputPathFromString(line);
            if (isDirectory(dir)) {
                std::error_code ec;
                const std::filesystem::path canonical =
                    std::filesystem::weakly_canonical(dir, ec);
                if (!ec) {
                    dir = canonical;
                } else {
                    dir = dir.lexically_normal();
                }
                const std::string key = normalizedKey(dir);
                if (std::find(seen.begin(), seen.end(), key) == seen.end()) {
                    seen.push_back(key);
                    result.includeDirs.push_back(dir);
                }
            }
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    if (result.includeDirs.empty()) {
        result.error = "could not parse GCC include search list";
    }
    return result;
}

} // namespace crucible
