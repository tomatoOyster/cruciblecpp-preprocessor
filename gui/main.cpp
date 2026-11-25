#include "GccDiscovery.h"
#include "Preprocessor.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {


constexpr wchar_t kWindowClass[] = L"CrucibleCppGuiWindow";
constexpr UINT kRunDoneMessage = WM_APP + 1;
constexpr int kMaxDisplayBytes = 8 * 1024 * 1024;

enum ControlId : int {
    IDC_INPUT = 100,
    IDC_BROWSE_INPUT,
    IDC_OUTPUT,
    IDC_BROWSE_OUTPUT,
    IDC_GCC,
    IDC_BROWSE_GCC,
    IDC_DETECT_GCC,
    IDC_STANDARD,
    IDC_RUN,
    IDC_OUTPUT_VIEW,
    IDC_DIAG_VIEW,
    IDC_STATUS,
    IDC_STATIC_INPUT,
    IDC_STATIC_OUTPUT,
    IDC_STATIC_GCC,
    IDC_STATIC_STANDARD,
    IDC_CANCEL,
    IDC_OPTIONS,
    IDC_COPY_CLI,
};

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
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
    if (size <= 0) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(codePage, flags, text.data(), static_cast<int>(text.size()),
                        result.data(), size);
    return result;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0,
                                         nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

std::string readFile(const std::filesystem::path& path, bool& truncated) {
    truncated = false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string data;
    data.resize(kMaxDisplayBytes);
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    const auto read = static_cast<std::size_t>(in.gcount());
    data.resize(read);
    if (in.peek() != EOF) truncated = true;
    return data;
}

int parseGccMajor(const std::string& version) {
    int value = 0;
    for (char c : version) {
        if (c >= '0' && c <= '9') {
            value = value * 10 + (c - '0');
        } else if (value > 0) {
            break;
        }
    }
    if (value < 4 || value > 99) return 15;
    return value;
}

std::string severityText(crucible::Severity severity) {
    switch (severity) {
    case crucible::Severity::Note: return "note";
    case crucible::Severity::Warning: return "warning";
    case crucible::Severity::Error: return "error";
    case crucible::Severity::Fatal: return "fatal error";
    }
    return "error";
}

std::string diagnosticPath(const crucible::Diagnostic& diagnostic) {
    if (!diagnostic.loc.presumedFile.empty()) return diagnostic.loc.presumedFile;
    if (diagnostic.loc.file) return diagnostic.loc.file->displayPath;
    return "<command line>";
}

std::vector<std::wstring> splitOptionLines(std::wstring text);

struct AppState {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HWND input = nullptr;
    HWND browseInput = nullptr;
    HWND output = nullptr;
    HWND browseOutput = nullptr;
    HWND gcc = nullptr;
    HWND browseGcc = nullptr;
    HWND detectGcc = nullptr;
    HWND standard = nullptr;
    HWND run = nullptr;
    HWND cancel = nullptr;
    HWND options = nullptr;
    HWND copyCli = nullptr;
    HWND outputView = nullptr;
    HWND diagView = nullptr;
    HWND status = nullptr;
    HWND labels[4]{};
    HFONT uiFont = nullptr;
    HFONT codeFont = nullptr;
    std::thread worker;
    std::atomic_bool cancelRequested{false};
    std::wstring selectedInput;
    std::wstring selectedOutput;
    std::wstring selectedGcc;
    std::vector<std::filesystem::path> includeDirs;
    std::vector<crucible::CommandLineAction> commandActions;
    crucible::PchPolicy pchPolicy = crucible::PchPolicy::Ignore;
    bool lineMarkers = true;
    bool keepComments = false;
    bool running = false;
    int exitCode = 0;
    std::string resultOutput;
    std::string resultDiagnostics;
    std::string resultStatus;

    void createControls() {
        const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
        const DWORD buttonStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
        const wchar_t* labelText[] = {L"源文件", L"输出 .i", L"目标 GCC", L"标准"};
        for (int i = 0; i < 4; ++i) {
            labels[i] = CreateWindowExW(0, L"STATIC", labelText[i],
                                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                                        0, 0, 10, 10, window, nullptr, instance, nullptr);
            SendMessageW(labels[i], WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        }
        input = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
                                0, 0, 10, 10, window,
                                reinterpret_cast<HMENU>(IDC_INPUT), instance, nullptr);
        browseInput = CreateWindowExW(0, L"BUTTON", L"浏览…", buttonStyle,
                                      0, 0, 10, 10, window,
                                      reinterpret_cast<HMENU>(IDC_BROWSE_INPUT), instance, nullptr);
        output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
                                 0, 0, 10, 10, window,
                                 reinterpret_cast<HMENU>(IDC_OUTPUT), instance, nullptr);
        browseOutput = CreateWindowExW(0, L"BUTTON", L"浏览…", buttonStyle,
                                       0, 0, 10, 10, window,
                                       reinterpret_cast<HMENU>(IDC_BROWSE_OUTPUT), instance, nullptr);
        gcc = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
                              0, 0, 10, 10, window,
                              reinterpret_cast<HMENU>(IDC_GCC), instance, nullptr);
        browseGcc = CreateWindowExW(0, L"BUTTON", L"选择…", buttonStyle,
                                    0, 0, 10, 10, window,
                                    reinterpret_cast<HMENU>(IDC_BROWSE_GCC), instance, nullptr);
        detectGcc = CreateWindowExW(0, L"BUTTON", L"自动检测", buttonStyle,
                                    0, 0, 10, 10, window,
                                    reinterpret_cast<HMENU>(IDC_DETECT_GCC), instance, nullptr);
        standard = CreateWindowExW(0, L"COMBOBOX", L"",
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                   0, 0, 10, 10, window,
                                   reinterpret_cast<HMENU>(IDC_STANDARD), instance, nullptr);
        SendMessageW(standard, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"C++11"));
        SendMessageW(standard, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"C++14"));
        SendMessageW(standard, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"C++17"));
        SendMessageW(standard, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"C++20"));
        SendMessageW(standard, CB_SETCURSEL, 3, 0);

        run = CreateWindowExW(0, L"BUTTON", L"预处理", buttonStyle,
                              0, 0, 10, 10, window,
                              reinterpret_cast<HMENU>(IDC_RUN), instance, nullptr);
        cancel = CreateWindowExW(0, L"BUTTON", L"取消", buttonStyle,
                                 0, 0, 10, 10, window,
                                 reinterpret_cast<HMENU>(IDC_CANCEL), instance, nullptr);
        options = CreateWindowExW(0, L"BUTTON", L"选项…", buttonStyle,
                                  0, 0, 10, 10, window,
                                  reinterpret_cast<HMENU>(IDC_OPTIONS), instance, nullptr);
        copyCli = CreateWindowExW(0, L"BUTTON", L"复制 CLI 命令", buttonStyle,
                                  0, 0, 10, 10, window,
                                  reinterpret_cast<HMENU>(IDC_COPY_CLI), instance, nullptr);
        outputView = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                         WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                                         ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                     0, 0, 10, 10, window,
                                     reinterpret_cast<HMENU>(IDC_OUTPUT_VIEW), instance, nullptr);
        diagView = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                       WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                                       ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                   0, 0, 10, 10, window,
                                   reinterpret_cast<HMENU>(IDC_DIAG_VIEW), instance, nullptr);
        status = CreateWindowExW(0, L"STATIC", L"准备就绪",
                                 WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 0, 0, 10, 10, window,
                                 reinterpret_cast<HMENU>(IDC_STATUS), instance, nullptr);
        SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        for (HWND control : {input, output, gcc}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        }
        for (HWND control : {browseInput, browseOutput, browseGcc, detectGcc, run, cancel, options, copyCli}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        }
        SendMessageW(standard, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        for (HWND control : {outputView, diagView}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(codeFont), TRUE);
            SendMessageW(control, EM_SETLIMITTEXT, 0x7ffffffe, 0);
        }
    }

    void layoutForWindow() {
        RECT client{};
        RECT windowRect{};
        GetClientRect(window, &client);
        GetWindowRect(window, &windowRect);
        int width = client.right - client.left;
        int height = client.bottom - client.top;
        const int physicalWidth = windowRect.right - windowRect.left;
        const int physicalHeight = windowRect.bottom - windowRect.top;
        // On a DPI-virtualized or very small desktop, client coordinates can be
        // larger than the real window. Clamp so every control stays visible.
        if (width > physicalWidth) width = physicalWidth;
        if (height > physicalHeight) height = physicalHeight;
        const UINT dpi = GetDpiForWindow(window);
        if (dpi > 96) {
            width = MulDiv(width, 96, static_cast<int>(dpi));
            height = MulDiv(height, 96, static_cast<int>(dpi));
        }
        layout(width, height);
    }

    void layout(int width, int height) {
        const int margin = 14;
        const int labelWidth = 104;
        const int buttonWidth = 86;
        const int row = 32;
        const int editX = margin + labelWidth;
        const int editW = std::max(180, width - editX - margin - buttonWidth - 12);
        int y = margin;
        for (int i = 0; i < 4; ++i) {
            MoveWindow(labels[i], margin, margin + i * row + 6,
                       labelWidth - 8, 22, TRUE);
        }
        MoveWindow(input, editX, y, editW, 25, TRUE);
        MoveWindow(browseInput, editX + editW + 8, y, buttonWidth, 25, TRUE);
        y += row;
        MoveWindow(output, editX, y, editW, 25, TRUE);
        MoveWindow(browseOutput, editX + editW + 8, y, buttonWidth, 25, TRUE);
        y += row;
        int gccW = std::max(160, editW - buttonWidth - 8);
        MoveWindow(gcc, editX, y, gccW, 25, TRUE);
        MoveWindow(browseGcc, editX + gccW + 8, y, buttonWidth, 25, TRUE);
        MoveWindow(detectGcc, editX + gccW + 8 + buttonWidth + 6, y, buttonWidth + 12, 25, TRUE);
        y += row;
        MoveWindow(standard, editX, y, 130, 200, TRUE);
        MoveWindow(run, editX + 150, y, 92, 27, TRUE);
        MoveWindow(cancel, editX + 248, y, 76, 27, TRUE);
        MoveWindow(options, editX + 332, y, 76, 27, TRUE);
        MoveWindow(copyCli, editX + 416, y, 112, 27, TRUE);
        y += 40;
        const int available = std::max(160, height - y - margin - 22);
        const int outputH = std::max(100, available * 62 / 100);
        const int diagH = std::max(80, available - outputH - 8);
        MoveWindow(outputView, margin, y, width - 2 * margin, outputH, TRUE);
        MoveWindow(diagView, margin, y + outputH + 8, width - 2 * margin, diagH, TRUE);
        MoveWindow(status, margin, height - 24, width - 2 * margin, 18, TRUE);
    }

    std::wstring textOf(HWND control) {
        wchar_t buffer[4096]{};
        LRESULT length = SendMessageW(control, WM_GETTEXT, 4096,
                                      reinterpret_cast<LPARAM>(buffer));
        if (length <= 0) length = GetWindowTextW(control, buffer, 4096);
        if (length <= 0) return {};
        return std::wstring(buffer, static_cast<std::size_t>(length));
    }

    void setText(HWND control, const std::wstring& value) {
        SetWindowTextW(control, value.c_str());
    }

    std::string selectedStandard() {
        const int index = static_cast<int>(SendMessageW(standard, CB_GETCURSEL, 0, 0));
        switch (index) {
        case 0: return "c++11";
        case 1: return "c++14";
        case 2: return "c++17";
        default: return "c++20";
        }
    }

    void setRunning(bool value) {
        running = value;
        for (HWND control : {input, browseInput, output, browseOutput, gcc,
                             browseGcc, detectGcc, standard, run, options}) {
            EnableWindow(control, !value);
        }
        EnableWindow(cancel, value);
        setText(status, value ? L"正在预处理…" : L"准备就绪");
    }

    void browseInputFile() {
        wchar_t file[MAX_PATH * 4]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter = L"C++ 文件\0*.cpp;*.cc;*.cxx;*.c;*.h;*.hpp\0所有文件\0*.*\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = static_cast<DWORD>(std::size(file));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return;
        selectedInput = file;
        setText(input, file);
        if (textOf(output).empty()) {
            std::filesystem::path path(file);
            path.replace_extension(L".i");
            selectedOutput = path.wstring();
            setText(output, path.wstring());
        }
    }

    void browseOutputFile() {
        wchar_t file[MAX_PATH * 4]{};
        const std::wstring current = textOf(output);
        if (!current.empty()) {
            wcsncpy_s(file, current.c_str(), std::size(file) - 1);
        }
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter = L"预处理输出\0*.i\0所有文件\0*.*\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = static_cast<DWORD>(std::size(file));
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        dialog.lpstrDefExt = L"i";
        if (!GetSaveFileNameW(&dialog)) return;
        selectedOutput = file;
        setText(output, file);
    }

    void browseGccFile() {
        wchar_t file[MAX_PATH * 4]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = window;
        dialog.lpstrFilter = L"GCC 编译器\0g++.exe;gcc.exe;c++.exe\0可执行文件\0*.exe\0";
        dialog.lpstrFile = file;
        dialog.nMaxFile = static_cast<DWORD>(std::size(file));
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!GetOpenFileNameW(&dialog)) return;
        selectedGcc = file;
        setText(gcc, file);
    }

    void showOptions();

    std::filesystem::path settingsPath() const {
        wchar_t appData[MAX_PATH * 2]{};
        const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData,
                                                     static_cast<DWORD>(std::size(appData)));
        std::filesystem::path base = length > 0 ? std::filesystem::path(appData)
                                                : std::filesystem::path(L".");
        base /= L"CrucibleCpp";
        std::error_code ec;
        std::filesystem::create_directories(base, ec);
        return base / L"settings.ini";
    }

    std::wstring getIni(const wchar_t* key, const wchar_t* fallback = L"") const {
        wchar_t buffer[32768]{};
        GetPrivateProfileStringW(L"gui", key, fallback, buffer,
                                 static_cast<DWORD>(std::size(buffer)),
                                 settingsPath().c_str());
        return buffer;
    }

    void putIni(const wchar_t* key, const std::wstring& value) const {
        WritePrivateProfileStringW(L"gui", key, value.c_str(),
                                   settingsPath().c_str());
    }

    void loadSettings() {
        selectedInput = getIni(L"input");
        selectedOutput = getIni(L"output");
        selectedGcc = getIni(L"gcc");
        setText(input, selectedInput);
        setText(output, selectedOutput);
        setText(gcc, selectedGcc);
        const int standardIndex = std::clamp(_wtoi(getIni(L"standard", L"3").c_str()), 0, 3);
        SendMessageW(standard, CB_SETCURSEL, static_cast<WPARAM>(standardIndex), 0);
        pchPolicy = static_cast<crucible::PchPolicy>(
            std::clamp(_wtoi(getIni(L"pch", L"0").c_str()), 0, 3));
        lineMarkers = getIni(L"lineMarkers", L"1") != L"0";
        keepComments = getIni(L"keepComments", L"0") != L"0";

        includeDirs.clear();
        for (const auto& part : splitOptionLines(getIni(L"includes"))) {
            includeDirs.emplace_back(part);
        }
        commandActions.clear();
        for (const auto& part : splitOptionLines(getIni(L"defines"))) {
            crucible::CommandLineAction action;
            action.kind = crucible::CommandLineAction::Kind::Define;
            action.text = wideToUtf8(part);
            commandActions.push_back(std::move(action));
        }
    }

    void saveSettings() const {
        putIni(L"input", selectedInput);
        putIni(L"output", selectedOutput);
        putIni(L"gcc", selectedGcc);
        putIni(L"standard", std::to_wstring(
            static_cast<int>(SendMessageW(standard, CB_GETCURSEL, 0, 0))));
        putIni(L"pch", std::to_wstring(static_cast<int>(pchPolicy)));
        putIni(L"lineMarkers", lineMarkers ? L"1" : L"0");
        putIni(L"keepComments", keepComments ? L"1" : L"0");
        std::wstring includes;
        for (const auto& dir : includeDirs) {
            if (!includes.empty()) includes += L"|";
            includes += dir.wstring();
        }
        std::wstring defines;
        for (const auto& action : commandActions) {
            if (!defines.empty()) defines += L"|";
            defines += utf8ToWide(action.text);
        }
        putIni(L"includes", includes);
        putIni(L"defines", defines);
    }

    std::wstring buildCliCommand() {
        std::wstring inputPath = selectedInput.empty() ? textOf(input) : selectedInput;
        std::wstring outputPath = selectedOutput.empty() ? textOf(output) : selectedOutput;
        std::wstring gccPath = selectedGcc.empty() ? textOf(gcc) : selectedGcc;
        std::wstring command = L"cruciblecpp-preprocessor.exe";
        if (!gccPath.empty()) {
            command += L" --gcc \"" + gccPath + L"\"";
        }
        const int standardIndex = static_cast<int>(SendMessageW(standard, CB_GETCURSEL, 0, 0));
        command += L" -std=" + std::wstring(standardIndex == 0 ? L"c++11" :
                                             standardIndex == 1 ? L"c++14" :
                                             standardIndex == 2 ? L"c++17" : L"c++20");
        if (!lineMarkers) command += L" -P";
        if (keepComments) command += L" -C";
        switch (pchPolicy) {
        case crucible::PchPolicy::Warn: command += L" --pch=warn"; break;
        case crucible::PchPolicy::Error: command += L" --pch=error"; break;
        case crucible::PchPolicy::Report: command += L" --pch=report"; break;
        default: break;
        }
        for (const auto& dir : includeDirs) {
            command += L" -I \"" + dir.wstring() + L"\"";
        }
        for (const auto& action : commandActions) {
            command += L" -D \"" + utf8ToWide(action.text) + L"\"";
        }
        command += L" \"" + inputPath + L"\" -o \"" + outputPath + L"\"";
        return command;
    }

    void copyToClipboard(const std::wstring& text) {
        if (!OpenClipboard(window)) return;
        EmptyClipboard();
        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (memory) {
            void* target = GlobalLock(memory);
            if (target) {
                memcpy(target, text.c_str(), bytes);
                GlobalUnlock(memory);
                if (!SetClipboardData(CF_UNICODETEXT, memory)) {
                    GlobalFree(memory);
                }
            } else {
                GlobalFree(memory);
            }
        }
        CloseClipboard();
    }

    void applyCommandLine() {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) return;
        for (int i = 1; i < argc; ++i) {
            const std::wstring arg = argv[i];
            if (arg == L"--input" && i + 1 < argc) {
                selectedInput = argv[++i];
                setText(input, selectedInput);
            } else if (arg == L"--output" && i + 1 < argc) {
                selectedOutput = argv[++i];
                setText(output, selectedOutput);
            } else if (arg == L"--gcc" && i + 1 < argc) {
                selectedGcc = argv[++i];
                setText(gcc, selectedGcc);
            }
        }
        LocalFree(argv);
    }

    void detectGccPath() {
        const crucible::GccDiscovery discovery = crucible::discoverGccFromPath();
        if (!discovery.found) {
            MessageBoxW(window, utf8ToWide(discovery.error.empty()
                                               ? "PATH 中没有找到 g++ / gcc"
                                               : discovery.error).c_str(),
                        L"GCC 检测", MB_ICONWARNING);
            return;
        }
        selectedGcc = discovery.compiler.wstring();
        setText(gcc, discovery.compiler.wstring());
        setText(status, (L"找到 GCC " + utf8ToWide(discovery.version)).c_str());
    }

    void startRun() {
        if (running) return;
        const std::wstring inputPath = selectedInput.empty() ? textOf(input) : selectedInput;
        if (inputPath.empty()) {
            MessageBoxW(window, L"请先选择源文件。", L"CrucibleCpp GUI", MB_ICONWARNING);
            return;
        }
        if (selectedOutput.empty() && textOf(output).empty()) {
            std::filesystem::path path(inputPath);
            path.replace_extension(L".i");
            setText(output, path.wstring());
        }
        cancelRequested.store(false);
        setRunning(true);
        resultOutput.clear();
        resultDiagnostics.clear();
        setText(outputView, L"");
        setText(diagView, L"");
        std::wstring gccPath = selectedGcc.empty() ? textOf(gcc) : selectedGcc;
        std::wstring outputPath = selectedOutput.empty() ? textOf(output) : selectedOutput;
        std::string standard = selectedStandard();
        worker = std::thread([this, inputPath, outputPath, gccPath, standard]() {
            crucible::GccDiscovery discovery;
            if (!gccPath.empty()) {
                discovery = crucible::discoverGcc(std::filesystem::path(gccPath));
            } else {
                discovery = crucible::discoverGccFromPath();
            }
            if (!discovery.found) {
                resultDiagnostics = "error: " +
                    (discovery.error.empty() ? std::string("GCC not found")
                                             : discovery.error);
                resultStatus = "预处理失败：找不到目标 GCC";
                exitCode = 2;
                PostMessageW(window, kRunDoneMessage, 0, 0);
                return;
            }

            crucible::PreprocessorOptions options;
            options.input = std::filesystem::path(inputPath);
            options.output = std::filesystem::path(outputPath);
            options.gccCompiler = discovery.compiler;
            options.gccMajor = parseGccMajor(discovery.version);
            options.standard = standard;
            options.autoGccIncludes = true;
            options.cancelRequested = &cancelRequested;
            options.includeDirs = includeDirs;
            options.commandActions = commandActions;
            options.pchPolicy = pchPolicy;
            options.lineMarkers = lineMarkers;
            options.keepComments = keepComments;
            options.lineMarkers = true;
            options.maxErrors = 50;

            int code = 2;
            try {
                crucible::Preprocessor preprocessor(std::move(options));
                code = preprocessor.run();
                std::ostringstream diagnostics;
                for (const auto& diagnostic : preprocessor.diagnostics().records()) {
                    diagnostics << diagnosticPath(diagnostic) << ':'
                                << diagnostic.loc.line << ':'
                                << diagnostic.loc.column << ": "
                                << severityText(diagnostic.severity) << ": "
                                << diagnostic.message << "\n";
                }
                resultDiagnostics = diagnostics.str();
            } catch (const std::exception& error) {
                resultDiagnostics = std::string("fatal error: ") + error.what() + "\n";
            } catch (...) {
                resultDiagnostics = "fatal error: unknown exception\n";
            }
            exitCode = code;
            bool truncated = false;
            std::string output = readFile(std::filesystem::path(outputPath), truncated);
            if (truncated) {
                output += "\n\n[GUI: output truncated for preview]\n";
            }
            resultOutput = std::move(output);
            resultStatus = code == 0 ? "预处理完成" :
                (code == 1 ? "预处理完成，但存在错误" :
                 (code == 130 ? "已取消" : "预处理失败"));
            PostMessageW(window, kRunDoneMessage, 0, 0);
        });
    }

    void finishRun() {
        if (worker.joinable()) worker.join();
        setRunning(false);
        setText(outputView, utf8ToWide(resultOutput).c_str());
        setText(diagView, utf8ToWide(resultDiagnostics).c_str());
        std::wstring statusText = utf8ToWide(resultStatus);
        if (!resultDiagnostics.empty()) {
            statusText += L"（诊断 " + std::to_wstring(
                std::count(resultDiagnostics.begin(), resultDiagnostics.end(), '\n')) + L" 条）";
        }
        setText(status, statusText);
    }

    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            createControls();
            loadSettings();
            layoutForWindow();
            return 0;
        case WM_SIZE:
            layoutForWindow();
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case IDC_BROWSE_INPUT: browseInputFile(); return 0;
            case IDC_BROWSE_OUTPUT: browseOutputFile(); return 0;
            case IDC_BROWSE_GCC: browseGccFile(); return 0;
            case IDC_DETECT_GCC: detectGccPath(); return 0;
            case IDC_RUN: startRun(); return 0;
            case IDC_CANCEL: cancelRequested.store(true); setText(status, L"正在取消…"); return 0;
            case IDC_OPTIONS: showOptions(); return 0;
            case IDC_COPY_CLI:
                copyToClipboard(buildCliCommand());
                setText(status, L"CLI 命令已复制到剪贴板");
                return 0;
            default: break;
            }
            break;
        case kRunDoneMessage:
            finishRun();
            return 0;
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            wchar_t file[32768]{};
            if (DragQueryFileW(drop, 0, file, static_cast<UINT>(std::size(file))) > 0) {
                selectedInput = file;
                setText(input, selectedInput);
                if (selectedOutput.empty()) {
                    std::filesystem::path path(file);
                    path.replace_extension(L".i");
                    selectedOutput = path.wstring();
                    setText(output, selectedOutput);
                }
            }
            DragFinish(drop);
            return 0;
        }
        case WM_CLOSE:
            if (running) {
                MessageBoxW(window, L"预处理仍在进行，请等待完成。",
                            L"CrucibleCpp GUI", MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            saveSettings();
            if (codeFont) DeleteObject(codeFont);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
};


struct OptionsState {
    AppState* app = nullptr;
    HWND window = nullptr;
    HWND includes = nullptr;
    HWND defines = nullptr;
    HWND pch = nullptr;
    HWND lineMarkers = nullptr;
    HWND keepComments = nullptr;
};

std::wstring optionsText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

std::vector<std::wstring> splitOptionLines(std::wstring text) {
    std::vector<std::wstring> result;
    std::wstring current;
    for (wchar_t c : text) {
        if (c == L'\r' || c == L'\n' || c == L';') {
            if (!current.empty()) {
                const auto first = current.find_first_not_of(L" \t");
                const auto last = current.find_last_not_of(L" \t");
                if (first != std::wstring::npos) {
                    result.push_back(current.substr(first, last - first + 1));
                }
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        const auto first = current.find_first_not_of(L" \t");
        const auto last = current.find_last_not_of(L" \t");
        if (first != std::wstring::npos) {
            result.push_back(current.substr(first, last - first + 1));
        }
    }
    return result;
}

LRESULT CALLBACK OptionsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<OptionsState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = static_cast<OptionsState*>(create->lpCreateParams);
            state->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        } else {
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }
    switch (message) {
    case WM_CREATE: {
        const DWORD labelStyle = WS_CHILD | WS_VISIBLE | SS_LEFT;
        const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN;
        const DWORD buttonStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON;
        auto makeLabel = [&](const wchar_t* text, int x, int y, int w) {
            HWND control = CreateWindowExW(0, L"STATIC", text, labelStyle, x, y, w, 20,
                                           window, nullptr, state->app->instance, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->app->uiFont), TRUE);
            return control;
        };
        auto makeButton = [&](const wchar_t* text, int id, int x, int y, int w) {
            HWND control = CreateWindowExW(0, L"BUTTON", text, buttonStyle, x, y, w, 28,
                                           window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                           state->app->instance, nullptr);
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->app->uiFont), TRUE);
            return control;
        };
        makeLabel(L"Include 目录（每行一个，或用分号分隔）", 16, 14, 480);
        state->includes = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
                                          16, 38, 480, 86, window,
                                          reinterpret_cast<HMENU>(301), state->app->instance, nullptr);
        makeLabel(L"宏定义（每行一个，例如 FOO=1）", 16, 132, 480);
        state->defines = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", editStyle,
                                         16, 156, 480, 72, window,
                                         reinterpret_cast<HMENU>(302), state->app->instance, nullptr);
        makeLabel(L"PCH 策略", 16, 242, 90);
        state->pch = CreateWindowExW(0, L"COMBOBOX", L"",
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                     106, 238, 180, 160, window,
                                     reinterpret_cast<HMENU>(303), state->app->instance, nullptr);
        SendMessageW(state->pch, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"忽略 .gch"));
        SendMessageW(state->pch, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"警告"));
        SendMessageW(state->pch, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"报错"));
        SendMessageW(state->pch, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"记录到 analysis"));
        SendMessageW(state->pch, CB_SETCURSEL, static_cast<WPARAM>(state->app->pchPolicy), 0);
        state->lineMarkers = CreateWindowExW(0, L"BUTTON", L"输出行标记",
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                              16, 278, 200, 24, window,
                                              reinterpret_cast<HMENU>(304), state->app->instance, nullptr);
        state->keepComments = CreateWindowExW(0, L"BUTTON", L"保留注释",
                                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                               16, 306, 200, 24, window,
                                               reinterpret_cast<HMENU>(305), state->app->instance, nullptr);
        SendMessageW(state->lineMarkers, BM_SETCHECK,
                     state->app->lineMarkers ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(state->keepComments, BM_SETCHECK,
                     state->app->keepComments ? BST_CHECKED : BST_UNCHECKED, 0);
        makeButton(L"确定", IDOK, 310, 350, 84);
        makeButton(L"取消", IDCANCEL, 404, 350, 84);
        for (HWND control : {state->includes, state->defines, state->pch,
                             state->lineMarkers, state->keepComments}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->app->uiFont), TRUE);
        }
        std::wstring includeText;
        for (const auto& dir : state->app->includeDirs) {
            if (!includeText.empty()) includeText += L"\r\n";
            includeText += dir.wstring();
        }
        SetWindowTextW(state->includes, includeText.c_str());
        std::wstring defineText;
        for (const auto& action : state->app->commandActions) {
            if (!defineText.empty()) defineText += L"\r\n";
            defineText += utf8ToWide(action.text);
        }
        SetWindowTextW(state->defines, defineText.c_str());
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            state->app->includeDirs.clear();
            for (const auto& line : splitOptionLines(optionsText(state->includes))) {
                state->app->includeDirs.emplace_back(line);
            }
            state->app->commandActions.clear();
            for (const auto& line : splitOptionLines(optionsText(state->defines))) {
                crucible::CommandLineAction action;
                action.kind = crucible::CommandLineAction::Kind::Define;
                action.text = wideToUtf8(line);
                state->app->commandActions.push_back(std::move(action));
            }
            const int pchIndex = static_cast<int>(SendMessageW(state->pch, CB_GETCURSEL, 0, 0));
            switch (pchIndex) {
            case 1: state->app->pchPolicy = crucible::PchPolicy::Warn; break;
            case 2: state->app->pchPolicy = crucible::PchPolicy::Error; break;
            case 3: state->app->pchPolicy = crucible::PchPolicy::Report; break;
            default: state->app->pchPolicy = crucible::PchPolicy::Ignore; break;
            }
            state->app->lineMarkers =
                SendMessageW(state->lineMarkers, BM_GETCHECK, 0, 0) == BST_CHECKED;
            state->app->keepComments =
                SendMessageW(state->keepComments, BM_GETCHECK, 0, 0) == BST_CHECKED;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        delete state;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void showOptionsDialog(AppState* app) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = OptionsProc;
        windowClass.hInstance = app->instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = L"CrucibleCppOptionsWindow";
        if (!RegisterClassExW(&windowClass)) return;
        registered = true;
    }
    auto* state = new OptionsState{};
    state->app = app;
    RECT parent{};
    GetWindowRect(app->window, &parent);
    const int width = 520;
    const int height = 430;
    EnableWindow(app->window, FALSE);
    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"CrucibleCppOptionsWindow", L"CrucibleCpp 选项",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        parent.left + 80, parent.top + 60, width, height,
        app->window, nullptr, app->instance, state);
    if (!dialog) {
        delete state;
        EnableWindow(app->window, TRUE);
        return;
    }
    UpdateWindow(dialog);
    MSG message{};
    while (IsWindow(dialog) && GetMessageW(&message, nullptr, 0, 0)) {
        if (!IsDialogMessageW(dialog, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    EnableWindow(app->window, TRUE);
    SetForegroundWindow(app->window);
}

void AppState::showOptions() {
    showOptionsDialog(this);
}

AppState* stateFrom(HWND window) {
    return reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    AppState* state = stateFrom(window);
    if (!state) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = static_cast<AppState*>(create->lpCreateParams);
            state->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(state));
        } else {
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }
    return state->handle(message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    // Avoid DPI virtualization so layout uses real client pixels. The manifest
    // requests PerMonitorV2; this fallback also works with older MinGW headers.
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    AppState state;
    state.instance = instance;
    state.uiFont = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    state.codeFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Consolas");

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass)) return 1;

    HWND window = CreateWindowExW(
        0, kWindowClass, L"CrucibleCpp GUI", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760, nullptr, nullptr, instance, &state);
    if (!window) return 1;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const bool tooLarge = (work.right - work.left) < 1100 ||
                          (work.bottom - work.top) < 760;
    DragAcceptFiles(window, TRUE);
    ShowWindow(window, tooLarge ? SW_SHOWMAXIMIZED : showCommand);
    UpdateWindow(window);
    // The first WM_SIZE can arrive before the child controls exist. Force one
    // final layout against the real client rectangle after the window is shown.
    RECT client{};
    GetClientRect(window, &client);
    SendMessageW(window, WM_SIZE, 0,
                 MAKELPARAM(client.right - client.left, client.bottom - client.top));

    state.applyCommandLine();
    RECT client2{};
    GetClientRect(window, &client2);
    SendMessageW(window, WM_SIZE, 0,
                 MAKELPARAM(client2.right - client2.left, client2.bottom - client2.top));
    if (GetCommandLineW() && wcsstr(GetCommandLineW(), L"--run")) {
        PostMessageW(window, WM_COMMAND, IDC_RUN, 0);
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}


