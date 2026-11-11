#include "Preprocessor.h"

#include <cstdlib>
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

void printUsage(std::ostream& out) {
    out << "Usage: cruciblecpp-preprocessor [options] <input.cpp>\n"
           "\n"
           "Options:\n"
           "  -o, --output <file>          Write output to <file> (default: stdout)\n"
           "  -I, --include-dir <dir>      Add a user include directory (repeatable)\n"
           "  -isystem <dir>               Add a system include directory (repeatable)\n"
           "  -D, --define <name[=value]>  Define a macro (repeatable, ordered)\n"
           "  -U, --undef <name>           Undefine a macro (repeatable, ordered)\n"
           "  -include <file>              Force-include a file (repeatable)\n"
           "  -P, --no-line-markers        Do not emit #line markers\n"
           "  -C, --keep-comments          Preserve comments in the output\n"
           "  -std=<c++17|c++20|c++23>     Set __cplusplus (default: c++20)\n"
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
    const std::vector<std::string> arguments = collectArguments(argc, argv);

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
            std::cout << "cruciblecpp-preprocessor 1.0.2\n";
            return 0;
        }
        if (arg == "-o" || arg == "--output") {
            options.output = pathFromUtf8(nextValue(arg.c_str()));
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
            if (i < arguments.size()) {
                options.input = pathFromUtf8(arguments[i]);
                ++i;
            }
            if (i < arguments.size()) {
                std::cerr << "error: only one input file is supported\n";
                return 2;
            }
            break;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "error: unknown option '" << arg << "'\n";
            printUsage(std::cerr);
            return 2;
        }
        if (!options.input.empty()) {
            std::cerr << "error: only one input file is supported\n";
            return 2;
        }
        options.input = pathFromUtf8(arg);
    }

    if (options.input.empty()) {
        std::cerr << "error: no input file\n";
        printUsage(std::cerr);
        return 2;
    }
    if (options.output == std::filesystem::path("-")) {
        options.output.clear();
    }

    crucible::Preprocessor preprocessor(std::move(options));
    return preprocessor.run();
}
