#include "GccDiscovery.h"
#include "Preprocessor.h"

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <thread>
#include <vector>

namespace {


constexpr wchar_t kWindowClass[] = L"CrucibleCppGuiWindow";
constexpr UINT kRunDoneMessage = WM_APP + 1;
constexpr UINT kLayoutMessage = WM_APP + 2;
constexpr UINT kAuxDoneMessage = WM_APP + 3;
constexpr int kMaxDisplayBytes = 8 * 1024 * 1024;

constexpr COLORREF kBackground = RGB(244, 251, 249);
constexpr COLORREF kPanel = RGB(255, 255, 255);
constexpr COLORREF kHeader = RGB(7, 59, 58);
constexpr COLORREF kAccent = RGB(8, 127, 120);
constexpr COLORREF kAccentSoft = RGB(224, 247, 243);
constexpr COLORREF kText = RGB(7, 59, 58);
constexpr COLORREF kMuted = RGB(76, 112, 109);
constexpr COLORREF kBorder = RGB(204, 226, 222);
constexpr COLORREF kDanger = RGB(176, 58, 58);
constexpr wchar_t kAgreementText[] = LR"AGREEMENT(CrucibleCpp 用户协议

协议版本：1.0
生效日期：2026 年 11 月 26 日

1. 协议适用与接受

本协议适用于 CrucibleCpp 的命令行版本、Windows GUI、源代码、文档及相关发行文件（以下统称“本软件”）。

下载、安装、复制、运行或以其他方式使用本软件，即表示你已阅读、理解并同意本协议。如果你不同意本协议，请停止使用并删除本软件。

本协议是使用官方发行版的补充说明。本软件不同版本同时受其对应开源许可证约束；如本协议与该许可证存在冲突，以对应许可证为准。

2. 定义

开发者：CrucibleCpp 项目维护者。
官方渠道：CrucibleCpp 的 GitHub 仓库、GitHub Releases 和 hfsdr.cn 官网。
用户：下载、安装或使用本软件的个人或组织。
商业使用：以营利、获取商业利益或商业运营为目的的使用，包括但不限于销售、付费分发、商业产品集成、付费服务、商业内部生产流程等。

3. 许可证与版本

V3.0.0 及以后：采用 CC BY-NC 4.0（署名—非商业性使用 4.0 国际）。
V2.9.0 及以前：继续采用 MIT License，旧版本不会被追溯变更。

不同版本的许可证文本以发行包中的 LICENSE、LICENSE-MIT 和 NOTICE 为准。

4. 允许的使用

在遵守对应许可证和本协议的前提下，你可以：

1. 个人学习、研究、开发和测试；
2. 在非商业项目中使用；
3. 复制、修改源代码，但必须保留版权、许可证和免责声明；
4. 在非商业范围内再分发原始或修改版本，并附上对应许可证；
5. 将本软件用于开源项目、教学、竞赛、学术研究等非商业场景；
6. 查看、修改和分发 GUI/CLI 源代码，但不得移除许可证和署名信息。

再分发时，不得暗示开发者对你的修改版本提供背书或支持。

5. 商业使用与商业授权

未经开发者书面授权，不得将本软件用于商业用途，包括但不限于：

销售本软件或其修改版本；
将本软件打包进商业软件、硬件或服务；
提供基于本软件的付费预处理、编译、咨询或托管服务；
在企业商业生产流程中直接使用；
以本软件为基础制作商业培训、商业课程或商业发行版。

商业使用需要单独取得商业授权。商业授权范围、费用和支持方式由双方另行约定。

6. 使用限制

使用本软件时，你不得：

1. 移除、隐藏或篡改版权声明、许可证、免责声明或作者标识；
2. 将本软件用于违法、侵权、恶意软件或其他危害他人的活动；
3. 冒充官方版本，或声称修改版本由开发者官方维护；
4. 使用 CrucibleCpp 名称、标识或作者名义进行误导性宣传；
5. 以违反适用法律或第三方许可证的方式使用本软件或其输出。

7. 用户代码与输出文件

你输入的源代码、头文件、宏定义、Include 路径以及生成的 .i、.exe 等输出文件，其权利仍归你或原权利人所有。

开发者不对你的代码内容主张所有权。你需要自行确保：

你有权处理相关代码和文件；
你的使用不侵犯第三方权利；
生成的输出符合你的项目许可证和法律规定；
在编译、分发或运行输出文件前自行完成测试和备份。

8. GCC 与第三方组件

本软件依赖用户自行安装的 GCC 工具链。GCC、MinGW-w64 及其运行时组件受其各自许可证约束，不属于本协议授权范围。

本软件不随包分发 GCC。你需要自行取得合法安装的 GCC，并遵守其许可证。不同 GCC 版本的预处理行为、头文件和编译结果可能不同，开发者不对第三方工具链的行为作出保证。

9. 隐私与本地处理

当前版本的 CLI 和 GUI 在本机执行预处理，不包含遥测、账号系统或代码上传功能。GUI 调用本机 GCC 进行预处理、对比和编译。

如果你未来使用包含网络功能的版本，开发者会通过协议更新或单独说明告知数据处理方式。

10. 无担保

本软件按“现状”提供，不附带任何明示或默示担保，包括但不限于：

适销性；
特定用途适用性；
不侵权；
无错误或无中断运行；
与特定 GCC 版本、项目或构建系统完全兼容。

你需要自行评估本软件是否适合你的项目，并自行备份代码和构建产物。

11. 责任限制

在适用法律允许的最大范围内，开发者不对因使用或无法使用本软件而产生的任何直接、间接、偶然、特殊或后果性损失承担责任，包括但不限于：

数据丢失；
构建失败；
项目延期；
利润损失；
第三方索赔；
因 GCC 或用户代码导致的问题。

本软件通常免费提供；在适用法律允许的范围内，开发者的累计责任不超过你为官方版本实际支付的费用。

12. 协议变更与终止

开发者可能随版本更新修改本协议。更新后的协议会在官方仓库或官网发布，并标注新的生效日期。

如果你违反本协议，开发者可以终止你使用官方发行版和官方服务的许可。终止后，你应停止使用并删除相关副本。

本协议终止后，第 7、8、10、11 和 13 条继续有效。

13. 其他

本协议部分条款被认定无效或不可执行时，不影响其他条款的效力。
本协议与对应开源许可证共同构成你对官方发行版使用的完整约定。
如果你需要商业授权、二次分发许可或其他例外安排，请通过项目仓库或官网联系开发者。

14. 联系方式

项目仓库：https://github.com/tomatoOyster/cruciblecpp-preprocessor
官网：https://hfsdr.cn
问题反馈：GitHub Issues
商业授权：通过项目仓库或官网联系
)AGREEMENT";


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
    IDC_OUTPUT_LABEL,
    IDC_DIAG_LABEL,
    IDC_CANCEL,
    IDC_OPTIONS,
    IDC_COPY_CLI,
    IDC_COMPARE_GCC,
    IDC_COMPILE_OUTPUT,
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

std::wstring editMultiline(std::wstring text) {
    std::wstring result;
    result.reserve(text.size() + text.size() / 16);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\n' && (i == 0 || text[i - 1] != L'\r')) {
            result += L"\r\n";
        } else {
            result.push_back(text[i]);
        }
    }
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

COLORREF blendColor(COLORREF from, COLORREF to, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const auto mix = [t](int a, int b) {
        return static_cast<int>(a + (b - a) * t + 0.5f);
    };
    return RGB(mix(GetRValue(from), GetRValue(to)),
               mix(GetGValue(from), GetGValue(to)),
               mix(GetBValue(from), GetBValue(to)));
}

struct ProcessResult {
    bool started = false;
    DWORD exitCode = 1;
    std::string text;
};

std::wstring quoteArgument(const std::wstring& value) {
    std::wstring result = L"\"";
    for (wchar_t c : value) {
        if (c == L'"') result += L"\\\"";
        else result.push_back(c);
    }
    result += L"\"";
    return result;
}

std::wstring cppCompilerFor(const std::wstring& compiler) {
    if (compiler.empty()) return compiler;
    std::filesystem::path path(compiler);
    const std::wstring name = path.filename().wstring();
    if (name == L"gcc.exe" || name == L"gcc") {
        std::filesystem::path candidate = path.parent_path() / L"g++.exe";
        std::error_code ec;
        if (std::filesystem::exists(candidate, ec)) return candidate.wstring();
    }
    return compiler;
}

ProcessResult runProcess(const std::wstring& command,
                         const std::filesystem::path& workingDirectory) {
    ProcessResult result;
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &security, 0)) return result;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    startup.hStdOutput = writePipe;
    startup.hStdError = writePipe;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    std::wstring cwd = workingDirectory.wstring();
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, cwd.empty() ? nullptr : cwd.c_str(),
                        &startup, &process)) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return result;
    }
    CloseHandle(writePipe);
    std::string output;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
        output.append(buffer, read);
        if (output.size() > 16 * 1024 * 1024) break;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    GetExitCodeProcess(process.hProcess, &result.exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(readPipe);
    result.started = true;
    result.text = std::move(output);
    return result;
}

std::vector<std::string> compareLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (char c : text) {
        if (c == '\n') {
            if (!current.empty() && current.back() == '\r') current.pop_back();
            const auto first = current.find_first_not_of(" \t");
            const auto last = current.find_last_not_of(" \t");
            if (first != std::string::npos) {
                std::string line = current.substr(first, last - first + 1);
                // Line markers are path/offset dependent; compare the tokens.
                if (!(line.size() > 2 && line[0] == '#' &&
                      (line[1] == ' ' || line[1] == 'l'))) {
                    lines.push_back(std::move(line));
                }
            }
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        const auto first = current.find_first_not_of(" \t");
        const auto last = current.find_last_not_of(" \t");
        if (first != std::string::npos) lines.push_back(current.substr(first, last - first + 1));
    }
    return lines;
}

std::string comparePreprocessed(const std::string& crucible, const std::string& gcc) {
    const auto left = compareLines(crucible);
    const auto right = compareLines(gcc);
    const std::size_t maxLines = 3000;
    if (left.size() > maxLines || right.size() > maxLines) {
        return "文件过大，跳过逐行对比（上限 " + std::to_string(maxLines) + " 行）\n";
    }
    std::ostringstream diff;
    std::size_t differences = 0;
    const std::size_t count = std::max(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i) {
        const std::string* a = i < left.size() ? &left[i] : nullptr;
        const std::string* b = i < right.size() ? &right[i] : nullptr;
        if (a && b && *a == *b) continue;
        if (differences++ >= 200) continue;
        diff << "行 " << (i + 1) << "\n";
        diff << "- " << (a ? *a : "<missing>") << "\n";
        diff << "+ " << (b ? *b : "<missing>") << "\n";
    }
    if (differences == 0) {
        return "一致（忽略行标记和空行）\n";
    }
    diff << "\n共 " << differences << " 处差异";
    if (differences > 200) diff << "，仅显示前 200 处";
    diff << "\n";
    return diff.str();
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
    HWND compareGcc = nullptr;
    HWND compileOutput = nullptr;
    HWND outputView = nullptr;
    HWND diagView = nullptr;
    HWND status = nullptr;
    HWND labels[4]{};
    HWND outputLabel = nullptr;
    HWND diagLabel = nullptr;
    HFONT uiFont = nullptr;
    HFONT titleFont = nullptr;
    HFONT subtitleFont = nullptr;
    HFONT codeFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    RECT formRect{};
    RECT resultRect{};
    RECT outputLabelRect{};
    RECT diagLabelRect{};
    int lastClientWidth = -1;
    int lastClientHeight = -1;
    std::unordered_map<HWND, float> hoverProgress;
    bool hoverTimerRunning = false;
    std::thread worker;
    std::thread auxWorker;
    int auxAction = 0; // 1 = compare, 2 = compile
    std::string auxText;
    std::string auxStatus;
    std::atomic_bool cancelRequested{false};
    std::wstring selectedInput;
    std::wstring selectedOutput;
    std::wstring selectedGcc;
    std::vector<std::filesystem::path> includeDirs;
    std::vector<crucible::CommandLineAction> commandActions;
    crucible::PchPolicy pchPolicy = crucible::PchPolicy::Ignore;
    bool lineMarkers = true;
    bool keepComments = false;
    bool agreementAccepted = false;
    bool running = false;
    int exitCode = 0;
    std::string resultOutput;
    std::string resultDiagnostics;
    std::string resultStatus;

    void createControls() {
        const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL;
        const DWORD buttonStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
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
        options = CreateWindowExW(0, L"BUTTON", L"选项", buttonStyle,
                                  0, 0, 10, 10, window,
                                  reinterpret_cast<HMENU>(IDC_OPTIONS), instance, nullptr);
        copyCli = CreateWindowExW(0, L"BUTTON", L"复制 CLI", buttonStyle,
                                  0, 0, 10, 10, window,
                                  reinterpret_cast<HMENU>(IDC_COPY_CLI), instance, nullptr);
        compareGcc = CreateWindowExW(0, L"BUTTON", L"对比 g++ -E", buttonStyle,
                                     0, 0, 10, 10, window,
                                     reinterpret_cast<HMENU>(IDC_COMPARE_GCC), instance, nullptr);
        compileOutput = CreateWindowExW(0, L"BUTTON", L"编译 .i", buttonStyle,
                                        0, 0, 10, 10, window,
                                        reinterpret_cast<HMENU>(IDC_COMPILE_OUTPUT), instance, nullptr);
        outputView = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                     WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                         WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                                         ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                                         WS_CLIPSIBLINGS,
                                     0, 0, 10, 10, window,
                                     reinterpret_cast<HMENU>(IDC_OUTPUT_VIEW), instance, nullptr);
        diagView = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | WS_VSCROLL |
                                       WS_HSCROLL | ES_MULTILINE | ES_READONLY |
                                       ES_AUTOVSCROLL | ES_AUTOHSCROLL |
                                       WS_CLIPSIBLINGS,
                                   0, 0, 10, 10, window,
                                   reinterpret_cast<HMENU>(IDC_DIAG_VIEW), instance, nullptr);
        outputLabel = CreateWindowExW(0, L"STATIC", L"输出 .i",
                                     WS_CHILD | WS_VISIBLE | SS_LEFT,
                                     0, 0, 10, 10, window,
                                     reinterpret_cast<HMENU>(IDC_OUTPUT_LABEL), instance, nullptr);
        diagLabel = CreateWindowExW(0, L"STATIC", L"诊断",
                                   WS_CHILD | WS_VISIBLE | SS_LEFT,
                                   0, 0, 10, 10, window,
                                   reinterpret_cast<HMENU>(IDC_DIAG_LABEL), instance, nullptr);
        for (HWND label : {outputLabel, diagLabel}) {
            SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        }
        status = CreateWindowExW(0, L"STATIC", L"准备就绪",
                                 WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 0, 0, 10, 10, window,
                                 reinterpret_cast<HMENU>(IDC_STATUS), instance, nullptr);
        SendMessageW(status, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        for (HWND control : {input, output, gcc}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        }
        for (HWND control : {browseInput, browseOutput, browseGcc, detectGcc, run, cancel, options, copyCli, compareGcc, compileOutput}) {
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
        lastClientWidth = width;
        lastClientHeight = height;
        const int physicalWidth = windowRect.right - windowRect.left;
        const int physicalHeight = windowRect.bottom - windowRect.top;
        // On a DPI-virtualized or very small desktop, client coordinates can be
        // larger than the real window. Clamp so every control stays visible.
        if (width > physicalWidth) width = physicalWidth;
        if (height > physicalHeight) height = physicalHeight;
        // Some DPI-virtualized MinGW builds report a logical client rectangle
        // larger than the real window. Clamp to the actual outer window so the
        // output panes never extend below the visible frame.
        RECT frame{};
        if (GetWindowRect(window, &frame)) {
            const int frameWidth = frame.right - frame.left;
            const int frameHeight = frame.bottom - frame.top;
            if (frameWidth > 0) width = std::min(width, frameWidth - 2);
            if (frameHeight > 0) height = std::min(height, frameHeight - 2);
        }
        layout(width, height);
    }

    void layout(int width, int height) {
        const int margin = 18;
        const int top = 92;
        const int labelWidth = 94;
        const int buttonWidth = 78;
        const int row = 34;
        const int editX = margin + labelWidth;
        const int editW = std::max(180, width - editX - margin - buttonWidth - 12);
        int y = top;
        for (int i = 0; i < 4; ++i) {
            MoveWindow(labels[i], margin, top + i * row + 7,
                       labelWidth - 8, 22, TRUE);
        }
        MoveWindow(input, editX, y, editW, 27, TRUE);
        MoveWindow(browseInput, editX + editW + 8, y, buttonWidth, 27, TRUE);
        y += row;
        MoveWindow(output, editX, y, editW, 27, TRUE);
        MoveWindow(browseOutput, editX + editW + 8, y, buttonWidth, 27, TRUE);
        y += row;
        int gccW = std::max(150, editW - buttonWidth - 8);
        MoveWindow(gcc, editX, y, gccW, 27, TRUE);
        MoveWindow(browseGcc, editX + gccW + 8, y, buttonWidth, 27, TRUE);
        MoveWindow(detectGcc, editX + gccW + 8 + buttonWidth + 6, y,
                   buttonWidth + 10, 27, TRUE);
        y += row;
        MoveWindow(standard, editX, y, 120, 200, TRUE);
        MoveWindow(run, editX + 128, y, 72, 29, TRUE);
        MoveWindow(cancel, editX + 206, y, 56, 29, TRUE);
        MoveWindow(compareGcc, editX + 268, y, 96, 29, TRUE);
        MoveWindow(compileOutput, editX + 370, y, 70, 29, TRUE);
        MoveWindow(options, editX + 446, y, 56, 29, TRUE);
        MoveWindow(copyCli, editX + 508, y, 96, 29, TRUE);
        formRect = {margin - 8, top - 12, width - margin + 8, top + row * 4 + 12};
        y = top + row * 4 + 24;
        outputLabelRect = {margin, y, width - margin, y + 20};
        ShowWindow(outputLabel, SW_HIDE);
        y += 22;
        const int statusTop = height - 28;
        const int contentBottom = std::max(y + 80, statusTop - 8);
        const int available = std::max(80, contentBottom - y);
        const int outputH = std::max(80, available * 62 / 100);
        const int diagTop = y + outputH + 8;
        const int diagViewTop = diagTop + 22;
        const int diagH = std::max(50, contentBottom - diagViewTop);
        MoveWindow(outputView, margin, y, width - 2 * margin, outputH, TRUE);
        resultRect = {margin - 8, diagTop - 2, width - margin + 8,
                      diagViewTop + diagH + 6};
        diagLabelRect = {margin, diagTop, width - margin, diagTop + 20};
        ShowWindow(diagLabel, SW_HIDE);
        MoveWindow(diagView, margin, diagViewTop, width - 2 * margin, diagH, TRUE);
        MoveWindow(status, margin, statusTop, width - 2 * margin, 18, TRUE);
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
                             browseGcc, detectGcc, standard, run, options, compareGcc,
                             compileOutput}) {
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

    void recordLaunch() const {
        const std::filesystem::path path = settingsPath().parent_path() /
                                           L"launch-count.ini";
        wchar_t buffer[32]{};
        GetPrivateProfileStringW(L"stats", L"launches", L"0", buffer,
                                 static_cast<DWORD>(std::size(buffer)),
                                 path.c_str());
        const unsigned long long previous = _wcstoui64(buffer, nullptr, 10);
        const std::wstring next = std::to_wstring(previous + 1);
        WritePrivateProfileStringW(L"stats", L"launches", next.c_str(),
                                   path.c_str());
    }

    void loadSettings() {
        agreementAccepted = getIni(L"agreement", L"0") == L"1";
        // Source and output start empty by design; the user must choose them
        // explicitly for every new session.
        selectedInput.clear();
        selectedOutput.clear();
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

    std::wstring effectiveOutputPath() {
        return selectedOutput.empty() ? textOf(output) : selectedOutput;
    }

    std::wstring effectiveGccPath() {
        return selectedGcc.empty() ? textOf(gcc) : selectedGcc;
    }

    std::wstring standardOption() const {
        const int index = static_cast<int>(SendMessageW(standard, CB_GETCURSEL, 0, 0));
        return index == 0 ? L"c++11" : index == 1 ? L"c++14" :
               index == 2 ? L"c++17" : L"c++20";
    }

    void startAux(int action) {
        if (running) return;
        const std::wstring outputPath = effectiveOutputPath();
        if (outputPath.empty()) {
            MessageBoxW(window, L"请先设置输出 .i 文件。", L"CrucibleCpp GUI",
                        MB_ICONWARNING);
            return;
        }
        std::error_code ec;
        if (!std::filesystem::exists(outputPath, ec)) {
            MessageBoxW(window, L"输出 .i 不存在，请先执行预处理。", L"CrucibleCpp GUI",
                        MB_ICONWARNING);
            return;
        }
        auxAction = action;
        auxText.clear();
        auxStatus.clear();
        cancelRequested.store(false);
        setRunning(true);
        setText(status, action == 1 ? L"正在对比 g++ -E…" : L"正在编译 .i…");
        const std::wstring inputPath = selectedInput.empty() ? textOf(input) : selectedInput;
        const std::wstring gccPath = effectiveGccPath();
        const std::wstring standardName = standardOption();
        const auto includes = includeDirs;
        const auto defines = commandActions;
        const bool keepLines = lineMarkers;
        const bool keepCommentsOption = keepComments;
        auxWorker = std::thread([this, action, outputPath, inputPath, gccPath,
                                 standardName, includes, defines, keepLines,
                                 keepCommentsOption]() {
            std::wstring compiler = cppCompilerFor(gccPath);
            if (compiler.empty()) {
                const auto discovery = crucible::discoverGccFromPath();
                if (!discovery.found) {
                    auxStatus = "失败：找不到 GCC";
                    auxText = discovery.error.empty() ? "GCC not found" : discovery.error;
                    PostMessageW(window, kAuxDoneMessage, 0, 0);
                    return;
                }
                compiler = discovery.compiler.wstring();
            }
            std::wstring command = quoteArgument(compiler);
            command += L" -std=" + standardName;
            if (action == 1) {
                command += L" -E";
                if (!keepLines) command += L" -P";
                if (keepCommentsOption) command += L" -C";
            }
            for (const auto& dir : includes) {
                command += L" -I " + quoteArgument(dir.wstring());
            }
            for (const auto& define : defines) {
                command += L" -D " + quoteArgument(utf8ToWide(define.text));
            }
            const std::filesystem::path outputFile(outputPath);
            const std::filesystem::path workingDir = outputFile.parent_path();
            if (action == 1) {
                if (inputPath.empty()) {
                    auxStatus = "失败：没有源文件";
                    auxText = "请先选择源文件。";
                    PostMessageW(window, kAuxDoneMessage, 0, 0);
                    return;
                }
                std::filesystem::path temp = std::filesystem::temp_directory_path() /
                    (L"cruciblecpp-gcc-e-" + std::to_wstring(GetCurrentProcessId()) + L".i");
                command += L" " + quoteArgument(inputPath) + L" -o " + quoteArgument(temp.wstring());
                const ProcessResult result = runProcess(command, workingDir);
                std::error_code removeError;
                bool truncated = false;
                const std::string gccOutput = readFile(temp, truncated);
                std::filesystem::remove(temp, removeError);
                if (!result.started) {
                    auxStatus = "失败：无法启动 g++";
                    auxText = "无法启动 GCC 进程。\n";
                } else if (result.exitCode != 0) {
                    auxStatus = "g++ -E 失败";
                    auxText = "g++ -E 退出码: " + std::to_string(result.exitCode) + "\n\n" +
                              result.text;
                } else {
                    bool ownTruncated = false;
                    const std::string ownOutput = readFile(outputPath, ownTruncated);
                    auxStatus = "对比完成";
                    auxText = "CrucibleCpp vs g++ -E\n";
                    auxText += "GCC: " + wideToUtf8(compiler) + "\n\n";
                    auxText += comparePreprocessed(ownOutput, gccOutput);
                    if (truncated || ownTruncated) {
                        auxText += "\n注意：文件超过显示上限，只比较了前 8 MiB。\n";
                    }
                }
            } else {
                std::filesystem::path exePath(outputPath);
                exePath.replace_extension(L".exe");
                // .i is "preprocessed C" to GCC; force C++ and suppress
                // warnings from included system headers, which are no longer
                // marked as system headers in a preprocessed file.
                command += L" -x c++ -w " + quoteArgument(outputPath) + L" -o " +
                           quoteArgument(exePath.wstring());
                const ProcessResult result = runProcess(command, workingDir);
                if (!result.started) {
                    auxStatus = "失败：无法启动 g++";
                    auxText = "无法启动 GCC 进程。\n";
                } else if (result.exitCode != 0) {
                    auxStatus = "编译失败";
                    auxText = "g++ 退出码: " + std::to_string(result.exitCode) + "\n\n" +
                              result.text;
                } else {
                    auxStatus = "编译成功";
                    auxText = "编译成功。\n\n输出: " + wideToUtf8(exePath.wstring()) + "\n";
                    if (!result.text.empty()) auxText += "\n" + result.text;
                }
            }
            PostMessageW(window, kAuxDoneMessage, 0, 0);
        });
    }

    void finishAux() {
        if (auxWorker.joinable()) auxWorker.join();
        setRunning(false);
        setText(diagView, editMultiline(utf8ToWide(auxText)).c_str());
        setText(status, utf8ToWide(auxStatus).c_str());
        auxAction = 0;
    }

    void startRun() {
        if (running) return;
        const std::wstring inputPath = selectedInput.empty() ? textOf(input) : selectedInput;
        const std::wstring outputPath = selectedOutput.empty() ? textOf(output) : selectedOutput;
        if (inputPath.empty() || outputPath.empty()) {
            MessageBoxW(window, L"请先选择目录", L"CrucibleCpp GUI", MB_ICONWARNING);
            return;
        }
        cancelRequested.store(false);
        setRunning(true);
        resultOutput.clear();
        resultDiagnostics.clear();
        setText(outputView, L"");
        setText(diagView, L"");
        std::wstring gccPath = selectedGcc.empty() ? textOf(gcc) : selectedGcc;
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
        setText(outputView, editMultiline(utf8ToWide(resultOutput)).c_str());
        setText(diagView, editMultiline(utf8ToWide(resultDiagnostics)).c_str());
        std::wstring statusText = utf8ToWide(resultStatus);
        if (!resultDiagnostics.empty()) {
            statusText += L"（诊断 " + std::to_wstring(
                std::count(resultDiagnostics.begin(), resultDiagnostics.end(), '\n')) + L" 条）";
        }
        setText(status, statusText);
    }

    void paint(HDC dc) {
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, backgroundBrush);

        RECT header{0, 0, client.right, 76};
        HBRUSH headerBrush = CreateSolidBrush(kHeader);
        FillRect(dc, &header, headerBrush);
        DeleteObject(headerBrush);

        SetBkMode(dc, TRANSPARENT);
        HBRUSH markBrush = CreateSolidBrush(kAccent);
        HPEN noPen = CreatePen(PS_NULL, 0, kAccent);
        HGDIOBJ oldBrush = SelectObject(dc, markBrush);
        HGDIOBJ oldPen = SelectObject(dc, noPen);
        RoundRect(dc, 22, 20, 58, 56, 12, 12);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(noPen);
        DeleteObject(markBrush);
        SelectObject(dc, titleFont);
        SetTextColor(dc, RGB(235, 255, 251));
        RECT markText{22, 14, 58, 56};
        DrawTextW(dc, L"C", -1, &markText,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        SelectObject(dc, titleFont);
        SetTextColor(dc, RGB(235, 255, 251));
        RECT title{70, 14, client.right - 20, 44};
        DrawTextW(dc, L"CrucibleCpp", -1, &title,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(dc, subtitleFont);
        SetTextColor(dc, RGB(143, 211, 201));
        RECT subtitle{72, 44, client.right - 20, 66};
        DrawTextW(dc, L"GCC 13 / 14 / 15  ·  Windows x64  ·  v3.1.0", -1,
                  &subtitle, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        auto drawPanel = [&](const RECT& rect) {
            if (rect.right <= rect.left || rect.bottom <= rect.top) return;
            HBRUSH brush = CreateSolidBrush(kPanel);
            HPEN pen = CreatePen(PS_SOLID, 1, kBorder);
            HGDIOBJ oldB = SelectObject(dc, brush);
            HGDIOBJ oldP = SelectObject(dc, pen);
            RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, 14, 14);
            SelectObject(dc, oldB);
            SelectObject(dc, oldP);
            DeleteObject(brush);
            DeleteObject(pen);
        };
        drawPanel(formRect);
        drawPanel(resultRect);

        SelectObject(dc, uiFont);
        SetTextColor(dc, kMuted);
        DrawTextW(dc, L"输出 .i", -1, &outputLabelRect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        DrawTextW(dc, L"诊断", -1, &diagLabelRect,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }

    std::vector<HWND> ownerDrawButtons() const {
        return {browseInput, browseOutput, browseGcc, detectGcc, run, cancel,
                options, copyCli, compareGcc, compileOutput};
    }

    HWND buttonUnderCursor() const {
        POINT point{};
        GetCursorPos(&point);
        HWND under = WindowFromPoint(point);
        if (!under || GetParent(under) != window) return nullptr;
        const auto buttons = ownerDrawButtons();
        for (HWND button : buttons) {
            if (button == under && IsWindowEnabled(button)) return under;
        }
        return nullptr;
    }

    void pollHoverStart() {
        if (hoverTimerRunning) return;
        if (buttonUnderCursor()) {
            SetTimer(window, 2, 16, nullptr);
            hoverTimerRunning = true;
        }
    }

    void animateHover() {
        const HWND under = buttonUnderCursor();
        bool active = false;
        for (HWND button : ownerDrawButtons()) {
            auto found = hoverProgress.find(button);
            float current = found == hoverProgress.end() ? 0.0f : found->second;
            const float target = button == under ? 1.0f : 0.0f;
            float next = current + (target - current) * 0.30f;
            if (std::abs(target - next) < 0.02f) next = target;
            if (std::abs(next - current) > 0.001f) {
                hoverProgress[button] = next;
                InvalidateRect(button, nullptr, TRUE);
                active = true;
            }
        }
        if (!active && !under) {
            KillTimer(window, 2);
            hoverTimerRunning = false;
        }
    }

    void drawButton(const DRAWITEMSTRUCT* item) {
        if (!item) return;
        const int id = static_cast<int>(item->CtlID);
        const bool primary = id == IDC_RUN;
        const bool danger = id == IDC_CANCEL;
        const bool selected = (item->itemState & ODS_SELECTED) != 0;
        const bool disabled = (item->itemState & ODS_DISABLED) != 0;
        float hover = 0.0f;
        if (const auto found = hoverProgress.find(item->hwndItem);
            found != hoverProgress.end()) {
            hover = found->second;
        }
        COLORREF fill = primary ? kAccent : kPanel;
        COLORREF border = primary ? kAccent : kBorder;
        COLORREF textColor = primary ? RGB(255, 255, 255) : kText;
        if (danger) {
            fill = kPanel;
            border = RGB(220, 180, 180);
            textColor = kDanger;
        }
        if (hover > 0.0f && !disabled) {
            if (primary) {
                fill = blendColor(kAccent, RGB(24, 164, 151), hover);
                border = blendColor(kAccent, RGB(24, 164, 151), hover);
            } else if (danger) {
                fill = blendColor(kPanel, RGB(255, 238, 238), hover);
                border = blendColor(RGB(220, 180, 180), kDanger, hover);
            } else {
                fill = blendColor(kPanel, kAccentSoft, hover);
                border = blendColor(kBorder, kAccent, hover);
                textColor = blendColor(kText, RGB(5, 96, 90), hover);
            }
        }
        if (selected) {
            fill = primary ? RGB(6, 104, 98) : kAccentSoft;
        }
        if (disabled) {
            fill = RGB(239, 245, 243);
            border = RGB(215, 229, 225);
            textColor = RGB(150, 170, 166);
        }
        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ oldB = SelectObject(item->hDC, brush);
        HGDIOBJ oldP = SelectObject(item->hDC, pen);
        RoundRect(item->hDC, item->rcItem.left, item->rcItem.top,
                  item->rcItem.right, item->rcItem.bottom, 9, 9);
        SelectObject(item->hDC, oldB);
        SelectObject(item->hDC, oldP);
        DeleteObject(brush);
        DeleteObject(pen);

        wchar_t text[128]{};
        GetWindowTextW(item->hwndItem, text, 128);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, textColor);
        HFONT font = uiFont;
        HGDIOBJ oldFont = SelectObject(item->hDC, font);
        RECT textRect = item->rcItem;
        textRect.left += 6;
        textRect.right -= 6;
        DrawTextW(item->hDC, text, -1, &textRect,
                  DT_CENTER | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        SelectObject(item->hDC, oldFont);
    }

    LRESULT handle(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            createControls();
            loadSettings();
            layoutForWindow();
            return 0;
        case WM_TIMER:
            if (wParam == 1) {
                RECT current{};
                GetClientRect(window, &current);
                const int cw = current.right - current.left;
                const int ch = current.bottom - current.top;
                if (cw != lastClientWidth || ch != lastClientHeight) {
                    layoutForWindow();
                    InvalidateRect(window, nullptr, TRUE);
                }
                pollHoverStart();
            } else if (wParam == 2) {
                animateHover();
            }
            return 0;
        case WM_SIZE:
            PostMessageW(window, kLayoutMessage, 0, 0);
            return 0;
        case kLayoutMessage:
            layoutForWindow();
            InvalidateRect(window, nullptr, TRUE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paintStruct{};
            HDC dc = BeginPaint(window, &paintStruct);
            paint(dc);
            EndPaint(window, &paintStruct);
            return 0;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            HWND control = reinterpret_cast<HWND>(lParam);
            if (control == outputView || control == diagView) {
                // Read-only EDIT controls use WM_CTLCOLORSTATIC, not
                // WM_CTLCOLOREDIT. Returning NULL_BRUSH here makes their
                // background transparent and leaves scroll trails.
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, kPanel);
                SetTextColor(dc, kText);
                return reinterpret_cast<LRESULT>(panelBrush);
            }
            SetBkMode(dc, OPAQUE);
            SetBkColor(dc, control == status ? kBackground : kPanel);
            SetTextColor(dc, kMuted);
            return reinterpret_cast<LRESULT>(control == status ? backgroundBrush : panelBrush);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kPanel);
            return reinterpret_cast<LRESULT>(panelBrush);
        }
        case WM_DRAWITEM:
            drawButton(reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
            return TRUE;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case IDC_BROWSE_INPUT: browseInputFile(); return 0;
            case IDC_BROWSE_OUTPUT: browseOutputFile(); return 0;
            case IDC_BROWSE_GCC: browseGccFile(); return 0;
            case IDC_DETECT_GCC: detectGccPath(); return 0;
            case IDC_RUN: startRun(); return 0;
            case IDC_CANCEL: cancelRequested.store(true); setText(status, L"正在取消…"); return 0;
            case IDC_OPTIONS: showOptions(); return 0;
            case IDC_COMPARE_GCC: startAux(1); return 0;
            case IDC_COMPILE_OUTPUT: startAux(2); return 0;
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
        case kAuxDoneMessage:
            finishAux();
            return 0;
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            wchar_t file[32768]{};
            if (DragQueryFileW(drop, 0, file, static_cast<UINT>(std::size(file))) > 0) {
                selectedInput = file;
                setText(input, selectedInput);
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
            KillTimer(window, 1);
            KillTimer(window, 2);
            saveSettings();
            if (uiFont) DeleteObject(uiFont);
            if (titleFont) DeleteObject(titleFont);
            if (subtitleFont) DeleteObject(subtitleFont);
            if (codeFont) DeleteObject(codeFont);
            if (backgroundBrush) DeleteObject(backgroundBrush);
            if (panelBrush) DeleteObject(panelBrush);
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
        const DWORD buttonStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW;
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
        windowClass.hbrBackground = app->backgroundBrush;
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

struct AgreementState {
    AppState* app = nullptr;
    HWND window = nullptr;
    HWND title = nullptr;
    HWND edit = nullptr;
    HWND note = nullptr;
    HWND ok = nullptr;
    bool accepted = false;
};

LRESULT CALLBACK AgreementProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AgreementState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = static_cast<AgreementState*>(create->lpCreateParams);
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
                                ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL |
                                ES_NOHIDESEL;
        state->title = CreateWindowExW(0, L"STATIC", L"CrucibleCpp 用户协议",
                                       labelStyle, 20, 16, 700, 44, window, nullptr,
                                       state->app->instance, nullptr);
        state->edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                      editMultiline(kAgreementText).c_str(),
                                      editStyle, 20, 72, 720, 430, window,
                                      reinterpret_cast<HMENU>(1001), state->app->instance,
                                      nullptr);
        state->note = CreateWindowExW(0, L"STATIC",
                                      L"点击“确定”后继续使用；关闭窗口将退出。",
                                      labelStyle, 20, 524, 540, 24, window, nullptr,
                                      state->app->instance, nullptr);
        state->ok = CreateWindowExW(0, L"BUTTON", L"确定",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    620, 516, 120, 32, window,
                                    reinterpret_cast<HMENU>(IDOK), state->app->instance,
                                    nullptr);
        SendMessageW(state->title, WM_SETFONT,
                     reinterpret_cast<WPARAM>(state->app->titleFont), TRUE);
        SendMessageW(state->edit, WM_SETFONT,
                     reinterpret_cast<WPARAM>(state->app->uiFont), TRUE);
        SendMessageW(state->note, WM_SETFONT,
                     reinterpret_cast<WPARAM>(state->app->uiFont), TRUE);
        SendMessageW(state->ok, WM_SETFONT,
                     reinterpret_cast<WPARAM>(state->app->uiFont), TRUE);
        RECT client{};
        GetClientRect(window, &client);
        SendMessageW(window, WM_SIZE, 0,
                     MAKELPARAM(client.right - client.left, client.bottom - client.top));
        SetFocus(state->edit);
        return 0;
    }
    case WM_SIZE: {
        if (!state->title) return 0;
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        if (width <= 0 || height <= 0) return 0;
        MoveWindow(state->title, 24, 16, width - 48, 44, TRUE);
        const int noteHeight = 24;
        const int buttonTop = height - 54;
        MoveWindow(state->note, 24, buttonTop, std::max(100, width - 190),
                   noteHeight, TRUE);
        MoveWindow(state->ok, width - 152, buttonTop - 4, 128, 34, TRUE);
        MoveWindow(state->edit, 24, 70, width - 48,
                   std::max(80, buttonTop - 86), TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            state->accepted = true;
            state->app->putIni(L"agreement", L"1");
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
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool showAgreementDialog(AppState* app) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = AgreementProc;
        windowClass.hInstance = app->instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = L"CrucibleCppAgreementWindow";
        if (!RegisterClassExW(&windowClass)) return false;
        registered = true;
    }
    AgreementState state;
    state.app = app;
    RECT parent{};
    GetWindowRect(app->window, &parent);
    const int width = 820;
    const int height = 650;
    EnableWindow(app->window, FALSE);
    HWND dialog = CreateWindowExW(
        WS_EX_DLGMODALFRAME, L"CrucibleCppAgreementWindow",
        L"CrucibleCpp 用户协议",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        parent.left + 40, parent.top + 40, width, height,
        app->window, nullptr, app->instance, &state);
    if (!dialog) {
        EnableWindow(app->window, TRUE);
        return false;
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
    return state.accepted;
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
    // Prefer per-monitor v2; otherwise Windows virtualizes GetClientRect on
    // high-DPI displays and the layout no longer matches the real window.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetContextFn = BOOL(WINAPI*)(HANDLE);
        auto setContext = reinterpret_cast<SetContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext) {
            setContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)));
        }
    }
    SetProcessDPIAware();

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&controls);

    AppState state;
    state.instance = instance;
    state.recordLaunch();
    state.uiFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    state.titleFont = CreateFontW(-28, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    state.subtitleFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    state.codeFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Consolas");
    state.backgroundBrush = CreateSolidBrush(kBackground);
    state.panelBrush = CreateSolidBrush(kPanel);

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = state.backgroundBrush;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hIconSm = windowClass.hIcon;
    if (!RegisterClassExW(&windowClass)) return 1;

    HWND window = CreateWindowExW(
        WS_EX_COMPOSITED, kWindowClass, L"CrucibleCpp GUI",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 1100, 760, nullptr, nullptr, instance, &state);
    if (!window) return 1;
    BOOL darkTitle = TRUE;
    DwmSetWindowAttribute(window, 20, &darkTitle, sizeof(darkTitle));
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const bool tooLarge = (work.right - work.left) < 1100 ||
                          (work.bottom - work.top) < 760;
    DragAcceptFiles(window, TRUE);
    ShowWindow(window, SW_SHOWNORMAL);
    if (tooLarge) {
        // Never create a window larger than the usable desktop: the lower
        // output/diagnostics panes would otherwise be clipped off-screen.
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        const int targetWidth = std::max(480, static_cast<int>(workArea.right - workArea.left));
        const int targetHeight = std::max(360, static_cast<int>(workArea.bottom - workArea.top));
        SetWindowPos(window, nullptr, workArea.left, workArea.top,
                     targetWidth, targetHeight, SWP_NOZORDER);
    }
    UpdateWindow(window);
    SetTimer(window, 1, 100, nullptr);
    // The first WM_SIZE can arrive before the child controls exist. Force one
    // final layout against the real client rectangle after the window is shown.
    RECT client{};
    GetClientRect(window, &client);
    SendMessageW(window, WM_SIZE, 0,
                 MAKELPARAM(client.right - client.left, client.bottom - client.top));

    if (!state.agreementAccepted) {
        if (!showAgreementDialog(&state)) {
            DestroyWindow(window);
            return 0;
        }
    }

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








