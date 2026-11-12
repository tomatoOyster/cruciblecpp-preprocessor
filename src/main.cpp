#include "Charset.h"
#include "GccDiscovery.h"
#include "Preprocessor.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
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
           "  --pch=ignore|warn|error|report\n"
           "                               Precompiled-header policy (default: ignore)\n"
           "  --pch-cache <dir>             Enable the PCH artifact cache\n"
           "  --pch-cache-stats <file>      Write per-run cache hit-rate JSON\n"
           "  --pch-cache-max-entries <n>   Maximum cached PCH entries (default: 256)\n"
           "  --pch-cache-max-size <bytes>  Maximum cache bytes (default: 512 MiB)\n"
           "  --pch-cache-max-age <days>    Evict entries older than N days (default: 30)\n"
           "  --pch-cache-lock-timeout <ms> Lock wait timeout (default: 10000)\n"
           "  --pch-cache-gc                Force cleanup (normal runs also enforce limits)\n"
           "  -MD, -MMD                    Generate a CMake/Ninja depfile\n"
           "  -MF <file>                   Dependency file path\n"
           "  -MT <target>                 Dependency target\n"
           "  -MQ <target>                 Dependency target (Make-escaped)\n"
           "  -MP                          Accepted for CMake compatibility\n"
           "  -Werror                      Treat warnings as errors\n"
           "  -W<name> / -Wno-<name>       Enable/disable a warning\n"
           "  -Werror=<name>               Treat one warning as an error\n"
           "  -Wno-error=<name>            Keep one warning non-fatal\n"
           "  -Wsystem-headers             Show warnings from system headers\n"
           "  -Wno-system-headers          Hide system-header warnings (default)\n"
           "  -fdiagnostics-color=<mode>   auto | always | never\n"
           "  -fno-diagnostics-color       Disable diagnostic colors\n"
           "  -fdiagnostics-show-caret     Show source line and caret (default)\n"
           "  -fno-diagnostics-show-caret  Disable source line and caret\n"
           "  -fdiagnostics-show-option    Append [-W...] (default)\n"
           "  -fno-diagnostics-show-option Do not append [-W...]\n"
           "  --strict                     Treat unknown pragmas as errors\n"
           "  --drop-unknown-pragmas       Drop unknown #pragma directives\n"
           "  --no-output-on-error         Do not write output after an error\n"
           "  --max-errors <n>             Stop after n errors (default: 20)\n"
           "  --max-include-depth <n>      Include nesting limit (default: 200)\n"
           "  -v, --verbose                Print processing information\n"
           "  -newyear                     Print days until next January 1 and exit\n"
           "  --help                       Show this help\n"
           "  --version                    Show version\n";
}

bool parseUnsigned(std::string_view text, std::uintmax_t& value) {
    try {
        std::size_t consumed = 0;
        const unsigned long long parsed =
            std::stoull(std::string(text), &consumed);
        if (consumed != text.size()) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool stderrIsTerminal() {
#ifdef _WIN32
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(fileno(stderr)) != 0;
#endif
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
    bool colorSpecified = false;
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
            std::cout << "cruciblecpp-preprocessor 2.6.0\n";
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
        if (arg == "-trigraphs" || arg == "--trigraphs") {
            options.trigraphs = true;
            continue;
        }
        if (arg == "-fdiagnostics-show-caret") {
            options.showDiagnosticCaret = true;
            continue;
        }
        if (arg == "-fno-diagnostics-show-caret") {
            options.showDiagnosticCaret = false;
            continue;
        }
        if (arg == "-fdiagnostics-show-option") {
            options.showDiagnosticOption = true;
            continue;
        }
        if (arg == "-fno-diagnostics-show-option") {
            options.showDiagnosticOption = false;
            continue;
        }
        if (arg == "-fno-diagnostics-color") {
            options.colorDiagnostics = false;
            colorSpecified = true;
            continue;
        }
        if (arg.rfind("-fdiagnostics-color=", 0) == 0) {
            const std::string mode = arg.substr(20);
            if (mode == "always") {
                options.colorDiagnostics = true;
            } else if (mode == "never") {
                options.colorDiagnostics = false;
            } else if (mode == "auto") {
                options.colorDiagnostics = stderrIsTerminal();
            } else {
                std::cerr << "error: unknown -fdiagnostics-color mode '"
                          << mode << "'\n";
                return 2;
            }
            colorSpecified = true;
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
        if (arg == "--pch" || arg.rfind("--pch=", 0) == 0) {
            const std::string value = arg == "--pch"
                                          ? nextValue(arg.c_str())
                                          : arg.substr(6);
            if (value == "ignore") {
                options.pchPolicy = crucible::PchPolicy::Ignore;
            } else if (value == "warn") {
                options.pchPolicy = crucible::PchPolicy::Warn;
            } else if (value == "error") {
                options.pchPolicy = crucible::PchPolicy::Error;
            } else if (value == "report") {
                options.pchPolicy = crucible::PchPolicy::Report;
            } else {
                std::cerr << "error: --pch expects ignore, warn, error, or report\n";
                return 2;
            }
            continue;
        }
        if (arg == "--pch-cache") {
            options.pchCacheDir = pathFromUtf8(nextValue(arg.c_str()));
            continue;
        }
        if (arg == "--pch-cache-stats") {
            options.pchCacheStatsFile =
                pathFromUtf8(nextValue(arg.c_str()));
            continue;
        }
        if (arg == "--pch-cache-max-entries") {
            int value = 0;
            if (!parseInteger(nextValue(arg.c_str()), value) || value <= 0) {
                std::cerr << "error: --pch-cache-max-entries expects a positive integer\n";
                return 2;
            }
            options.pchCacheMaxEntries = static_cast<std::size_t>(value);
            continue;
        }
        if (arg == "--pch-cache-max-size") {
            std::uintmax_t value = 0;
            if (!parseUnsigned(nextValue(arg.c_str()), value) || value == 0) {
                std::cerr << "error: --pch-cache-max-size expects a positive byte count\n";
                return 2;
            }
            options.pchCacheMaxBytes = value;
            continue;
        }
        if (arg == "--pch-cache-max-age") {
            int value = 0;
            if (!parseInteger(nextValue(arg.c_str()), value) || value < 0) {
                std::cerr << "error: --pch-cache-max-age expects a non-negative day count\n";
                return 2;
            }
            options.pchCacheMaxAgeDays = value;
            continue;
        }
        if (arg == "--pch-cache-lock-timeout") {
            int value = 0;
            if (!parseInteger(nextValue(arg.c_str()), value) || value <= 0) {
                std::cerr << "error: --pch-cache-lock-timeout expects a positive millisecond count\n";
                return 2;
            }
            options.pchCacheLockTimeoutMs = value;
            continue;
        }
        if (arg == "--pch-cache-gc") {
            options.pchCacheCleanup = true;
            continue;
        }
        if (arg == "-Winvalid-pch" || arg == "-Wno-invalid-pch") {
            // GCC warning controls; PCH is never parsed by this tool.
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
        if (arg == "-Wno-error") {
            options.warningsAsErrors = false;
            continue;
        }
        if (arg == "-Wall") {
            for (const char* option :
                 {"-Wcomment", "-Wendif-labels", "-Wmultichar",
                  "-Wtrigraphs", "-Wunknown-pragmas", "-Wpragmas",
                  "-Wcpp", "-Winvalid-pch"}) {
                options.diagnosticStates[option] = crucible::Severity::Warning;
            }
            continue;
        }
        if (arg == "-Wextra" || arg == "-Wpedantic" || arg == "-pedantic") {
            options.diagnosticStates[arg == "-Wextra" ? "-Wextra"
                                                       : "-Wpedantic"] =
                crucible::Severity::Warning;
            continue;
        }
        if (arg == "-Wsystem-headers") {
            options.showSystemHeaderWarnings = true;
            continue;
        }
        if (arg == "-Wno-system-headers") {
            options.showSystemHeaderWarnings = false;
            continue;
        }
        if (arg.rfind("-Werror=", 0) == 0) {
            std::string option = arg.substr(8);
            if (option.empty() || option.front() != '-') {
                option = "-W" + option;
            }
            options.diagnosticStates[option] = crucible::Severity::Error;
            continue;
        }
        if (arg.rfind("-Wno-error=", 0) == 0) {
            std::string option = arg.substr(11);
            if (option.empty() || option.front() != '-') {
                option = "-W" + option;
            }
            options.diagnosticStates[option] = crucible::Severity::Warning;
            continue;
        }
        if (arg.rfind("-Wno-", 0) == 0) {
            options.diagnosticStates["-W" + arg.substr(5)] =
                crucible::Severity::Note;
            continue;
        }
        if (arg.rfind("-W", 0) == 0 && arg.size() > 2) {
            options.diagnosticStates[arg] = crucible::Severity::Warning;
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

    if (!colorSpecified) {
        options.colorDiagnostics = stderrIsTerminal();
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
