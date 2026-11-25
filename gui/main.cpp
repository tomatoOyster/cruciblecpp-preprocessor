#include "GccDiscovery.h"
#include "Preprocessor.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
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
    HWND outputView = nullptr;
    HWND diagView = nullptr;
    HWND status = nullptr;
    HWND labels[4]{};
    HFONT uiFont = nullptr;
    HFONT codeFont = nullptr;
    std::thread worker;
    std::wstring selectedInput;
    std::wstring selectedOutput;
    std::wstring selectedGcc;
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
        for (HWND control : {browseInput, browseOutput, browseGcc, detectGcc, run}) {
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
        MoveWindow(run, editX + 150, y, 110, 27, TRUE);
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
                             browseGcc, detectGcc, standard, run}) {
            EnableWindow(control, !value);
        }
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
                (code == 1 ? "预处理完成，但存在错误" : "预处理失败");
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
            default: break;
            }
            break;
        case kRunDoneMessage:
            finishRun();
            return 0;
        case WM_CLOSE:
            if (running) {
                MessageBoxW(window, L"预处理仍在进行，请等待完成。",
                            L"CrucibleCpp GUI", MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (codeFont) DeleteObject(codeFont);
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
};

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

