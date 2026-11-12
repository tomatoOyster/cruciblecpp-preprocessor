#pragma once

#include "MacroExpander.h"
#include "PchCache.h"
#include "Token.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crucible {

class ConditionalEngine;
class IncludeHandler;
class MacroExpander;
class MacroRegistry;
class OutputWriter;
class SpecialOperator;

enum class Severity { Note, Warning, Error, Fatal };

struct IncludeFrame {
    std::string path;
    std::uint32_t line = 0;
};

struct Diagnostic {
    Severity severity = Severity::Error;
    SourceLocation loc;
    std::string message;
};

class DiagnosticSink {
public:
    explicit DiagnosticSink(int maxErrors = 20);

    void setWarningsAsErrors(bool enabled) { warningsAsErrors_ = enabled; }
    void setMaxErrors(int max) { maxErrors_ = max; }
    void setShowCaret(bool enabled) { showCaret_ = enabled; }
    void setShowOption(bool enabled) { showOption_ = enabled; }
    void setColor(bool enabled) { color_ = enabled; }
    void setSystemHeader(bool enabled) { systemHeader_ = enabled; }
    void setShowSystemWarnings(bool enabled) { showSystemWarnings_ = enabled; }
    void setIncludeStack(std::vector<IncludeFrame> stack) {
        includeStack_ = std::move(stack);
    }

    void pushDiagnosticState();
    void popDiagnosticState();
    void setDiagnosticState(std::string_view option, Severity severity);
    bool warningEnabled(std::string_view option) const;

    void report(Severity severity, const SourceLocation& loc,
                std::string message, std::string_view option = {});
    void error(const SourceLocation& loc, std::string message) {
        report(Severity::Error, loc, std::move(message));
    }
    void warning(const SourceLocation& loc, std::string message,
                 std::string_view option = {}) {
        report(Severity::Warning, loc, std::move(message), option);
    }
    void note(const SourceLocation& loc, std::string message) {
        report(Severity::Note, loc, std::move(message));
    }

    bool hasError() const { return errorCount_ > 0; }
    bool fatal() const { return fatal_; }
    bool limitReached() const { return limitReached_; }
    int errorCount() const { return errorCount_; }

private:
    int maxErrors_ = 20;
    int errorCount_ = 0;
    bool warningsAsErrors_ = false;
    bool showCaret_ = true;
    bool showOption_ = true;
    bool color_ = false;
    bool systemHeader_ = false;
    bool showSystemWarnings_ = false;
    bool fatal_ = false;
    bool limitReached_ = false;
    std::vector<IncludeFrame> includeStack_;
    std::unordered_map<std::string, Severity> diagnosticState_;
    std::vector<std::unordered_map<std::string, Severity>> diagnosticStack_;
};

struct CommandLineAction {
    enum class Kind { Define, Undef };
    Kind kind = Kind::Define;
    std::string text;
};

enum class PchPolicy { Ignore, Warn, Error, Report };

struct PreprocessorOptions {
    std::filesystem::path input;
    std::filesystem::path output;

    std::vector<std::filesystem::path> includeDirs;
    std::vector<std::filesystem::path> systemDirs;
    std::vector<CommandLineAction> commandActions;
    std::vector<std::filesystem::path> forcedIncludes;

    bool generateDependencies = false;
    std::filesystem::path dependencyFile;
    std::string dependencyTarget;
    bool quoteDependencyTarget = false;

    bool lineMarkers = true;
    bool keepComments = false;
    bool trigraphs = false;
    bool showDiagnosticCaret = true;
    bool showDiagnosticOption = true;
    bool colorDiagnostics = false;
    bool showSystemHeaderWarnings = false;
    std::unordered_map<std::string, Severity> diagnosticStates;
    bool warningsAsErrors = false;
    bool strict = false;
    bool dropUnknownPragmas = false;
    bool outputOnError = true;
    bool verbose = false;
    // Query g++/gcc from PATH for its implicit include directories.
    bool autoGccIncludes = true;

    int maxIncludeDepth = 200;
    int maxErrors = 20;
    std::string standard = "c++20";
    std::string inputCharset = "UTF-8";
    std::string executionCharset = "UTF-8";
    bool unsignedChar = false;

    // GCC .gch files are never parsed. This controls diagnostics only.
    PchPolicy pchPolicy = PchPolicy::Ignore;

    // Optional PCH artifact cache. Empty directory disables it.
    std::filesystem::path pchCacheDir;
    std::filesystem::path pchCacheStatsFile;
    std::size_t pchCacheMaxEntries = 256;
    std::uintmax_t pchCacheMaxBytes = 512ULL * 1024 * 1024;
    int pchCacheMaxAgeDays = 30;
    int pchCacheLockTimeoutMs = 10000;
    bool pchCacheCleanup = false;

    // Optional JSON analysis output for GUI/front-end tooling.
    std::filesystem::path analysisFile;
};

class Preprocessor {
public:
    explicit Preprocessor(PreprocessorOptions options);
    ~Preprocessor();

    Preprocessor(const Preprocessor&) = delete;
    Preprocessor& operator=(const Preprocessor&) = delete;

    int run();

private:
    bool applyCommandLine();
    bool loadGccProfile();
    bool defineFromCommandLine(const std::string& spec);

    bool processFile(const std::filesystem::path& path, bool angledInclude,
                     std::uint32_t includedFromLine = 0,
                     bool systemHeader = false);
    void processTokenStream(std::shared_ptr<SourceFile> file);
    void checkPoisoned(const std::vector<Token>& tokens);
    void warnUnusedMacros();
    bool processDirective(const std::vector<Token>& line,
                          std::size_t zeroBasedLine, bool& wroteOutput);
    bool processInclude(const std::vector<Token>& line, bool nextInclude,
                        const SourceLocation& loc);
    bool processDefine(const std::vector<Token>& line,
                       const SourceLocation& loc, bool fromProfile = false);
    bool processConditional(const std::vector<Token>& line,
                            const std::string& directive,
                            const SourceLocation& loc);
    bool evaluateCondition(const std::vector<Token>& expression,
                           const SourceLocation& loc);

    void handlePragmaTokens(const std::vector<Token>& tokens);
    bool handlePragmaText(std::string_view text, const SourceLocation& loc);
    void handleTextTokens(const std::vector<Token>& tokens);
    std::vector<Token> expandTokens(const std::vector<Token>& tokens);

    bool writeOutput();
    bool writeDependencies();
    bool writeAnalysis();
    bool checkPrecompiledHeader(const std::filesystem::path& header,
                                std::string_view includeName,
                                const SourceLocation& loc);
    void recordDependency(const std::filesystem::path& path);
    std::shared_ptr<SourceFile> makeSyntheticFile(
        std::string_view text, std::string_view displayName);

    PreprocessorOptions options_;
    DiagnosticSink diagnostics_;

    std::unique_ptr<MacroRegistry> macros_;
    std::unique_ptr<MacroExpander> expander_;
    std::unique_ptr<ConditionalEngine> conditionals_;
    std::unique_ptr<IncludeHandler> includes_;
    std::unique_ptr<SpecialOperator> special_;
    std::unique_ptr<OutputWriter> writer_;
    std::unique_ptr<PchCache> pchCache_;
    std::string compilerPath_;
    std::string compilerVersion_;

    std::shared_ptr<SourceFile> currentFile_;
    std::filesystem::path currentPath_;
    std::string currentPresumedFile_;
    std::string baseFile_;
    std::vector<IncludeFrame> includeStack_;
    std::unordered_map<std::string, SourceLocation> poisoned_;
    std::unordered_set<std::string> reportedPoisonUses_;
    bool currentSystemHeader_ = false;
    int lineDelta_ = 0;
    int includeLevel_ = 0;
    int counter_ = 0;
    int filesProcessed_ = 0;
    std::vector<std::filesystem::path> dependencies_;
    std::unordered_set<std::string> dependencyKeys_;

    struct IncludeEdge {
        std::string from;
        std::string to;
        int line = 0;
        bool angled = false;
    };
    struct MacroTrace {
        int id = 0;
        int parentId = -1;
        std::string macro;
        std::string kind;
        std::string file;
        int line = 0;
        int column = 0;
        int offset = 0;
        int length = 0;
        int depth = 0;
        std::string invocation;
        std::string sourceLine;
        std::string definitionFile;
        int definitionLine = 0;
        int definitionColumn = 0;
        std::string definitionText;
        std::string replacement;
        std::vector<MacroTraceArgument> arguments;
        std::vector<MacroTraceStep> steps;
    };
    struct PchRecord {
        std::string includeName;
        std::string path;
        std::string policy;
    };
    std::vector<IncludeEdge> includeEdges_;
    std::vector<MacroTrace> macroTrace_;
    std::unordered_map<int, std::size_t> macroTraceIndex_;
    std::vector<PchRecord> pchRecords_;
    std::unordered_set<std::string> pchWarned_;
};

} // namespace crucible
