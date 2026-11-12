#include "Preprocessor.h"

#include "Charset.h"
#include "ConditionalEngine.h"
#include "FileReader.h"
#include "GccDiscovery.h"
#include "GccHasTables.h"
#include "IncludeHandler.h"
#include "MacroExpander.h"
#include "MacroRegistry.h"
#include "OutputWriter.h"
#include "SpecialOperator.h"
#include "Tokenizer.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>
#include <utility>

namespace crucible {
namespace {

// Generated from:
//   g++ -std=c++20 -dM -E -x c++ -
// on MinGW GCC 15.2.0. __cplusplus is intentionally omitted so that the
// -std option controls it.
#include "GccProfileData.inc"

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

bool isRegularFile(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec &&
           std::filesystem::is_regular_file(path, ec) && !ec;
}

std::string joinTokenText(const std::vector<Token>& tokens) {
    std::string text;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::Placemarker ||
            token.kind == TokenKind::Newline ||
            token.kind == TokenKind::Comment || token.text.empty()) {
            continue;
        }
        if (!text.empty()) {
            text.push_back(' ');
        }
        text += token.text;
        if (text.size() > 2000) {
            text += " ...";
            break;
        }
    }
    return text;
}

std::string jsonEscape(std::string_view text) {
    std::string result;
    result.reserve(text.size() + 8);
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"':
            result += "\\\"";
            break;
        case '\\':
            result += "\\\\";
            break;
        case '\b':
            result += "\\b";
            break;
        case '\f':
            result += "\\f";
            break;
        case '\n':
            result += "\\n";
            break;
        case '\r':
            result += "\\r";
            break;
        case '\t':
            result += "\\t";
            break;
        default:
            if (c < 0x20) {
                const char hex[] = "0123456789abcdef";
                result += "\\u00";
                result.push_back(hex[(c >> 4) & 0xF]);
                result.push_back(hex[c & 0xF]);
            } else {
                result.push_back(ch);
            }
            break;
        }
    }
    return result;
}

bool isDirectiveLine(const std::vector<Token>& line) {
    for (const auto& token : line) {
        if (token.kind == TokenKind::Comment) {
            continue;
        }
        return token.isPunct("#");
    }
    return false;
}

std::string trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

bool isIdentifierName(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(name[0]);
    if (!(name[0] == '_' || std::isalpha(first) || first >= 0x80)) {
        return false;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if (!(name[i] == '_' || std::isalnum(c) || c >= 0x80)) {
            return false;
        }
    }
    return true;
}

std::string stripQuotes(std::string_view text) {
    const std::size_t first = text.find('"');
    const std::size_t last = text.rfind('"');
    if (first != std::string_view::npos && last != std::string_view::npos &&
        first < last) {
        return std::string(text.substr(first + 1, last - first - 1));
    }
    return std::string(text);
}

Token makeNumber(std::string text, const SourceLocation& loc) {
    Token token;
    token.kind = TokenKind::PPNumber;
    token.text = std::move(text);
    token.loc = loc;
    return token;
}

bool parseHeaderFromTokens(const std::vector<Token>& tokens, std::size_t& index,
                           std::string& name, bool& angled) {
    while (index < tokens.size() && tokens[index].isTrivia()) {
        ++index;
    }
    if (index >= tokens.size()) {
        return false;
    }
    if (tokens[index].kind == TokenKind::StringLiteral) {
        name = stripQuotes(tokens[index].text);
        angled = false;
        ++index;
        return true;
    }
    if (tokens[index].isPunct("<")) {
        ++index;
        std::string result;
        while (index < tokens.size()) {
            if (tokens[index].isPunct(">")) {
                ++index;
                name = std::move(result);
                angled = true;
                return true;
            }
            if (!result.empty() && tokens[index].leadingSpace) {
                result.push_back(' ');
            }
            result.append(tokens[index].text);
            ++index;
        }
        return false;
    }
    return false;
}

bool replaceDefined(
    const std::vector<Token>& input,
    const std::function<bool(std::string_view)>& isDefined,
    std::vector<Token>& output, DiagnosticSink& diagnostics) {
    bool ok = true;
    for (std::size_t i = 0; i < input.size();) {
        if (!input[i].isIdentifier("defined")) {
            output.push_back(input[i]);
            ++i;
            continue;
        }

        std::string name;
        std::size_t advance = 1;
        if (i + 1 < input.size() && input[i + 1].isPunct("(")) {
            if (i + 3 < input.size() &&
                input[i + 2].kind == TokenKind::Identifier &&
                input[i + 3].isPunct(")")) {
                name = input[i + 2].text;
                advance = 4;
            } else {
                diagnostics.error(input[i].loc,
                                  "invalid operand to 'defined'");
                ok = false;
            }
        } else if (i + 1 < input.size() &&
                   input[i + 1].kind == TokenKind::Identifier) {
            name = input[i + 1].text;
            advance = 2;
        } else {
            diagnostics.error(input[i].loc, "invalid operand to 'defined'");
            ok = false;
        }

        const bool defined = !name.empty() && isDefined(name);
        output.push_back(
            makeNumber(defined ? "1" : "0", input[i].loc));
        i += advance;
    }
    return ok;
}

bool isGccDefinedOperator(std::string_view name) {
    // GCC 15.2 makes these operator names visible to #ifdef/#if defined()
    // even though they are not ordinary predefined macros.
    return name == "__has_include" || name == "__has_include_next" ||
           name == "__has_attribute" || name == "__has_cpp_attribute" ||
           name == "__has_feature" || name == "__has_extension" ||
           name == "__has_builtin";
}

bool parseCppAttributeName(const std::vector<Token>& tokens,
                           std::size_t openIndex, std::string& name,
                           std::size_t& closeIndex) {
    if (openIndex >= tokens.size() || !tokens[openIndex].isPunct("(")) {
        return false;
    }
    std::size_t index = openIndex + 1;
    while (index < tokens.size() && tokens[index].isTrivia()) {
        ++index;
    }
    if (index >= tokens.size() ||
        tokens[index].kind != TokenKind::Identifier) {
        return false;
    }
    name = tokens[index].text;
    ++index;
    while (index + 2 < tokens.size() &&
           tokens[index].isPunct("::") &&
           tokens[index + 1].kind == TokenKind::Identifier) {
        name += "::";
        name += tokens[index + 1].text;
        index += 2;
    }
    while (index < tokens.size() && tokens[index].isTrivia()) {
        ++index;
    }
    if (index >= tokens.size() || !tokens[index].isPunct(")")) {
        return false;
    }
    closeIndex = index;
    return true;
}

bool isAlternativeOperator(std::string_view name) {
    return name == "and" || name == "or" || name == "not" ||
           name == "bitand" || name == "bitor" || name == "xor" ||
           name == "compl" || name == "not_eq";
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

int stdVersionFor(std::string_view standard) {
    const std::string value = lowercase(std::string(standard));
    if (value == "c++17" || value == "c++1z") {
        return 201703;
    }
    if (value == "c++23" || value == "c++2b") {
        return 202302;
    }
    return 202002;
}

std::optional<std::pair<std::string, std::string>> splitDefineSpec(
    std::string_view spec) {
    const std::size_t equal = spec.find('=');
    if (equal == std::string_view::npos) {
        return std::pair<std::string, std::string>{std::string(spec), "1"};
    }
    return std::pair<std::string, std::string>{
        std::string(spec.substr(0, equal)),
        std::string(spec.substr(equal + 1))};
}

bool parsePositiveDecimal(std::string_view text, int& value) {
    std::string digits;
    for (char c : text) {
        if (c != '\'') {
            digits.push_back(c);
        }
    }
    if (digits.empty()) {
        return false;
    }
    std::int64_t parsed = 0;
    const auto result = std::from_chars(digits.data(),
                                        digits.data() + digits.size(), parsed);
    if (result.ec != std::errc() ||
        result.ptr != digits.data() + digits.size() || parsed <= 0 ||
        parsed > 2147483647) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

std::string joinTokens(const std::vector<Token>& tokens, std::size_t start) {
    std::string result;
    for (std::size_t i = start; i < tokens.size(); ++i) {
        if (i > start) {
            result.push_back(' ');
        }
        result.append(tokens[i].text);
    }
    return result;
}

std::optional<std::string> extractQuotedArgument(std::string_view text) {
    const std::size_t quote = text.find('"');
    if (quote == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t end = text.find('"', quote + 1);
    if (end == std::string_view::npos) {
        return std::nullopt;
    }
    return std::string(text.substr(quote + 1, end - quote - 1));
}

} // namespace

DiagnosticSink::DiagnosticSink(int maxErrors) : maxErrors_(maxErrors) {}

void DiagnosticSink::report(Severity severity, const SourceLocation& loc,
                            std::string message) {
    if (severity == Severity::Warning && warningsAsErrors_) {
        severity = Severity::Error;
    }

    if (severity == Severity::Error || severity == Severity::Fatal) {
        ++errorCount_;
        if (limitReached_) {
            return;
        }
    }

    if (!loc.presumedFile.empty()) {
        std::cerr << loc.presumedFile << ':' << loc.line << ':'
                  << loc.column << ": ";
    } else if (loc.valid() && loc.file) {
        std::cerr << loc.file->displayPath << ':' << loc.line << ':'
                  << loc.column << ": ";
    } else {
        std::cerr << "<command line>: ";
    }

    switch (severity) {
    case Severity::Note:
        std::cerr << "note: ";
        break;
    case Severity::Warning:
        std::cerr << "warning: ";
        break;
    case Severity::Error:
        std::cerr << "error: ";
        break;
    case Severity::Fatal:
        std::cerr << "fatal error: ";
        break;
    }
    std::cerr << message << '\n';

    if (!includeStack_.empty()) {
        for (auto it = includeStack_.rbegin() + 1;
             it != includeStack_.rend(); ++it) {
            std::cerr << "  included from " << *it << '\n';
        }
    }

    if (severity == Severity::Fatal) {
        fatal_ = true;
    }
    if ((severity == Severity::Error || severity == Severity::Fatal) &&
        errorCount_ >= maxErrors_) {
        limitReached_ = true;
        std::cerr << "fatal error: too many errors (" << errorCount_
                  << "), stopping\n";
        fatal_ = true;
    }
}

Preprocessor::Preprocessor(PreprocessorOptions options)
    : options_(std::move(options)),
      diagnostics_(options_.maxErrors),
      macros_(std::make_unique<MacroRegistry>()),
      includes_(std::make_unique<IncludeHandler>(options_.includeDirs,
                                                 options_.systemDirs)),
      special_(std::make_unique<SpecialOperator>(diagnostics_)),
      writer_(std::make_unique<OutputWriter>(options_.lineMarkers)) {
    diagnostics_.setWarningsAsErrors(options_.warningsAsErrors);

    if (options_.autoGccIncludes) {
        const GccDiscovery discovery = discoverGccFromPath();
        if (discovery.found) {
            compilerPath_ = pathToUtf8(discovery.compiler);
            compilerVersion_ = discovery.version;
            if (options_.verbose) {
                std::cerr << "cruciblecpp: GCC";
                if (!discovery.version.empty()) {
                    std::cerr << ' ' << discovery.version;
                }
                std::cerr << " at " << pathToUtf8(discovery.compiler)
                          << '\n';
            }
            for (const auto& dir : discovery.includeDirs) {
                if (options_.verbose) {
                    std::cerr << "cruciblecpp: include " << pathToUtf8(dir)
                              << '\n';
                }
                options_.systemDirs.push_back(dir);
            }
            if (!discovery.error.empty() && options_.verbose) {
                std::cerr << "cruciblecpp: GCC discovery: "
                          << discovery.error << '\n';
            }
        } else if (options_.verbose) {
            std::cerr << "cruciblecpp: GCC discovery failed: "
                      << discovery.error << '\n';
        }

        // Preserve explicit -isystem order and remove duplicates introduced by
        // GCC's search list or by overlapping explicit directories.
        std::vector<std::filesystem::path> merged;
        std::unordered_set<std::string> seen;
        for (const auto& dir : options_.systemDirs) {
            std::string key = pathToUtf8(dir.lexically_normal());
#ifdef _WIN32
            std::transform(key.begin(), key.end(), key.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
#endif
            if (seen.insert(key).second) {
                merged.push_back(dir);
            }
        }
        options_.systemDirs = std::move(merged);
        includes_ = std::make_unique<IncludeHandler>(options_.includeDirs,
                                                     options_.systemDirs);
    }

    expander_ = std::make_unique<MacroExpander>(*macros_, diagnostics_,
                                                *special_);
    expander_->setBuiltinHandler([this](const Token& token)
                                     -> std::optional<std::vector<Token>> {
        BuiltinContext context;
        context.file = currentPresumedFile_;
        context.baseFile = baseFile_;
        context.includeLevel = includeLevel_;
        context.stdVersion = stdVersionFor(options_.standard);
        context.counter = &counter_;
        return special_->expandBuiltin(token, context);
    });

    expander_->setTraceHandlers(
        [this](const MacroTraceStart& info) {
            if (options_.analysisFile.empty()) {
                return;
            }
            const auto normalized = [](std::string value) {
#ifdef _WIN32
                std::transform(value.begin(), value.end(), value.begin(),
                               [](unsigned char c) {
                                   return static_cast<char>(std::tolower(c));
                               });
#endif
                std::replace(value.begin(), value.end(), '\\', '/');
                return value;
            };
            const bool rootInvocation =
                info.invocation.file &&
                normalized(info.invocation.file->displayPath) ==
                    normalized(pathToUtf8(options_.input));
            // Keep the 5000-event budget for system headers, but always retain
            // events from the source file being previewed so macro animations
            // are not drowned out by libstdc++ setup.
            if (!rootInvocation && macroTrace_.size() >= 5000) {
                return;
            }
            if (macroTrace_.size() >= 20000) {
                return;
            }
            MacroTrace trace;
            trace.id = info.id;
            trace.parentId = info.parentId;
            trace.macro = info.macro;
            trace.kind = info.functionLike ? "function" : "object";
            trace.file = info.invocation.file
                             ? info.invocation.file->displayPath
                             : currentPresumedFile_;
            trace.line = static_cast<int>(info.invocation.line);
            trace.column = static_cast<int>(info.invocation.column);
            trace.offset = info.offset;
            trace.length = info.length;
            trace.depth = info.depth;
            trace.invocation = info.invocationText;
            trace.sourceLine = info.sourceLine;
            trace.definitionFile = info.definition.file
                                       ? info.definition.file->displayPath
                                       : std::string("<command line>");
            trace.definitionLine = static_cast<int>(info.definition.line);
            trace.definitionColumn = static_cast<int>(info.definition.column);
            trace.definitionText = info.definitionText;
            trace.arguments = info.arguments;
            macroTraceIndex_[trace.id] = macroTrace_.size();
            macroTrace_.push_back(std::move(trace));
        },
        [this](int id, const std::vector<Token>& replacement,
               const std::vector<MacroTraceArgument>& arguments) {
            const auto found = macroTraceIndex_.find(id);
            if (found == macroTraceIndex_.end()) {
                return;
            }
            auto& trace = macroTrace_[found->second];
            trace.replacement = joinTokenText(replacement);
            trace.arguments = arguments;
        },
        [this](const MacroTraceStep& step) {
            const auto found = macroTraceIndex_.find(step.parentId);
            if (found == macroTraceIndex_.end()) {
                return;
            }
            macroTrace_[found->second].steps.push_back(step);
        });

    if (!options_.pchCacheDir.empty()) {
        PchCacheOptions cacheOptions;
        cacheOptions.directory = options_.pchCacheDir;
        cacheOptions.statsFile = options_.pchCacheStatsFile;
        cacheOptions.maxEntries = options_.pchCacheMaxEntries;
        cacheOptions.maxBytes = options_.pchCacheMaxBytes;
        cacheOptions.maxAgeDays = options_.pchCacheMaxAgeDays;
        cacheOptions.lockTimeoutMs = options_.pchCacheLockTimeoutMs;
        cacheOptions.cleanup = options_.pchCacheCleanup;
        cacheOptions.verbose = options_.verbose;
        PchCacheContext context;
        context.compiler = pathFromUtf8(compilerPath_);
        context.compilerVersion = compilerVersion_;
        context.toolVersion = "2.5.2";
        context.standard = options_.standard;
        context.inputCharset = options_.inputCharset;
        context.executionCharset = options_.executionCharset;
        context.unsignedChar = options_.unsignedChar;
        context.includeDirs = options_.includeDirs;
        context.systemDirs = options_.systemDirs;
        context.forcedIncludes = options_.forcedIncludes;
        for (const auto& action : options_.commandActions) {
            context.commandActions.push_back(
                (action.kind == CommandLineAction::Kind::Define ? "D:"
                                                                : "U:") +
                action.text);
        }
        pchCache_ = std::make_unique<PchCache>(std::move(cacheOptions),
                                               std::move(context));
    }
}

Preprocessor::~Preprocessor() = default;

std::shared_ptr<SourceFile> Preprocessor::makeSyntheticFile(
    std::string_view text, std::string_view displayName) {
    auto file = std::make_shared<SourceFile>();
    file->displayPath = std::string(displayName);
    file->physicalPath = std::string(displayName);
    file->text = std::string(text);
    file->lineOffsets.push_back(0);
    for (std::size_t i = 0; i < file->text.size(); ++i) {
        if (file->text[i] == '\n') {
            file->lineOffsets.push_back(i + 1);
        }
    }
    return file;
}

bool Preprocessor::loadGccProfile() {
    static const std::unordered_set<std::string> dynamicMacros = {
        "__FILE__",       "__LINE__",       "__DATE__",
        "__TIME__",       "__TIMESTAMP__",  "__COUNTER__",
        "__INCLUDE_LEVEL__", "__BASE_FILE__", "__cplusplus"};

    std::istringstream stream(kGccProfileText);
    std::string line;
    bool ok = true;
    while (std::getline(stream, line)) {
        if (line.rfind("#define ", 0) != 0) {
            continue;
        }
        // Extract the name only for filtering dynamic/predefined entries.
        std::size_t nameBegin = 8;
        std::size_t nameEnd = nameBegin;
        while (nameEnd < line.size() && line[nameEnd] != ' ' &&
               line[nameEnd] != '(') {
            ++nameEnd;
        }
        const std::string name = line.substr(nameBegin, nameEnd - nameBegin);
        if (dynamicMacros.find(name) != dynamicMacros.end()) {
            continue;
        }

        auto synthetic = makeSyntheticFile(line, "<gcc-profile>");
        Tokenizer tokenizer;
        std::vector<Token> tokens =
            tokenizer.tokenize(synthetic, &diagnostics_, false);
        if (tokens.empty()) {
            continue;
        }
        SourceLocation loc;
        loc.file = synthetic;
        loc.line = 1;
        loc.column = 1;
        if (!processDefine(tokens, loc, true)) {
            ok = false;
        }
    }
    return ok;
}

bool Preprocessor::defineFromCommandLine(const std::string& spec) {
    const auto split = splitDefineSpec(spec);
    if (!split.has_value()) {
        diagnostics_.error({}, "invalid -D definition '" + spec + "'");
        return false;
    }

    std::string left = trim(split->first);
    std::string value = split->second;
    if (left.empty()) {
        diagnostics_.error({}, "empty macro name in -D");
        return false;
    }

    MacroDefinition definition;
    const std::size_t open = left.find('(');
    if (open != std::string::npos) {
        if (left.back() != ')') {
            diagnostics_.error({}, "invalid function-like -D definition '" +
                                      spec + "'");
            return false;
        }
        definition.name = trim(left.substr(0, open));
        definition.functionLike = true;
        const std::string params =
            left.substr(open + 1, left.size() - open - 2);
        std::size_t begin = 0;
        while (begin <= params.size()) {
            const std::size_t comma = params.find(',', begin);
            std::string param = trim(
                params.substr(begin, comma == std::string::npos
                                         ? std::string::npos
                                         : comma - begin));
            if (!param.empty()) {
                if (param == "...") {
                    definition.variadic = true;
                    definition.variadicName = "__VA_ARGS__";
                } else if (param.size() > 3 &&
                           param.compare(param.size() - 3, 3, "...") == 0) {
                    definition.variadic = true;
                    definition.variadicName = param.substr(0, param.size() - 3);
                } else {
                    if (!isIdentifierName(param)) {
                        diagnostics_.error({}, "invalid macro parameter '" +
                                                 param + "'");
                        return false;
                    }
                    definition.params.push_back(param);
                }
            }
            if (comma == std::string::npos) {
                break;
            }
            begin = comma + 1;
        }
    } else {
        definition.name = left;
    }

    if (!isIdentifierName(definition.name)) {
        diagnostics_.error({}, "invalid macro name '" + definition.name + "'");
        return false;
    }

    auto synthetic = makeSyntheticFile(value, "<command line>");
    Tokenizer tokenizer;
    definition.replacement = tokenizer.tokenize(synthetic, &diagnostics_,
                                                options_.keepComments);
    for (auto& token : definition.replacement) {
        if (token.kind == TokenKind::Newline ||
            token.kind == TokenKind::Comment) {
            token.kind = TokenKind::Placemarker;
        }
    }
    definition.replacement.erase(
        std::remove_if(definition.replacement.begin(),
                       definition.replacement.end(),
                       [](const Token& token) {
                           return token.kind == TokenKind::Placemarker;
                       }),
        definition.replacement.end());
    definition.definitionLoc.file = synthetic;
    definition.definitionLoc.line = 1;
    definition.definitionLoc.column = 1;
    definition.definitionLoc.offset = 0;
    // A command-line -D may override a profile predefined macro, matching the
    // usual compiler behavior. Source-level redefinitions remain errors.
    if (const MacroDefinition* existing = macros_->find(definition.name);
        existing && existing->isBuiltin) {
        macros_->undef(definition.name);
    }
    return macros_->define(definition, diagnostics_);
}

bool Preprocessor::applyCommandLine() {
    loadGccProfile();

    if (options_.unsignedChar) {
        defineFromCommandLine("__CHAR_UNSIGNED__=1");
    }

    CharsetEncoding execution;
    std::string charsetError;
    if (!parseCharset(options_.executionCharset, execution, charsetError)) {
        diagnostics_.error({}, charsetError);
    } else if (execution.kind != CharsetEncoding::Kind::Utf8) {
        macros_->undef("__GNUC_EXECUTION_CHARSET_NAME");
        defineFromCommandLine("__GNUC_EXECUTION_CHARSET_NAME=\"" +
                              options_.executionCharset + "\"");
    }

    for (const auto& action : options_.commandActions) {
        if (action.kind == CommandLineAction::Kind::Define) {
            defineFromCommandLine(action.text);
        } else {
            macros_->undef(action.text);
        }
    }

    for (const auto& forced : options_.forcedIncludes) {
        if (!isRegularFile(forced)) {
            std::filesystem::path pch = forced;
            pch += ".gch";
            if (isRegularFile(pch)) {
                diagnostics_.error(
                    {}, "cannot find forced include file '" +
                            pathToUtf8(forced) +
                            "'; found only precompiled header '" +
                            pathToUtf8(pch) +
                            "', and g++ -E cannot use it");
                return false;
            }
        }
        if (!checkPrecompiledHeader(forced, pathToUtf8(forced),
                                    SourceLocation{})) {
            return false;
        }
        processFile(forced, false);
    }
    return !diagnostics_.hasError();
}

int Preprocessor::run() {
    if (options_.input.empty()) {
        diagnostics_.error({}, "no input file");
        return 2;
    }

    baseFile_ = pathToUtf8(options_.input);
    currentPresumedFile_ = baseFile_;

    if (stdVersionFor(options_.standard) != 202002) {
        diagnostics_.warning(
            {}, "the hardcoded GCC 15.2 profile targets C++20; using " +
                    options_.standard + " may produce different __cpp_* macros");
    }

    applyCommandLine();
    if (!diagnostics_.limitReached()) {
        processFile(options_.input, false);
    }

    writer_->ensureNewline();
    if (!writeDependencies()) {
        return 2;
    }
    const bool wrote = writeOutput();
    if (!wrote) {
        return 2;
    }
    if (!writeAnalysis()) {
        return 2;
    }
    if (options_.verbose) {
        std::cerr << "cruciblecpp: processed " << filesProcessed_
                  << " file(s), " << diagnostics_.errorCount()
                  << " error(s)\n";
    }
    return diagnostics_.hasError() ? 1 : 0;
}

bool Preprocessor::processFile(const std::filesystem::path& path,
                               bool angledInclude) {
    (void)angledInclude;
    if (diagnostics_.limitReached() || diagnostics_.fatal()) {
        return false;
    }
    recordDependency(path);
    if (includes_->isOnce(path)) {
        return true;
    }
    if (includeLevel_ >= options_.maxIncludeDepth) {
        diagnostics_.error({}, "maximum include depth exceeded");
        return false;
    }

    FileReader reader;
    auto file = reader.read(path, diagnostics_, options_.inputCharset);
    if (!file) {
        return false;
    }
    ++filesProcessed_;

    auto previousFile = currentFile_;
    auto previousConditionals = std::move(conditionals_);
    const auto previousPath = currentPath_;
    const std::string previousPresumed = currentPresumedFile_;
    const int previousDelta = lineDelta_;
    const int previousLevel = includeLevel_;

    currentFile_ = file;
    currentPath_ = path;
    currentPresumedFile_ = file->displayPath;
    lineDelta_ = 0;
    ++includeLevel_;
    includeStack_.push_back(file->displayPath);
    diagnostics_.setIncludeStack(includeStack_);

    conditionals_ = std::make_unique<ConditionalEngine>(
        diagnostics_,
        [this](const std::vector<Token>& expression,
               const SourceLocation& loc) {
            return evaluateCondition(expression, loc);
        },
        [this](std::string_view name) {
            return macros_->contains(name) ||
                   SpecialOperator::isBuiltinName(name) ||
                   isGccDefinedOperator(name);
        });

    writer_->writeLineMarker(1, currentPresumedFile_);
    processTokenStream(file);

    if (conditionals_ && conditionals_->hasOpenFrames()) {
        conditionals_->reportUnclosed();
    }

    conditionals_.reset();
    conditionals_ = std::move(previousConditionals);
    currentFile_ = std::move(previousFile);
    currentPath_ = previousPath;
    currentPresumedFile_ = previousPresumed;
    lineDelta_ = previousDelta;
    includeLevel_ = previousLevel;
    if (!includeStack_.empty()) {
        includeStack_.pop_back();
    }
    diagnostics_.setIncludeStack(includeStack_);
    return true;
}

void Preprocessor::processTokenStream(std::shared_ptr<SourceFile> file) {
    Tokenizer tokenizer;
    const std::vector<Token> tokens =
        tokenizer.tokenize(file, &diagnostics_, options_.keepComments);

    std::vector<std::vector<Token>> lines;
    std::vector<Token> current;
    for (const auto& token : tokens) {
        if (token.kind == TokenKind::Newline) {
            // A newline inside a block comment does not end a directive that
            // has already started. It still ends empty/code/comment-only
            // lines, so a following '#' can begin a new directive.
            const bool directiveInProgress = isDirectiveLine(current);
            if (token.softNewline && directiveInProgress) {
                continue;
            }
            lines.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(token);
        }
    }
    if (!current.empty()) {
        lines.push_back(std::move(current));
    }

    auto adjustLine = [&](std::vector<Token>& lineTokens,
                          std::size_t zeroBasedLine) {
        for (auto& token : lineTokens) {
            const long long adjusted =
                static_cast<long long>(token.loc.line) + lineDelta_;
            token.loc.line = static_cast<std::uint32_t>(
                std::max<long long>(1, adjusted));
            if (!currentPresumedFile_.empty()) {
                token.loc.presumedFile = currentPresumedFile_;
            }
        }
        for (auto& token : lineTokens) {
            if (token.loc.line == 0) {
                token.loc.line = static_cast<std::uint32_t>(zeroBasedLine + 1);
            }
        }
    };

    // Do not emit a blank output line for every source line. With line
    // markers enabled, emit a marker before each text chunk so diagnostics
    // stay exact. With -P the output is deliberately compact as well.
    const bool compactGaps = true;

    for (std::size_t i = 0; i < lines.size();) {
        if (lines[i].empty()) {
            if (!compactGaps) {
                writer_->writeNewline();
            }
            ++i;
            continue;
        }

        const bool directive = isDirectiveLine(lines[i]);

        if (directive) {
            std::vector<Token> line = lines[i];
            adjustLine(line, i);
            bool wrote = false;
            processDirective(line, i, wrote);
            if (!wrote && !compactGaps) {
                writer_->writeNewline();
            }
            ++i;
            continue;
        }

        if (!conditionals_->active()) {
            if (!compactGaps) {
                writer_->writeNewline();
            }
            ++i;
            continue;
        }

        std::size_t j = i;
        std::vector<Token> chunk;
        int parenDepth = 0;
        while (j < lines.size()) {
            if (isDirectiveLine(lines[j])) {
                break;
            }
            // A top-level blank line can be omitted; the next chunk starts
            // with an explicit line marker. Keep blanks inside an unfinished
            // macro argument so a multi-line invocation is not split.
            if (lines[j].empty() && parenDepth == 0) {
                break;
            }
            for (const auto& token : lines[j]) {
                if (token.isPunct("(")) {
                    ++parenDepth;
                } else if (token.isPunct(")") && parenDepth > 0) {
                    --parenDepth;
                }
                chunk.push_back(token);
            }
            if (!chunk.empty()) {
                Token newline;
                newline.kind = TokenKind::Newline;
                newline.text = "\n";
                newline.loc = chunk.back().loc;
                newline.atLineStart = true;
                chunk.push_back(std::move(newline));
            } else {
                Token newline;
                newline.kind = TokenKind::Newline;
                newline.text = "\n";
                newline.loc.file = file;
                newline.loc.line = static_cast<std::uint32_t>(j + 1);
                newline.loc.column = 1;
                newline.atLineStart = true;
                chunk.push_back(std::move(newline));
            }
            ++j;
        }

        adjustLine(chunk, i);
        const std::vector<Token> expanded = expandTokens(chunk);
        if (compactGaps && !expanded.empty()) {
            const Token& first = expanded.front();
            if (first.kind != TokenKind::Newline) {
                writer_->writeLineMarker(
                    first.loc.line,
                    first.loc.presumedFile.empty()
                        ? currentPresumedFile_
                        : first.loc.presumedFile);
            }
        }
        handleTextTokens(expanded);
        writer_->ensureNewline();
        i = j;
    }
}

std::vector<Token> Preprocessor::expandTokens(
    const std::vector<Token>& tokens) {
    if (!expander_) {
        return tokens;
    }
    return expander_->expand(tokens);
}

void Preprocessor::handleTextTokens(const std::vector<Token>& tokens) {
    std::vector<Token> pending;

    auto flush = [&]() {
        for (const auto& token : pending) {
            writer_->writeToken(token);
        }
        pending.clear();
    };

    for (std::size_t i = 0; i < tokens.size();) {
        if (tokens[i].isIdentifier("_Pragma")) {
            std::size_t open = i + 1;
            while (open < tokens.size() && tokens[open].isTrivia()) {
                ++open;
            }
            std::size_t string = open + 1;
            while (string < tokens.size() && tokens[string].isTrivia()) {
                ++string;
            }
            std::size_t close = string + 1;
            while (close < tokens.size() && tokens[close].isTrivia()) {
                ++close;
            }
            if (open < tokens.size() && tokens[open].isPunct("(") &&
                string < tokens.size() &&
                tokens[string].kind == TokenKind::StringLiteral &&
                close < tokens.size() && tokens[close].isPunct(")")) {
                flush();
                const std::string text =
                    special_->destringize(tokens[string].text);
                handlePragmaText(text, tokens[i].loc);
                i = close + 1;
                continue;
            }
        }
        pending.push_back(tokens[i]);
        ++i;
    }
    flush();
}

bool Preprocessor::handlePragmaText(std::string_view text,
                                    const SourceLocation& loc) {
    const std::string trimmed = trim(text);
    const std::size_t space = trimmed.find_first_of(" \t(");
    const std::string word =
        trimmed.substr(0, space == std::string::npos ? trimmed.size() : space);

    if (word == "once") {
        if (!currentPath_.empty()) {
            includes_->markOnce(currentPath_);
        }
        return false;
    }

    if (word == "push_macro" || word == "pop_macro") {
        if (auto argument = extractQuotedArgument(trimmed)) {
            if (word == "push_macro") {
                macros_->pushMacro(*argument);
            } else {
                macros_->popMacro(*argument, diagnostics_);
            }
        } else {
            diagnostics_.error(loc, "expected a string literal in #pragma " +
                                       word);
        }
        return false;
    }

    if (word == "message") {
        diagnostics_.note(loc, trimmed);
        return false;
    }

    // The preprocessed output is a single file; emitting system_header there
    // would either be ignored or incorrectly mark the whole output as a system
    // header. Consume it and keep the pragma out of the .i.
    if (trimmed == "GCC system_header") {
        return false;
    }

    if (options_.dropUnknownPragmas) {
        return false;
    }
    if (options_.strict) {
        diagnostics_.error(loc, "unknown #pragma: " + trimmed);
        return false;
    }
    writer_->writePragma(trimmed);
    return true;
}

bool Preprocessor::processDirective(const std::vector<Token>& inputLine,
                                    std::size_t zeroBasedLine,
                                    bool& wroteOutput) {
    (void)zeroBasedLine;
    wroteOutput = false;
    bool wroteLeadingComments = false;
    if (options_.keepComments) {
        for (const auto& token : inputLine) {
            if (token.kind != TokenKind::Comment) {
                break;
            }
            writer_->writeToken(token);
            wroteLeadingComments = true;
        }
        if (wroteLeadingComments) {
            writer_->ensureNewline();
        }
    }
    wroteOutput = wroteLeadingComments;

    std::vector<Token> filtered;
    filtered.reserve(inputLine.size());
    for (const auto& token : inputLine) {
        if (token.kind != TokenKind::Comment) {
            filtered.push_back(token);
        }
    }
    const std::vector<Token>& line = filtered;
    if (line.empty() || !line[0].isPunct("#")) {
        wroteOutput = wroteLeadingComments;
        return false;
    }

    const SourceLocation loc = line[0].loc;
    if (line.size() == 1) {
        return true;
    }

    const std::string directive = line[1].text;

    if (directive == "if" || directive == "ifdef" || directive == "ifndef" ||
        directive == "elif" || directive == "elifdef" ||
        directive == "elifndef" || directive == "else" ||
        directive == "endif") {
        processConditional(line, directive, loc);
        return true;
    }

    if (!conditionals_->active()) {
        return true;
    }

    if (directive == "define") {
        processDefine(line, loc);
    } else if (directive == "undef") {
        if (line.size() < 3 || line[2].kind != TokenKind::Identifier) {
            diagnostics_.error(loc, "expected a macro name after #undef");
        } else {
            macros_->undef(line[2].text);
        }
    } else if (directive == "include" || directive == "include_next") {
        const bool next = directive == "include_next";
        const bool included = processInclude(line, next, loc);
        wroteOutput = included;
    } else if (directive == "line") {
        std::vector<Token> arguments(line.begin() + 2, line.end());
        const std::vector<Token> expanded = expandTokens(arguments);
        if (expanded.empty() ||
            expanded[0].kind != TokenKind::PPNumber) {
            diagnostics_.error(loc, "#line expects a positive line number");
            return true;
        }
        int newLine = 0;
        if (!parsePositiveDecimal(expanded[0].text, newLine)) {
            diagnostics_.error(expanded[0].loc,
                               "invalid line number in #line");
            return true;
        }
        if (expanded.size() > 2) {
            diagnostics_.warning(loc, "extra tokens after #line");
        }
        std::string newFile = currentPresumedFile_;
        if (expanded.size() >= 2) {
            if (expanded[1].kind != TokenKind::StringLiteral) {
                diagnostics_.error(expanded[1].loc,
                                   "#line file name must be a string literal");
            } else {
                newFile = stripQuotes(expanded[1].text);
            }
        }
        const int originalEndLine =
            static_cast<int>(line.back().loc.line) - lineDelta_;
        lineDelta_ = newLine - (originalEndLine + 1);
        currentPresumedFile_ = newFile;
        if (options_.lineMarkers) {
            writer_->writeLineMarker(static_cast<std::uint32_t>(newLine),
                                     currentPresumedFile_);
            wroteOutput = true;
        }
    } else if (directive == "error") {
        diagnostics_.error(loc, joinTokens(line, 2));
    } else if (directive == "warning") {
        diagnostics_.warning(loc, joinTokens(line, 2));
    } else if (directive == "pragma") {
        const std::string text = joinTokens(line, 2);
        wroteOutput = handlePragmaText(text, loc);
    } else {
        const std::string text = joinTokens(line, 1);
        if (options_.strict) {
            diagnostics_.error(loc, "unknown preprocessing directive #" +
                                       directive);
        } else {
            diagnostics_.warning(loc,
                                 "unknown preprocessing directive #" +
                                     directive + "; passing it through");
            writer_->ensureNewline();
            writer_->writeRaw("#" + text + "\n");
            wroteOutput = true;
        }
    }
    wroteOutput = wroteOutput || wroteLeadingComments;
    return true;
}

bool Preprocessor::processDefine(const std::vector<Token>& line,
                                 const SourceLocation& loc, bool fromProfile) {
    if (line.size() < 3 || line[2].kind != TokenKind::Identifier) {
        diagnostics_.error(loc, "expected a macro name after #define");
        return false;
    }

    MacroDefinition definition;
    definition.name = line[2].text;
    definition.definitionLoc = line[2].loc;
    definition.isBuiltin = fromProfile;

    std::size_t i = 3;
    if (i < line.size() && line[i].isPunct("(") && !line[i].leadingSpace) {
        definition.functionLike = true;
        ++i;
        bool closed = false;
        while (i < line.size()) {
            if (line[i].isPunct(")")) {
                ++i;
                closed = true;
                break;
            }
            if (line[i].isPunct("...")) {
                definition.variadic = true;
                definition.variadicName = "__VA_ARGS__";
                ++i;
                if (i >= line.size() || !line[i].isPunct(")")) {
                    diagnostics_.error(loc,
                                       "variadic parameter must be last");
                    return false;
                }
                ++i;
                closed = true;
                break;
            }
            if (line[i].kind != TokenKind::Identifier) {
                diagnostics_.error(line[i].loc,
                                   "expected macro parameter name");
                return false;
            }
            const std::string parameter = line[i].text;
            ++i;
            if (i < line.size() && line[i].isPunct("...")) {
                definition.variadic = true;
                definition.variadicName = parameter;
                ++i;
                if (i >= line.size() || !line[i].isPunct(")")) {
                    diagnostics_.error(loc,
                                       "variadic parameter must be last");
                    return false;
                }
                ++i;
                closed = true;
                break;
            }
            definition.params.push_back(parameter);
            if (i < line.size() && line[i].isPunct(",")) {
                ++i;
                continue;
            }
            if (i < line.size() && line[i].isPunct(")")) {
                ++i;
                closed = true;
                break;
            }
            diagnostics_.error(loc, "expected ',' or ')' in macro parameter list");
            return false;
        }
        if (!closed) {
            diagnostics_.error(loc, "unterminated macro parameter list");
            return false;
        }
    }

    definition.replacement.assign(line.begin() + static_cast<std::ptrdiff_t>(i),
                                  line.end());
    return macros_->define(definition, diagnostics_);
}

bool Preprocessor::processConditional(const std::vector<Token>& line,
                                      const std::string& directive,
                                      const SourceLocation& loc) {
    auto expression = [&]() {
        return std::vector<Token>(line.begin() + 2, line.end());
    };

    if (directive == "if") {
        conditionals_->onIf(expression(), loc);
        return true;
    }
    if (directive == "ifdef" || directive == "ifndef") {
        if (line.size() < 3 || line[2].kind != TokenKind::Identifier) {
            diagnostics_.error(loc, "#" + directive +
                                        " expects a macro name");
            conditionals_->onIf({}, loc);
            return false;
        }
        conditionals_->onIfdef(line[2].text, directive == "ifndef", loc);
        return true;
    }
    if (directive == "elif") {
        conditionals_->onElif(expression(), loc);
        return true;
    }
    if (directive == "elifdef" || directive == "elifndef") {
        if (line.size() < 3 || line[2].kind != TokenKind::Identifier) {
            diagnostics_.error(loc, "#" + directive +
                                        " expects a macro name");
            conditionals_->onElif({}, loc);
            return false;
        }
        conditionals_->onElifdef(line[2].text, directive == "elifndef", loc);
        return true;
    }
    if (directive == "else") {
        if (line.size() > 2) {
            diagnostics_.warning(loc, "extra tokens after #else");
        }
        conditionals_->onElse(loc);
        return true;
    }
    if (directive == "endif") {
        if (line.size() > 2) {
            diagnostics_.warning(loc, "extra tokens after #endif");
        }
        conditionals_->onEndif(loc);
        return true;
    }
    return false;
}

bool Preprocessor::checkPrecompiledHeader(
    const std::filesystem::path& header, std::string_view includeName,
    const SourceLocation& loc) {
    std::filesystem::path pch = header;
    pch += ".gch";
    if (!isRegularFile(pch)) {
        return true;
    }

    const std::string policy =
        options_.pchPolicy == PchPolicy::Warn
            ? "warn"
            : options_.pchPolicy == PchPolicy::Error
                  ? "error"
                  : options_.pchPolicy == PchPolicy::Report ? "report"
                                                            : "ignore";
    const std::string pchText = pathToUtf8(pch);
    if (pchCache_) {
        const PchCacheResult cached = pchCache_->observe(header, pch);
        if (cached.status == PchCacheStatus::Error) {
            diagnostics_.note(loc, "PCH cache: " + cached.detail);
        }
    }
    if (options_.pchPolicy != PchPolicy::Ignore) {
        PchRecord record;
        record.includeName = std::string(includeName);
        record.path = pchText;
        record.policy = policy;
        pchRecords_.push_back(std::move(record));
    }

    if (options_.pchPolicy == PchPolicy::Error) {
        diagnostics_.error(
            loc, "precompiled header '" + pchText +
                     "' is present; refusing to use it (--pch=error); "
                     "g++ -E reads the header source");
        return false;
    }

    if (options_.pchPolicy == PchPolicy::Warn) {
        std::string key = pchText;
#ifdef _WIN32
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
#endif
        if (pchWarned_.insert(key).second) {
            diagnostics_.warning(
                loc, "ignoring precompiled header '" + pchText +
                         "'; g++ -E reads the header source");
        }
    }
    return true;
}

bool Preprocessor::processInclude(const std::vector<Token>& line,
                                  bool nextInclude,
                                  const SourceLocation& loc) {
    if (line.size() < 3) {
        diagnostics_.error(loc, "expected a header name after #include");
        return false;
    }

    std::string name;
    bool angled = false;
    bool rawParsed = false;
    std::size_t index = 2;

    // Prefer the raw spelling after the directive keyword. This preserves
    // Windows backslashes and "//" inside <...>, which ordinary tokenization
    // would otherwise damage. Comments/extra tokens fall back to token mode.
    if (line.size() >= 2 && line[1].loc.file && currentFile_) {
        const std::size_t start =
            line[1].loc.offset + line[1].text.size();
        const std::size_t end =
            currentFile_->text.find('\n', start);
        const std::string_view rest(
            currentFile_->text.data() + start,
            (end == std::string::npos ? currentFile_->text.size() : end) -
                start);
        std::size_t pos = 0;
        if (Tokenizer::parseHeaderName(rest, pos, name, angled)) {
            while (pos < rest.size() &&
                   (rest[pos] == ' ' || rest[pos] == '\t' ||
                    rest[pos] == '\r')) {
                ++pos;
            }
            if (pos == rest.size() || rest[pos] == '\n') {
                rawParsed = true;
                index = line.size();
            }
        }
    }

    if (rawParsed) {
        // name/angled already set
    } else if (line[index].kind == TokenKind::HeaderName) {
        const std::string& header = line[index].text;
        if (header.size() >= 2 && header.front() == '<' &&
            header.back() == '>') {
            name = header.substr(1, header.size() - 2);
            angled = true;
            ++index;
        } else {
            diagnostics_.error(loc, "invalid header name in #include");
            return false;
        }
    } else if (line[index].kind == TokenKind::StringLiteral) {
        name = stripQuotes(line[index].text);
        ++index;
    } else if (line[index].isPunct("<")) {
        if (!parseHeaderFromTokens(line, index, name, angled)) {
            diagnostics_.error(loc, "unterminated header name in #include");
            return false;
        }
    } else {
        std::vector<Token> operand(line.begin() + 2, line.end());
        const std::vector<Token> expanded = expandTokens(operand);
        std::size_t expandedIndex = 0;
        if (!parseHeaderFromTokens(expanded, expandedIndex, name, angled)) {
            diagnostics_.error(loc,
                               "#include expects a quoted header name or a "
                               "macro expanding to one");
            return false;
        }
        if (expandedIndex != expanded.size()) {
            diagnostics_.warning(loc, "extra tokens after #include");
        }
        index = line.size();
    }

    if (index < line.size()) {
        diagnostics_.warning(loc, "extra tokens after #include");
    }

    auto resolved = includes_->resolve(name, angled, currentPath_,
                                       nextInclude);
    if (!resolved.has_value()) {
        if (auto pch = includes_->findPrecompiledHeader(
                name, angled, currentPath_, nextInclude)) {
            diagnostics_.error(
                loc, "cannot find include file '" + name +
                         "'; found only precompiled header '" +
                         pathToUtf8(*pch) +
                         "', and g++ -E cannot use it");
        } else {
            diagnostics_.error(loc,
                               "cannot find include file '" + name + "'");
        }
        return false;
    }

    if (!checkPrecompiledHeader(*resolved, name, loc)) {
        return false;
    }

    if (!options_.analysisFile.empty() && includeEdges_.size() < 10000) {
        IncludeEdge edge;
        edge.from = currentFile_ ? currentFile_->displayPath : baseFile_;
        edge.to = pathToUtf8(*resolved);
        edge.line = static_cast<int>(loc.line);
        edge.angled = angled;
        includeEdges_.push_back(std::move(edge));
    }

    const int returnLine =
        static_cast<int>(line.back().loc.line) + 1;
    const bool ok = processFile(*resolved, angled);
    if (!options_.lineMarkers) {
        writer_->ensureNewline();
    } else if (ok) {
        writer_->writeLineMarker(static_cast<std::uint32_t>(returnLine),
                                 currentPresumedFile_);
    }
    return ok;
}

bool Preprocessor::evaluateCondition(const std::vector<Token>& expression,
                                     const SourceLocation& loc) {
    if (expression.empty()) {
        diagnostics_.error(loc, "empty #if expression");
        return false;
    }

    auto isDefined = [this](std::string_view name) {
        return macros_->contains(name) ||
               SpecialOperator::isBuiltinName(name) ||
               isGccDefinedOperator(name);
    };

    std::vector<Token> definedProcessed;
    replaceDefined(expression, isDefined, definedProcessed, diagnostics_);

    std::vector<Token> specialProcessed;
    for (std::size_t i = 0; i < definedProcessed.size();) {
        const Token& token = definedProcessed[i];
        if (token.isIdentifier("__has_include") ||
            token.isIdentifier("__has_include_next")) {
            const bool next =
                token.text == "__has_include_next";
            std::size_t j = i + 1;
            if (j >= definedProcessed.size() ||
                !definedProcessed[j].isPunct("(")) {
                diagnostics_.error(token.loc,
                                   "__has_include expects parentheses");
                specialProcessed.push_back(makeNumber("0", token.loc));
                ++i;
                continue;
            }
            ++j;
            std::string header;
            bool headerAngled = false;
            bool parsed = false;

            if (j < definedProcessed.size() &&
                (definedProcessed[j].kind == TokenKind::StringLiteral ||
                 definedProcessed[j].isPunct("<"))) {
                parsed = parseHeaderFromTokens(definedProcessed, j, header,
                                               headerAngled);
            } else {
                const std::size_t argumentStart = j;
                int depth = 1;
                std::size_t k = j;
                for (; k < definedProcessed.size(); ++k) {
                    if (definedProcessed[k].isPunct("(")) {
                        ++depth;
                    } else if (definedProcessed[k].isPunct(")")) {
                        --depth;
                        if (depth == 0) {
                            break;
                        }
                    }
                }
                if (k < definedProcessed.size()) {
                    std::vector<Token> argument(
                        definedProcessed.begin() +
                            static_cast<std::ptrdiff_t>(argumentStart),
                        definedProcessed.begin() +
                            static_cast<std::ptrdiff_t>(k));
                    const std::vector<Token> expanded =
                        expandTokens(argument);
                    std::size_t expandedIndex = 0;
                    parsed = parseHeaderFromTokens(expanded, expandedIndex,
                                                   header, headerAngled);
                    j = k;
                }
            }

            if (!parsed) {
                diagnostics_.error(token.loc,
                                   "invalid argument to __has_include");
                specialProcessed.push_back(makeNumber("0", token.loc));
                i = (j > i ? j : i + 1);
                continue;
            }

            if (j >= definedProcessed.size() ||
                !definedProcessed[j].isPunct(")")) {
                diagnostics_.error(token.loc,
                                   "expected ')' after __has_include");
            } else {
                ++j;
            }

            const auto resolved = includes_->resolve(
                header, headerAngled, currentPath_, next);
            if (resolved.has_value()) {
                // GCC records a header found by __has_include in the depfile
                // even when it is only probed and never included.
                recordDependency(*resolved);
            }
            specialProcessed.push_back(
                makeNumber(resolved.has_value() ? "1" : "0", token.loc));
            i = j;
            continue;
        }

        if (token.isIdentifier("__has_cpp_attribute")) {
            std::string attributeName;
            std::size_t close = 0;
            if (parseCppAttributeName(definedProcessed, i + 1,
                                      attributeName, close)) {
                const int value = gccCppAttributeValue(
                    attributeName, options_.standard);
                specialProcessed.push_back(
                    makeNumber(std::to_string(value), token.loc));
                i = close + 1;
                continue;
            }
            diagnostics_.error(token.loc,
                               "invalid argument to __has_cpp_attribute");
            specialProcessed.push_back(makeNumber("0", token.loc));
            ++i;
            continue;
        }

        if (token.isIdentifier("__has_builtin") ||
            token.isIdentifier("__has_attribute") ||
            token.isIdentifier("__has_feature") ||
            token.isIdentifier("__has_extension") ||
            token.isIdentifier("__has_warning") ||
            token.isIdentifier("__has_declspec_attribute")) {
            std::size_t open = i + 1;
            if (open < definedProcessed.size() &&
                definedProcessed[open].isPunct("(")) {
                int depth = 0;
                std::size_t close = open;
                for (; close < definedProcessed.size(); ++close) {
                    if (definedProcessed[close].isPunct("(")) {
                        ++depth;
                    } else if (definedProcessed[close].isPunct(")")) {
                        --depth;
                        if (depth == 0) {
                            break;
                        }
                    }
                }
                if (close < definedProcessed.size()) {
                    std::string argument;
                    for (std::size_t k = open + 1; k < close; ++k) {
                        if (definedProcessed[k].kind ==
                            TokenKind::Identifier) {
                            argument = definedProcessed[k].text;
                            break;
                        }
                    }
                    int value = 0;
                    if (token.text == "__has_builtin") {
                        value = gccBuiltinAvailable(argument) ? 1 : 0;
                    } else if (token.text == "__has_attribute") {
                        value = gccAttributeAvailable(argument) ? 1 : 0;
                    } else if (token.text == "__has_feature" ||
                               token.text == "__has_extension") {
                        value = gccFeatureAvailable(
                                    argument,
                                    token.text == "__has_extension",
                                    options_.standard)
                                    ? 1
                                    : 0;
                    }
                    specialProcessed.push_back(
                        makeNumber(std::to_string(value), token.loc));
                    i = close + 1;
                    continue;
                }
            }
            specialProcessed.push_back(makeNumber("0", token.loc));
            ++i;
            continue;
        }

        specialProcessed.push_back(token);
        ++i;
    }

    std::vector<Token> expanded = expandTokens(specialProcessed);

    // Macro expansion can introduce __has_* operators. GCC/libstdc++ relies on
    // this, for example _GLIBCXX_HAS_BUILTIN(...) expands to
    // __has_builtin(...). Run a second operator pass after macro expansion.
    std::vector<Token> postExpansion;
    postExpansion.reserve(expanded.size());
    for (std::size_t i = 0; i < expanded.size();) {
        const Token& token = expanded[i];
        if (token.isIdentifier("__has_include") ||
            token.isIdentifier("__has_include_next")) {
            const bool next = token.text == "__has_include_next";
            std::size_t open = i + 1;
            if (open < expanded.size() && expanded[open].isPunct("(")) {
                std::size_t j = open + 1;
                std::string header;
                bool angled = false;
                if (parseHeaderFromTokens(expanded, j, header, angled) &&
                    j < expanded.size() && expanded[j].isPunct(")")) {
                    const auto resolved = includes_->resolve(
                        header, angled, currentPath_, next);
                    if (resolved.has_value()) {
                        // See the first __has_include pass: probes are
                        // dependencies in GCC's depfile too.
                        recordDependency(*resolved);
                    }
                    postExpansion.push_back(
                        makeNumber(resolved.has_value() ? "1" : "0",
                                   token.loc));
                    i = j + 1;
                    continue;
                }
            }
            postExpansion.push_back(makeNumber("0", token.loc));
            ++i;
            continue;
        }
        if (token.isIdentifier("__has_cpp_attribute")) {
            std::string attributeName;
            std::size_t close = 0;
            if (parseCppAttributeName(expanded, i + 1, attributeName, close)) {
                const int value = gccCppAttributeValue(
                    attributeName, options_.standard);
                postExpansion.push_back(
                    makeNumber(std::to_string(value), token.loc));
                i = close + 1;
                continue;
            }
            postExpansion.push_back(makeNumber("0", token.loc));
            ++i;
            continue;
        }
        if (token.isIdentifier("__has_builtin") ||
            token.isIdentifier("__has_attribute") ||
            token.isIdentifier("__has_feature") ||
            token.isIdentifier("__has_extension") ||
            token.isIdentifier("__has_warning") ||
            token.isIdentifier("__has_declspec_attribute")) {
            std::size_t open = i + 1;
            if (open < expanded.size() && expanded[open].isPunct("(")) {
                int depth = 0;
                std::size_t close = open;
                for (; close < expanded.size(); ++close) {
                    if (expanded[close].isPunct("(")) {
                        ++depth;
                    } else if (expanded[close].isPunct(")")) {
                        --depth;
                        if (depth == 0) {
                            break;
                        }
                    }
                }
                if (close < expanded.size()) {
                    std::string argument;
                    for (std::size_t k = open + 1; k < close; ++k) {
                        if (expanded[k].kind == TokenKind::Identifier) {
                            argument = expanded[k].text;
                            break;
                        }
                    }
                    int value = 0;
                    if (token.text == "__has_builtin") {
                        value = gccBuiltinAvailable(argument) ? 1 : 0;
                    } else if (token.text == "__has_attribute") {
                        value = gccAttributeAvailable(argument) ? 1 : 0;
                    } else if (token.text == "__has_feature" ||
                               token.text == "__has_extension") {
                        value = gccFeatureAvailable(
                                    argument,
                                    token.text == "__has_extension",
                                    options_.standard)
                                    ? 1
                                    : 0;
                    }
                    postExpansion.push_back(
                        makeNumber(std::to_string(value), token.loc));
                    i = close + 1;
                    continue;
                }
            }
            postExpansion.push_back(makeNumber("0", token.loc));
            ++i;
            continue;
        }
        postExpansion.push_back(token);
        ++i;
    }
    expanded = std::move(postExpansion);

    // If a macro produced `defined`, handle it once more for compatibility.
    bool containsDefined = false;
    for (const auto& token : expanded) {
        if (token.isIdentifier("defined")) {
            containsDefined = true;
            break;
        }
    }
    if (containsDefined) {
        // GCC/Windows SDK headers (notably MinGW's __INTRINSIC_PROLOG)
        // produce `defined` through macro expansion. Evaluating it is the
        // useful, compatible behavior; do not emit a warning per occurrence.
        std::vector<Token> secondPass;
        replaceDefined(expanded, isDefined, secondPass, diagnostics_);
        expanded = std::move(secondPass);
    }

    std::vector<Token> expressionTokens;
    expressionTokens.reserve(expanded.size());
    for (auto& token : expanded) {
        if (token.kind == TokenKind::Identifier) {
            if (token.text == "true" || token.text == "false" ||
                isAlternativeOperator(token.text)) {
                expressionTokens.push_back(token);
            } else {
                expressionTokens.push_back(makeNumber("0", token.loc));
            }
        } else {
            expressionTokens.push_back(token);
        }
    }

    CharacterOptions characterOptions;
    characterOptions.executionCharset = options_.executionCharset;
    characterOptions.unsignedChar = options_.unsignedChar;
    return ConditionalEngine::evaluateExpression(expressionTokens, loc,
                                                  diagnostics_,
                                                  characterOptions);
}

bool Preprocessor::writeAnalysis() {
    if (options_.analysisFile.empty()) {
        return true;
    }

    std::error_code ec;
    const auto parent = options_.analysisFile.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream out(options_.analysisFile, std::ios::binary);
    if (!out) {
        diagnostics_.error({}, "cannot write analysis file '" +
                                   pathToUtf8(options_.analysisFile) + "'");
        return false;
    }

    std::vector<std::string> files;
    std::unordered_set<std::string> seen;
    auto addFile = [&](const std::string& file) {
        if (!file.empty() && seen.insert(file).second) {
            files.push_back(file);
        }
    };
    addFile(baseFile_);
    for (const auto& edge : includeEdges_) {
        addFile(edge.from);
        addFile(edge.to);
    }

    out << "{\"version\":1,\"root\":\"" << jsonEscape(baseFile_)
        << "\",\"files\":[";
    for (std::size_t i = 0; i < files.size(); ++i) {
        if (i) {
            out << ',';
        }
        out << '"' << jsonEscape(files[i]) << '"';
    }
    out << "],\"pch\":[";
    for (std::size_t i = 0; i < pchRecords_.size(); ++i) {
        if (i) {
            out << ',';
        }
        const auto& pch = pchRecords_[i];
        out << "{\"include\":\"" << jsonEscape(pch.includeName)
            << "\",\"path\":\"" << jsonEscape(pch.path)
            << "\",\"policy\":\"" << jsonEscape(pch.policy) << "\"}";
    }
    out << "],\"includes\":[";
    for (std::size_t i = 0; i < includeEdges_.size(); ++i) {
        if (i) {
            out << ',';
        }
        const auto& edge = includeEdges_[i];
        out << "{\"from\":\"" << jsonEscape(edge.from) << "\",\"to\":\""
            << jsonEscape(edge.to) << "\",\"line\":" << edge.line
            << ",\"angled\":" << (edge.angled ? "true" : "false") << '}';
    }
    out << "],\"macros\":[";
    for (std::size_t i = 0; i < macroTrace_.size(); ++i) {
        if (i) {
            out << ',';
        }
        const auto& trace = macroTrace_[i];
        out << "{\"id\":" << trace.id << ",\"parentId\":"
            << trace.parentId << ",\"macro\":\""
            << jsonEscape(trace.macro) << "\",\"kind\":\""
            << jsonEscape(trace.kind) << "\",\"file\":\""
            << jsonEscape(trace.file) << "\",\"line\":" << trace.line
            << ",\"column\":" << trace.column << ",\"offset\":"
            << trace.offset << ",\"length\":" << trace.length
            << ",\"depth\":" << trace.depth << ",\"invocation\":\""
            << jsonEscape(trace.invocation) << "\",\"sourceLine\":\""
            << jsonEscape(trace.sourceLine) << "\",\"definitionFile\":\""
            << jsonEscape(trace.definitionFile) << "\",\"definitionLine\":"
            << trace.definitionLine << ",\"definitionColumn\":"
            << trace.definitionColumn << ",\"definitionText\":\""
            << jsonEscape(trace.definitionText) << "\",\"replacement\":\""
            << jsonEscape(trace.replacement) << "\",\"arguments\":[";
        for (std::size_t a = 0; a < trace.arguments.size(); ++a) {
            if (a) {
                out << ',';
            }
            out << "{\"raw\":\"" << jsonEscape(trace.arguments[a].raw)
                << "\",\"expanded\":\""
                << jsonEscape(trace.arguments[a].expanded) << "\"}";
        }
        out << "],\"steps\":[";
        for (std::size_t s = 0; s < trace.steps.size(); ++s) {
            if (s) {
                out << ',';
            }
            out << "{\"operation\":\""
                << jsonEscape(trace.steps[s].operation)
                << "\",\"before\":\""
                << jsonEscape(trace.steps[s].before) << "\",\"after\":\""
                << jsonEscape(trace.steps[s].after) << "\"}";
        }
        out << "]}";
    }
    out << "]}";
    if (!out.good()) {
        diagnostics_.error({}, "failed while writing analysis file '" +
                                   pathToUtf8(options_.analysisFile) + "'");
        return false;
    }
    return true;
}

void Preprocessor::recordDependency(const std::filesystem::path& path) {
    if (!options_.generateDependencies) {
        return;
    }
    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        absolute = path;
    }
    absolute = absolute.lexically_normal();
    const std::string key = includes_->canonicalKey(absolute);
    if (dependencyKeys_.insert(key).second) {
        dependencies_.push_back(std::move(absolute));
    }
}

namespace {

std::string escapeDependencyPath(std::string text, bool dollarEscape) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char c : text) {
        if (c == '\\') {
            out.push_back('/');
        } else if (c == ' ' || c == '#') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '$' && dollarEscape) {
            out.append("$$");
        } else {
            out.push_back(c);
        }
    }
    return out;
}

} // namespace

bool Preprocessor::writeDependencies() {
    if (!options_.generateDependencies) {
        return true;
    }

    std::filesystem::path depfile = options_.dependencyFile;
    if (depfile.empty()) {
        if (!options_.output.empty()) {
            depfile = pathFromUtf8(pathToUtf8(options_.output) + ".d");
        } else if (!options_.input.empty()) {
            depfile = pathFromUtf8(pathToUtf8(options_.input) + ".d");
        } else {
            diagnostics_.error({}, "cannot derive dependency file name");
            return false;
        }
    }

    std::string target = options_.dependencyTarget;
    if (target.empty()) {
        if (!options_.output.empty()) {
            target = pathToUtf8(options_.output);
        } else if (!options_.input.empty()) {
            target = pathToUtf8(options_.input);
        } else {
            target = "stdout";
        }
    }
    if (options_.quoteDependencyTarget) {
        target = escapeDependencyPath(std::move(target), true);
    } else {
        // CMake/Ninja use forward slashes; a raw -MT target is otherwise kept
        // verbatim.
        std::replace(target.begin(), target.end(), '\\', '/');
    }

    std::error_code ec;
    if (depfile.has_parent_path() && !depfile.parent_path().empty()) {
        std::filesystem::create_directories(depfile.parent_path(), ec);
        if (ec) {
            diagnostics_.error({}, "cannot create dependency directory '" +
                                       pathToUtf8(depfile.parent_path()) + "'");
            return false;
        }
    }

    std::ofstream out(depfile, std::ios::binary | std::ios::trunc);
    if (!out) {
        diagnostics_.error({}, "cannot open dependency file '" +
                                   pathToUtf8(depfile) + "'");
        return false;
    }

    out << target << ':';
    for (const auto& dependency : dependencies_) {
        out << ' '
            << escapeDependencyPath(pathToUtf8(dependency), false);
    }
    out << '\n';
    if (!out) {
        diagnostics_.error({}, "failed to write dependency file '" +
                                   pathToUtf8(depfile) + "'");
        return false;
    }
    return true;
}

bool Preprocessor::writeOutput() {
    if (!options_.outputOnError && diagnostics_.hasError()) {
        return true;
    }

    writer_->ensureNewline();

    if (options_.output.empty()) {
        std::cout << writer_->buffer();
        if (!std::cout) {
            diagnostics_.error({}, "failed to write preprocessed output");
            return false;
        }
        return true;
    }

    const std::filesystem::path output = options_.output;
    std::error_code ec;
    if (std::filesystem::exists(output, ec) &&
        std::filesystem::exists(options_.input, ec)) {
        ec.clear();
        if (std::filesystem::equivalent(output, options_.input, ec) && !ec) {
            diagnostics_.error(
                {}, "input and output refer to the same file; refusing to "
                    "overwrite the source");
            return false;
        }
    }
    if (output.has_parent_path() && !output.parent_path().empty()) {
        std::filesystem::create_directories(output.parent_path(), ec);
        if (ec) {
            diagnostics_.error({}, "cannot create output directory '" +
                                       pathToUtf8(output.parent_path()) + "'");
            return false;
        }
    }

    const std::filesystem::path temporary =
        pathFromUtf8(pathToUtf8(output) + ".tmp");
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            diagnostics_.error({}, "cannot open output file '" +
                                       pathToUtf8(temporary) + "'");
            return false;
        }
        out << writer_->buffer();
        if (!out) {
            diagnostics_.error({}, "failed to write output file '" +
                                       pathToUtf8(temporary) + "'");
            return false;
        }
    }

    // On Windows an antivirus/indexer or a just-finished compiler can briefly
    // hold the destination. Retry the remove+rename window a few times.
    bool replaced = false;
    std::error_code renameError;
    for (int attempt = 0; attempt < 50; ++attempt) {
        std::error_code removeError;
        std::filesystem::remove(output, removeError);
        renameError.clear();
        std::filesystem::rename(temporary, output, renameError);
        if (!renameError) {
            replaced = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!replaced) {
        std::error_code cleanupError;
        std::filesystem::remove(temporary, cleanupError);
        diagnostics_.error({}, "cannot replace output file '" +
                                   pathToUtf8(output) + "': " +
                                   renameError.message());
        return false;
    }
    return true;
}

} // namespace crucible
