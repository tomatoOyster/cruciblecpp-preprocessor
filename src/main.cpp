#include "Charset.h"
#include "GccDiscovery.h"
#include "Preprocessor.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

std::filesystem::path pathFromUtf8(std::string_view text) {
    const std::u8string value(
        reinterpret_cast<const char8_t*>(text.data()), text.size());
    return std::filesystem::path(value);
}

std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()),
                       value.size());
}

#ifdef _WIN32
std::string wideToUtf8(const wchar_t* text) {
    if (!text) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr,
                        nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}
#endif

std::vector<std::string> collectArguments(int argc, char** argv) {
#ifdef _WIN32
    int wideCount = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &wideCount);
    if (wide) {
        std::vector<std::string> result;
        result.reserve(static_cast<std::size_t>(wideCount));
        for (int i = 0; i < wideCount; ++i) {
            result.push_back(wideToUtf8(wide[i]));
        }
        LocalFree(wide);
        return result;
    }
#endif
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) {
        result.emplace_back(argv[i]);
    }
    return result;
}

int daysUntilNewYear() {
    int yearValue = 0;
    unsigned monthValue = 0;
    unsigned dayValue = 0;
#ifdef _WIN32
    SYSTEMTIME local{};
    GetLocalTime(&local);
    yearValue = local.wYear;
    monthValue = local.wMonth;
    dayValue = local.wDay;
#else
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _POSIX_VERSION
    localtime_r(&now, &local);
#else
    local = *std::localtime(&now);
#endif
    yearValue = local.tm_year + 1900;
    monthValue = static_cast<unsigned>(local.tm_mon + 1);
    dayValue = static_cast<unsigned>(local.tm_mday);
#endif

    using namespace std::chrono;
    const year_month_day today{
        year{yearValue}, month{monthValue}, day{dayValue}};
    if (!today.ok()) {
        return 0;
    }
    if (today.month() == January && today.day() == day{1}) {
        return 0;
    }
    const year_month_day nextNewYear{
        year{yearValue + 1}, January, day{1}};
    return static_cast<int>((sys_days{nextNewYear} - sys_days{today}).count());
}

void printUsage(std::ostream& out) {
    out << "Usage: cruciblecpp-preprocessor [options] <input.cpp...>\n"
           "\n"
           "Options:\n"
           "  -o, --output <file...>       Output file(s), in input order (default: stdout)\n"
           "  -I, --include-dir <dir>      Add a user include directory (repeatable)\n"
           "  -isystem <dir>               Add a system include directory (repeatable)\n"
           "  -nostdinc                    Do not auto-discover GCC include directories\n"
           "  --no-gcc-includes            Same as -nostdinc\n"
           "  --print-gcc-includes         Print discovered GCC include paths and exit\n"
           "  -D, --define <name[=value]>  Define a macro (repeatable, ordered)\n"
           "  -U, --undef <name>           Undefine a macro (repeatable, ordered)\n"
           "  -include <file>              Force-include a file (repeatable)\n"
           "  -P, --no-line-markers        Do not emit #line markers\n"
           "  -C, --keep-comments          Preserve comments in the output\n"
           "  -std=<c++17|c++20|c++23>     Set __cplusplus (default: c++20)\n"
           "  -finput-charset=<name>       Source charset (default: UTF-8)\n"
           "  -fexec-charset=<name>        Execution charset for char constants (default: UTF-8)\n"
           "  -funsigned-char              Treat plain char as unsigned\n"
           "  -fsigned-char                Treat plain char as signed (default)\n"
           "  --analysis-json <file>       Write include edges and macro trace JSON\n"
           "  -MD, -MMD                    Generate a CMake/Ninja depfile\n"
           "  -MF <file>                   Dependency file path\n"
           "  -MT <target>                 Dependency target\n"
           "  -MQ <target>                 Dependency target (Make-escaped)\n"
           "  -MP                          Accepted for CMake compatibility\n"
           "  -Werror                      Treat warnings as errors\n"
           "  --strict                     Treat unknown directives/pragmas as errors\n"
           "  --drop-unknown-pragmas       Drop unknown #pragma directives\n"
           "  --no-output-on-error         Do not write output after an error\n"
           "  --max-errors <n>             Stop after n errors (default: 20)\n"
           "  --max-include-depth <n>      Include nesting limit (default: 200)\n"
           "  -v, --verbose                Print processing information\n"
           "  -newyear                     Print days until next January 1 and exit\n"
           "  --help                       Show this help\n"
           "  --version                    Show version\n";
}

bool parseInteger(std::string_view text, int& value) {
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(std::string(text), &consumed);
        if (consumed != text.size()) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

int main(int argc, char** argv) {
    crucible::PreprocessorOptions options;
    std::vector<std::filesystem::path> inputs;
    std::vector<std::filesystem::path> outputs;
    const std::vector<std::string> arguments = collectArguments(argc, argv);

    // This is intentionally handled before all other arguments: when
    // -newyear is present, every other option/file is ignored.
    if (std::find(arguments.begin() + 1, arguments.end(), "-newyear") !=
        arguments.end()) {
        std::cout << daysUntilNewYear() << '\n';
        return 0;
    }

    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const std::string arg = arguments[i];
        auto nextValue = [&](const char* option) -> std::string {
            if (i + 1 >= arguments.size()) {
                std::cerr << "error: missing value for " << option << '\n';
                std::exit(2);
            }
            return arguments[++i];
        };

        if (arg == "--help" || arg == "-h") {
            printUsage(std::cout);
            return 0;
        }
        if (arg == "--version") {
            std::cout << "cruciblecpp-preprocessor 2.2.0\n";
            return 0;
        }
        if (arg == "-o" || arg == "--output") {
            outputs.push_back(pathFromUtf8(nextValue(arg.c_str())));
            // Multi-file form: inputs first, then `-o out1 out2 ...`.
            // For the common `-o out input.cpp` form, inputs is still
            // empty here and only the single value is consumed.
            if (!inputs.empty()) {
                while (i + 1 < arguments.size() &&
                       (arguments[i + 1].empty() ||
                        arguments[i + 1].front() != '-')) {
                    outputs.push_back(pathFromUtf8(arguments[++i]));
                }
            }
            continue;
        }
        if (arg == "-I" || arg == "--include-dir") {
            options.includeDirs.push_back(
                pathFromUtf8(nextValue(arg.c_str())));
            continue;
        }
        if (arg == "-isystem") {
            options.systemDirs.push_back(
                pathFromUtf8(nextValue(arg.c_str())));
            continue;
        }
        if (arg == "-nostdinc" || arg == "--no-gcc-includes") {
            options.autoGccIncludes = false;
            continue;
        }
        if (arg == "--print-gcc-includes") {
            const crucible::GccDiscovery discovery =
                crucible::discoverGccFromPath();
            if (!discovery.found) {
                std::cerr << "error: " << discovery.error << '\n';
                return 1;
            }
            for (const auto& dir : discovery.includeDirs) {
                std::cout << pathToUtf8(dir) << '\n';
            }
            return discovery.includeDirs.empty() ? 1 : 0;
        }
        if (arg == "-D" || arg == "--define") {
            options.commandActions.push_back(
                {crucible::CommandLineAction::Kind::Define,
                 nextValue(arg.c_str())});
            continue;
        }
        if (arg == "-U" || arg == "--undef") {
            options.commandActions.push_back(
                {crucible::CommandLineAction::Kind::Undef,
                 nextValue(arg.c_str())});
            continue;
        }
        if (arg == "-include") {
            options.forcedIncludes.push_back(
                pathFromUtf8(nextValue(arg.c_str())));
            continue;
        }
        if (arg == "-P" || arg == "--no-line-markers") {
            options.lineMarkers = false;
            continue;
        }
        if (arg == "-C" || arg == "--keep-comments") {
            options.keepComments = true;
            continue;
        }
        if (arg == "-MD" || arg == "-MMD") {
            options.generateDependencies = true;
            continue;
        }
        if (arg == "--analysis-json") {
            options.analysisFile = pathFromUtf8(nextValue(arg.c_str()));
            continue;
        }
        if (arg == "-MF") {
            options.dependencyFile = pathFromUtf8(nextValue(arg.c_str()));
            options.generateDependencies = true;
            continue;
        }
        if (arg == "-MT") {
            options.dependencyTarget = nextValue(arg.c_str());
            options.generateDependencies = true;
            continue;
        }
        if (arg == "-MQ") {
            options.dependencyTarget = nextValue(arg.c_str());
            options.quoteDependencyTarget = true;
            options.generateDependencies = true;
            continue;
        }
        if (arg == "-MP") {
            continue;
        }
        if (arg == "-Werror") {
            options.warningsAsErrors = true;
            continue;
        }
        if (arg == "-Wall" || arg == "-Wextra" || arg == "-E") {
            continue;
        }
        if (arg == "--strict") {
            options.strict = true;
            continue;
        }
        if (arg == "--drop-unknown-pragmas") {
            options.dropUnknownPragmas = true;
            continue;
        }
        if (arg == "--no-output-on-error") {
            options.outputOnError = false;
            continue;
        }
        if (arg == "-v" || arg == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (arg == "--max-errors") {
            int value = 0;
            if (!parseInteger(nextValue(arg.c_str()), value) || value <= 0) {
                std::cerr << "error: --max-errors expects a positive integer\n";
                return 2;
            }
            options.maxErrors = value;
            continue;
        }
        if (arg == "--max-include-depth") {
            int value = 0;
            if (!parseInteger(nextValue(arg.c_str()), value) || value <= 0) {
                std::cerr << "error: --max-include-depth expects a positive integer\n";
                return 2;
            }
            options.maxIncludeDepth = value;
            continue;
        }
        if (arg.size() > 2 && arg.rfind("-D", 0) == 0) {
            options.commandActions.push_back(
                {crucible::CommandLineAction::Kind::Define, arg.substr(2)});
            continue;
        }
        if (arg.size() > 2 && arg.rfind("-U", 0) == 0) {
            options.commandActions.push_back(
                {crucible::CommandLineAction::Kind::Undef, arg.substr(2)});
            continue;
        }
        if (arg.size() > 2 && arg.rfind("-I", 0) == 0) {
            options.includeDirs.push_back(pathFromUtf8(arg.substr(2)));
            continue;
        }
        if (arg.size() > 8 && arg.rfind("-isystem", 0) == 0) {
            options.systemDirs.push_back(pathFromUtf8(arg.substr(8)));
            continue;
        }
        if (arg.rfind("-finput-charset=", 0) == 0) {
            options.inputCharset = arg.substr(16);
            continue;
        }
        if (arg == "-finput-charset") {
            options.inputCharset = nextValue(arg.c_str());
            continue;
        }
        if (arg.rfind("-fexec-charset=", 0) == 0) {
            options.executionCharset = arg.substr(15);
            continue;
        }
        if (arg == "-fexec-charset") {
            options.executionCharset = nextValue(arg.c_str());
            continue;
        }
        if (arg == "-funsigned-char") {
            options.unsignedChar = true;
            continue;
        }
        if (arg == "-fsigned-char") {
            options.unsignedChar = false;
            continue;
        }
        if (arg.rfind("-std=", 0) == 0) {
            options.standard = arg.substr(5);
            continue;
        }
        if (arg.rfind("--std=", 0) == 0) {
            options.standard = arg.substr(6);
            continue;
        }
        if (arg == "--") {
            ++i;
            while (i < arguments.size()) {
                inputs.push_back(pathFromUtf8(arguments[i]));
                ++i;
            }
            break;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            printUsage(std::cerr);
            return 2;
        }
        inputs.push_back(pathFromUtf8(arg));
    }

    if (inputs.empty()) {
        std::cerr << "error: no input file\n";
        printUsage(std::cerr);
        return 2;
    }
    if (outputs.empty()) {
        if (inputs.size() != 1) {
            std::cerr << "error: multiple inputs require matching -o outputs\n";
            return 2;
        }
        outputs.emplace_back();
    }
    if (outputs.size() != inputs.size()) {
        std::cerr << "error: " << inputs.size() << " input file(s) but "
                  << outputs.size() << " output file(s)\n";
        return 2;
    }
    if (options.generateDependencies && inputs.size() > 1) {
        std::cerr << "error: depfile generation supports one input at a time\n";
        return 2;
    }

    crucible::CharsetEncoding charset;
    std::string charsetError;
    if (!crucible::parseCharset(options.inputCharset, charset, charsetError)) {
        std::cerr << "error: " << charsetError << '\n';
        return 2;
    }
    if (!crucible::parseCharset(options.executionCharset, charset,
                                charsetError)) {
        std::cerr << "error: " << charsetError << '\n';
        return 2;
    }
    if (charset.kind == crucible::CharsetEncoding::Kind::Utf16LE ||
        charset.kind == crucible::CharsetEncoding::Kind::Utf16BE) {
        std::cerr << "error: UTF-16 is not supported as an execution charset\n";
        return 2;
    }

    int result = 0;
    for (std::size_t index = 0; index < inputs.size(); ++index) {
        crucible::PreprocessorOptions fileOptions = options;
        fileOptions.input = inputs[index];
        fileOptions.output = outputs[index];
        if (fileOptions.output == std::filesystem::path("-")) {
            fileOptions.output.clear();
        }
        if (fileOptions.output.empty() && inputs.size() > 1) {
            std::cerr << "error: empty output path for multi-file mode\n";
            return 2;
        }
        crucible::Preprocessor preprocessor(std::move(fileOptions));
        const int code = preprocessor.run();
        if (code != 0 && result == 0) {
            result = code;
        }
    }
    return result;
}
