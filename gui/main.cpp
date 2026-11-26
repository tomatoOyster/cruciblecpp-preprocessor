#include "GccDiscovery.h"
#include "Preprocessor.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <winhttp.h>
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
constexpr UINT kAnnouncementMessage = WM_APP + 4;
constexpr int kMaxDisplayBytes = 8 * 1024 * 1024;
constexpr wchar_t kAnnouncementHost[] = L"hfsdr.cn";
constexpr wchar_t kAnnouncementPath[] = L"/announcement.ini";

COLORREF kBackground = RGB(244, 251, 249);
COLORREF kPanel = RGB(255, 255, 255);
COLORREF kOutputBackground = RGB(255, 255, 255);
COLORREF kDiagBackground = RGB(255, 255, 255);
COLORREF kBanner = RGB(255, 255, 255);
COLORREF kHeader = RGB(7, 59, 58);
COLORREF kAccent = RGB(8, 127, 120);
COLORREF kAccentSoft = RGB(224, 247, 243);
COLORREF kText = RGB(7, 59, 58);
COLORREF kMuted = RGB(76, 112, 109);
COLORREF kBorder = RGB(204, 226, 222);
COLORREF kDanger = RGB(176, 58, 58);

std::wstring colorToHex(COLORREF color) {
    wchar_t buffer[16]{};
    swprintf_s(buffer, L"#%02X%02X%02X",
               GetRValue(color), GetGValue(color), GetBValue(color));
    return buffer;
}

COLORREF colorFromHex(const std::wstring& value, COLORREF fallback) {
    if (value.size() != 7 || value[0] != L'#') return fallback;
    wchar_t* end = nullptr;
    const unsigned long parsed = wcstoul(value.c_str() + 1, &end, 16);
    if (!end || *end != L'\0') return fallback;
    return RGB((parsed >> 16) & 0xFF, (parsed >> 8) & 0xFF, parsed & 0xFF);
}

COLORREF hsvToRgb(int hue, int saturation, int value) {
    hue = ((hue % 360) + 360) % 360;
    saturation = std::clamp(saturation, 0, 100);
    value = std::clamp(value, 0, 100);
    const int region = hue / 60;
    const int remainder = hue % 60;
    const int p = value * (100 - saturation) / 100;
    const int q = value * (100 - saturation * remainder / 60) / 100;
    const int t = value * (100 - saturation * (60 - remainder) / 60) / 100;
    int r = 0, g = 0, b = 0;
    switch (region) {
    case 0: r = value; g = t; b = p; break;
    case 1: r = q; g = value; b = p; break;
    case 2: r = p; g = value; b = t; break;
    case 3: r = p; g = q; b = value; break;
    case 4: r = t; g = p; b = value; break;
    default: r = value; g = p; b = q; break;
    }
    return RGB(r * 255 / 100, g * 255 / 100, b * 255 / 100);
}

void rgbToHsv(COLORREF color, int& hue, int& saturation, int& value) {
    const int r = GetRValue(color);
    const int g = GetGValue(color);
    const int b = GetBValue(color);
    const int maximum = std::max({r, g, b});
    const int minimum = std::min({r, g, b});
    const int delta = maximum - minimum;
    value = maximum * 100 / 255;
    saturation = maximum == 0 ? 0 : delta * 100 / maximum;
    if (delta == 0) { hue = 0; return; }
    int result = 0;
    if (maximum == r) result = 60 * (g - b) / delta;
    else if (maximum == g) result = 120 + 60 * (b - r) / delta;
    else result = 240 + 60 * (r - g) / delta;
    if (result < 0) result += 360;
    hue = result;
}

struct ThemeColorDef {
    const wchar_t* key;
    const wchar_t* label;
    COLORREF* value;
};

std::vector<ThemeColorDef> themeColorDefs() {
    return {
        {L"colorBackground", L"窗口背景", &kBackground},
        {L"colorHeader", L"顶部栏", &kHeader},
        {L"colorPanel", L"面板", &kPanel},
        {L"colorBanner", L"公告卡片", &kBanner},
        {L"colorText", L"正文", &kText},
        {L"colorMuted", L"次要文字", &kMuted},
        {L"colorAccent", L"强调色", &kAccent},
        {L"colorAccentSoft", L"浅强调", &kAccentSoft},
        {L"colorBorder", L"边框", &kBorder},
        {L"colorDanger", L"取消/危险", &kDanger},
        {L"colorOutput", L"输出背景", &kOutputBackground},
        {L"colorDiagnostics", L"诊断背景", &kDiagBackground},
    };
}
constexpr wchar_t kAgreementText[] = LR"AGREEMENT(CrucibleCpp 用户协议

协议版本：2.0
生效日期：2026 年 11 月 26 日
开发者：BA3RZL
适用范围：CrucibleCpp 官方 CLI、Windows GUI、源代码、文档及发行文件

1. 协议适用与接受

本协议适用于 CrucibleCpp 的命令行版本、Windows GUI、源代码、文档、压缩包及相关发行文件（以下统称“本软件”）。

下载、安装、复制、运行或以其他方式使用本软件，即表示你已阅读、理解并同意本协议。

如果你不同意本协议，请停止使用并删除本软件。

本协议是使用官方发行版的补充约定。本软件不同版本同时受其对应开源许可证约束；如本协议与该许可证存在冲突，以对应许可证为准。

2. 定义

开发者：BA3RZL，CrucibleCpp 项目维护者。
官方渠道：CrucibleCpp 的 GitHub 仓库、GitHub Releases 和 hfsdr.cn 官网。
用户：下载、安装、复制或使用本软件的个人或组织。
商业使用：以营利、获取商业利益或商业运营为目的的使用，包括销售、付费分发、商业产品集成、付费服务、商业内部生产流程等。
官方发行版：由开发者通过官方渠道发布的二进制、压缩包、源代码、文档和校验文件。

3. 许可证与版本

V3.0.0 及以后：采用 CC BY-NC 4.0。
V2.9.0 及以前：继续采用 MIT License。
旧版本不会因本协议或新版本发布而被追溯变更许可证。

不同版本的许可证文本以发行包中的 LICENSE、LICENSE-MIT 和 NOTICE 为准。

如果本协议与 CC BY-NC 4.0 或 MIT 许可证发生冲突，以对应许可证为准；本协议不减少许可证已经授予你的权利。

4. 允许的使用

在遵守对应许可证和本协议的前提下，你可以：

1. 个人学习、研究、开发和测试；
2. 在非商业项目中使用；
3. 复制、修改源代码，但必须保留版权、许可证和免责声明；
4. 在非商业范围内再分发原始或修改版本，并附上对应许可证；
5. 将本软件用于开源项目、教学、竞赛、学术研究等非商业场景；
6. 查看、修改和分发 GUI/CLI 源代码，但不得移除许可证和署名信息。

再分发时，不得暗示开发者 BA3RZL 对你的修改版本提供官方背书或支持。

5. 商业使用与商业授权

未经开发者书面授权，不得将本软件用于商业用途，包括但不限于：

销售本软件或其修改版本；
将本软件打包进商业软件、硬件或服务；
提供基于本软件的付费预处理、编译、咨询或托管服务；
在企业商业生产流程中直接使用；
以本软件为基础制作商业培训、商业课程或商业发行版。

商业使用需要单独取得商业授权。商业授权范围、费用和支持方式由双方另行约定。

如果你不确定某项使用是否属于商业使用，请先联系开发者确认：

yywxdm@outlook.com
3482490231@qq.com

6. 使用限制

使用本软件时，你不得：

1. 移除、隐藏或篡改版权声明、许可证、免责声明或作者标识；
2. 将本软件用于违法、侵权、恶意软件或其他危害他人的活动；
3. 冒充官方版本，或声称修改版本由开发者官方维护；
4. 使用 CrucibleCpp 名称、标识或开发者 BA3RZL 名义进行误导性宣传；
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

本软件不随包分发 GCC。你需要自行取得合法安装的 GCC，并遵守其许可证。

静态发行版不再依赖 MinGW 的 libstdc++-6.dll、libgcc_s_seh-1.dll 和 libwinpthread-1.dll，但仍依赖 Windows 系统 DLL 和 UCRT 系统组件。开发者不对第三方工具链、系统组件或运行环境的行为作出保证。

9. 本地数据与隐私

当前版本不包含账号系统、遥测或代码上传功能。CLI 和 GUI 在本机执行预处理，GUI 调用本机 GCC 完成预处理、对比和编译。

GUI 可能在本地保存：

用户协议同意状态；
GCC 路径、C++ 标准、PCH、行标记、注释等设置；
Include 目录和宏定义；
GUI 启动次数。

启动次数当前只记录、不参与业务逻辑，写入：

%APPDATA%\CrucibleCpp\launch-count.ini

这些数据不会由本软件主动上传。你可以自行删除 %APPDATA%\CrucibleCpp 下的配置文件。

如果未来版本加入网络功能，开发者会通过协议更新或单独说明告知数据处理方式。

10. 协议变更与版本更新

开发者可能随版本更新修改本协议。更新后的协议会在官方仓库或官网发布，并标注新的协议版本和生效日期。

新版本协议发布后，继续使用相应版本的本软件，视为接受更新后的协议；如果你不接受，应停止使用并删除相关副本。

11. 无担保

本软件按“现状”提供，不附带任何明示或默示担保，包括但不限于：

适销性；
特定用途适用性；
不侵权；
无错误或无中断运行；
与特定 GCC 版本、项目、操作系统或构建系统完全兼容。

你需要自行评估本软件是否适合你的项目，并自行备份代码和构建产物。

12. 责任限制

在适用法律允许的最大范围内，开发者 BA3RZL 不对因使用或无法使用本软件而产生的任何直接、间接、偶然、特殊或后果性损失承担责任，包括但不限于：

数据丢失；
构建失败；
项目延期；
利润损失；
第三方索赔；
因 GCC、系统组件或用户代码导致的问题。

本软件通常免费提供；在适用法律允许的范围内，开发者的累计责任不超过你为官方版本实际支付的费用。

13. 终止

如果你违反本协议，开发者可以终止你使用官方发行版和官方服务的许可。终止后，你应停止使用并删除相关副本。

本协议终止后，第 7、8、11、12 和 14 条继续有效。

14. 其他

本协议部分条款被认定无效或不可执行时，不影响其他条款的效力。
本协议与对应开源许可证共同构成你对官方发行版使用的完整约定。
本协议不构成对开发者商标、名称或标识的额外授权。
如果你需要商业授权、二次分发许可或其他例外安排，请通过以下方式联系开发者。

15. 联系方式

开发者：BA3RZL
邮箱：yywxdm@outlook.com
备用邮箱：3482490231@qq.com
项目仓库：https://github.com/tomatoOyster/cruciblecpp-preprocessor
官网：https://hfsdr.cn
问题反馈：GitHub Issues

Copyright © 2026 BA3RZL.
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
    IDC_SETTINGS,
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

struct Announcement {
    bool enabled = false;
    std::string id;
    std::string title;
    std::string body;
    std::string link;
    std::string linkText;
};

std::string trimAscii(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string unescapeAscii(std::string value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            const char next = value[i + 1];
            if (next == 'n') { result.push_back('\n'); ++i; continue; }
            if (next == 'r') { result.push_back('\r'); ++i; continue; }
            if (next == '\\') { result.push_back('\\'); ++i; continue; }
        }
        result.push_back(value[i]);
    }
    return result;
}

Announcement parseAnnouncement(const std::string& text) {
    Announcement announcement;
    std::string line;
    auto consume = [&](const std::string& raw) {
        const std::string value = trimAscii(raw);
        if (value.empty() || value[0] == '#' || value[0] == ';') return;
        const auto equals = value.find('=');
        if (equals == std::string::npos) return;
        std::string key = trimAscii(value.substr(0, equals));
        std::string data = unescapeAscii(trimAscii(value.substr(equals + 1)));
        for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (key == "enabled") {
            announcement.enabled = data == "1" || data == "true" || data == "yes";
        } else if (key == "id") {
            announcement.id = data;
        } else if (key == "title") {
            announcement.title = data;
        } else if (key == "body") {
            announcement.body = data;
        } else if (key == "link") {
            announcement.link = data;
        } else if (key == "link_text" || key == "linktext") {
            announcement.linkText = data;
        }
    };
    for (char c : text) {
        if (c == '\n') { consume(line); line.clear(); }
        else line.push_back(c);
    }
    consume(line);
    if (announcement.linkText.empty()) announcement.linkText = "查看详情";
    return announcement;
}

std::string fetchAnnouncementText() {
    std::string result;
    HINTERNET session = WinHttpOpen(L"CrucibleCpp/3.1.1",
                                    WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return result;
    WinHttpSetTimeouts(session, 4000, 4000, 4000, 8000);
    HINTERNET connection = WinHttpConnect(session, kAnnouncementHost,
                                          INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connection) { WinHttpCloseHandle(session); return result; }
    std::wstring path = kAnnouncementPath;
    path += L"?ts=" + std::to_wstring(GetTickCount64());
    HINTERNET request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        return result;
    }
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect));
    if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD status = 0;
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                            WINHTTP_NO_HEADER_INDEX);
        if (status == 200) {
            char buffer[4096];
            DWORD read = 0;
            while (WinHttpReadData(request, buffer, sizeof(buffer), &read) && read > 0) {
                result.append(buffer, read);
                if (result.size() > 128 * 1024) break;
            }
        }
    }
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
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

struct ColorRegion {
    int colorIndex = 0;
    RECT rect{};
    const wchar_t* label = L"";
};

struct AppState;
void startColorPickMode(AppState* app);
void stopColorPickMode(AppState* app);
void updateColorPickHover(AppState* app, POINT point);
void selectColorRegion(AppState* app, POINT point, bool ctrl);
void paintColorOverlay(AppState* app, HDC dc);
void syncPickerFromSelection(AppState* app);
void applyPickerColor(AppState* app);
bool scheduleCacheReset(std::wstring& errorText);
LRESULT CALLBACK ColorOverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

struct SettingsState;

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
    HWND settingsButton = nullptr;
    HWND outputView = nullptr;
    HWND diagView = nullptr;
    HWND announcementBanner = nullptr;
    HWND status = nullptr;
    HWND labels[4]{};
    HWND outputLabel = nullptr;
    HWND diagLabel = nullptr;
    HFONT uiFont = nullptr;
    HFONT titleFont = nullptr;
    HFONT subtitleFont = nullptr;
    HFONT announcementTitleFont = nullptr;
    HFONT announcementBodyFont = nullptr;
    HFONT announcementCloseFont = nullptr;
    HFONT codeFont = nullptr;
    HBRUSH backgroundBrush = nullptr;
    HBRUSH panelBrush = nullptr;
    HBRUSH outputBackgroundBrush = nullptr;
    HBRUSH diagBackgroundBrush = nullptr;
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
    bool alwaysShowAnnouncement = true;
    bool enableCompareGcc = true;
    bool skipSaveOnExit = false;
    SettingsState* settingsState = nullptr;
    HWND colorOverlay = nullptr;
    bool colorPickMode = false;
    int colorHover = -1;
    std::vector<int> colorSelection;
    COLORREF pickerColor = kAccent;
    int pickerHue = 174;
    int pickerSat = 95;
    int pickerVal = 100;
    HBITMAP wheelBitmap = nullptr;
    HDC wheelMem = nullptr;
    void* wheelBits = nullptr;
    HGDIOBJ wheelOldBitmap = nullptr;
    int wheelW = 0;
    int wheelH = 0;
    int wheelCachedVal = -1;
    int wheelBarHue = -1;
    int wheelBarSat = -1;
    bool running = false;
    int exitCode = 0;
    std::string resultOutput;
    std::string resultDiagnostics;
    std::string resultStatus;
    Announcement announcement;
    std::thread announcementWorker;
    std::wstring announcementLink;
    std::wstring announcementLinkText;
    int announcementHeight = 0;
    bool announcementVisible = false;
    int uiFontSize = 13;
    int codeFontSize = 15;
    int letterSpacing = 0;

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
        settingsButton = CreateWindowExW(0, L"BUTTON", L"设置", buttonStyle,
                                         0, 0, 10, 10, window,
                                         reinterpret_cast<HMENU>(IDC_SETTINGS), instance, nullptr);
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
        announcementBanner = CreateWindowExW(0, L"CrucibleCppAnnouncementBanner", L"",
                                             WS_CHILD | WS_CLIPSIBLINGS,
                                             0, 0, 10, 10, window, nullptr,
                                             instance, this);
        ShowWindow(announcementBanner, SW_HIDE);
        colorOverlay = nullptr;
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
        for (HWND control : {browseInput, browseOutput, browseGcc, detectGcc, run, cancel, options, copyCli, compareGcc, compileOutput, settingsButton}) {
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

    void applyThemeColors() {
        if (backgroundBrush) DeleteObject(backgroundBrush);
        if (panelBrush) DeleteObject(panelBrush);
        if (outputBackgroundBrush) DeleteObject(outputBackgroundBrush);
        if (diagBackgroundBrush) DeleteObject(diagBackgroundBrush);
        backgroundBrush = CreateSolidBrush(kBackground);
        panelBrush = CreateSolidBrush(kPanel);
        outputBackgroundBrush = CreateSolidBrush(kOutputBackground);
        diagBackgroundBrush = CreateSolidBrush(kDiagBackground);
        SetClassLongPtrW(window, GCLP_HBRBACKGROUND,
                         reinterpret_cast<LONG_PTR>(backgroundBrush));
        InvalidateRect(window, nullptr, TRUE);
        if (announcementBanner) InvalidateRect(announcementBanner, nullptr, TRUE);
    }

    void rebuildFonts() {
        auto replaceFont = [](HFONT& font, int size, int weight, const wchar_t* face) {
            if (font) DeleteObject(font);
            font = CreateFontW(-size, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH | FF_SWISS, face);
        };
        replaceFont(uiFont, uiFontSize, FW_NORMAL, L"Segoe UI");
        replaceFont(titleFont, uiFontSize + 15, FW_BOLD, L"Segoe UI");
        replaceFont(subtitleFont, std::max(10, uiFontSize - 1), FW_NORMAL, L"Segoe UI");
        replaceFont(announcementTitleFont, uiFontSize + 3, FW_BOLD, L"Segoe UI");
        replaceFont(announcementBodyFont, uiFontSize, FW_NORMAL, L"Segoe UI");
        replaceFont(announcementCloseFont, 36, FW_NORMAL, L"Segoe UI");
        if (codeFont) DeleteObject(codeFont);
        codeFont = CreateFontW(-codeFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               FIXED_PITCH | FF_MODERN, L"Consolas");
        for (HWND control : {input, browseInput, output, browseOutput, gcc,
                             browseGcc, detectGcc, standard, run, cancel, options,
                             copyCli, compareGcc, compileOutput, settingsButton,
                             outputLabel, diagLabel, status}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        }
        SendMessageW(outputView, WM_SETFONT, reinterpret_cast<WPARAM>(codeFont), TRUE);
        SendMessageW(diagView, WM_SETFONT, reinterpret_cast<WPARAM>(codeFont), TRUE);
        for (HWND label : labels) {
            SendMessageW(label, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
        }
        InvalidateRect(window, nullptr, TRUE);
    }

    void paintAnnouncement(HDC dc) {
        SetTextCharacterExtra(dc, letterSpacing);
        RECT client{};
        GetClientRect(announcementBanner, &client);
        HBRUSH background = CreateSolidBrush(kPanel);
        FillRect(dc, &client, background);
        DeleteObject(background);
        HPEN border = CreatePen(PS_SOLID, 1, kBorder);
        HBRUSH card = CreateSolidBrush(kBanner);
        HGDIOBJ oldPen = SelectObject(dc, border);
        HGDIOBJ oldBrush = SelectObject(dc, card);
        RoundRect(dc, 0, 0, client.right, client.bottom, 14, 14);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(border);
        DeleteObject(card);
        HBRUSH stripe = CreateSolidBrush(kAccent);
        RECT stripeRect{0, 0, 6, client.bottom};
        FillRect(dc, &stripeRect, stripe);
        DeleteObject(stripe);

        HBRUSH mark = CreateSolidBrush(kAccent);
        HPEN noPen = CreatePen(PS_NULL, 0, kAccent);
        oldPen = SelectObject(dc, noPen);
        oldBrush = SelectObject(dc, mark);
        Ellipse(dc, 16, 16, 46, 46);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(noPen);
        DeleteObject(mark);
        SelectObject(dc, announcementTitleFont);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        RECT markText{16, 16, 46, 46};
        DrawTextW(dc, L"i", -1, &markText, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        SetTextColor(dc, kText);
        RECT title{58, 10, client.right - 40, 34};
        DrawTextW(dc, utf8ToWide(announcement.title).c_str(), -1, &title,
                  DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, announcementBodyFont);
        SetTextColor(dc, kMuted);
        RECT body{58, 34, client.right - 26, 74};
        DrawTextW(dc, utf8ToWide(announcement.body).c_str(), -1, &body,
                  DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS);
        if (!announcementLink.empty()) {
            SetTextColor(dc, kAccent);
            RECT link{58, 76, client.right - 40, 100};
            DrawTextW(dc, announcementLinkText.c_str(), -1, &link,
                      DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
        // Large circular close affordance; drawn geometrically so it stays
        // legible at every DPI instead of depending on a glyph metric.
        const int closeX = client.right - 28;
        const int closeY = 28;
        HBRUSH closeFill = CreateSolidBrush(kAccentSoft);
        HPEN closePen = CreatePen(PS_NULL, 0, kAccentSoft);
        HGDIOBJ oldCloseBrush = SelectObject(dc, closeFill);
        HGDIOBJ oldClosePen = SelectObject(dc, closePen);
        Ellipse(dc, closeX - 19, closeY - 19, closeX + 19, closeY + 19);
        SelectObject(dc, oldCloseBrush);
        SelectObject(dc, oldClosePen);
        DeleteObject(closeFill);
        DeleteObject(closePen);
        HPEN crossPen = CreatePen(PS_SOLID, 2, kMuted);
        HGDIOBJ oldCrossPen = SelectObject(dc, crossPen);
        MoveToEx(dc, closeX - 8, closeY - 8, nullptr);
        LineTo(dc, closeX + 8, closeY + 8);
        MoveToEx(dc, closeX + 8, closeY - 8, nullptr);
        LineTo(dc, closeX - 8, closeY + 8);
        SelectObject(dc, oldCrossPen);
        DeleteObject(crossPen);
        SelectObject(dc, announcementBodyFont);
        SetTextColor(dc, kAccent);
        RECT expand{client.right - 218, 74, client.right - 50, 102};
        DrawTextW(dc, L"展开全屏", -1, &expand,
                  DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }

    void dismissAnnouncement() {
        announcementVisible = false;
        announcementHeight = 0;
        ShowWindow(announcementBanner, SW_HIDE);
        if (!announcement.id.empty()) {
            putIni(L"announcementLastSeen", utf8ToWide(announcement.id));
        }
        layoutForWindow();
        InvalidateRect(window, nullptr, TRUE);
    }

    void showAnnouncement() {
        if (announcement.title.empty() || announcement.body.empty()) return;
        announcementLink = utf8ToWide(announcement.link);
        announcementLinkText = utf8ToWide(announcement.linkText);
        announcementVisible = true;
        announcementHeight = 112;
        ShowWindow(announcementBanner, SW_SHOW);
        layoutForWindow();
        InvalidateRect(announcementBanner, nullptr, TRUE);
    }

    void clickAnnouncement(int x, int y) {
        RECT client{};
        GetClientRect(announcementBanner, &client);
        if (x >= client.right - 52 && x <= client.right - 2 && y >= 2 && y <= 48) {
            dismissAnnouncement();
            return;
        }
        if (x >= client.right - 220 && x <= client.right - 46 &&
            y >= 70 && y <= 104) {
            openAnnouncementFullscreen();
            return;
        }
        if (!announcementLink.empty() && x >= 40 && x <= client.right - 230 &&
            y >= 72 && y <= 102) {
            ShellExecuteW(window, L"open", announcementLink.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
        }
    }

    void applyDisplaySettings() {
        if (enableCompareGcc) {
            ShowWindow(compareGcc, SW_SHOW);
        } else {
            ShowWindow(compareGcc, SW_HIDE);
        }
        if (window) layoutForWindow();
    }

    void fetchAnnouncement() {
        if (announcementWorker.joinable()) return;
        announcementWorker = std::thread([this]() {
            const std::string text = fetchAnnouncementText();
            announcement = parseAnnouncement(text);
            PostMessageW(window, kAnnouncementMessage, 0, 0);
        });
    }

    void finishAnnouncement() {
        if (announcementWorker.joinable()) announcementWorker.join();
        if (!announcement.enabled || announcement.title.empty() ||
            announcement.body.empty()) {
            return;
        }
        if (!alwaysShowAnnouncement) {
            std::wstring lastSeen = getIni(L"announcementLastSeen");
            if (lastSeen.empty()) lastSeen = getIni(L"announcementDismissed");
            if (!announcement.id.empty() && lastSeen == utf8ToWide(announcement.id)) {
                return;
            }
        }
        showAnnouncement();
    }

    void paintAnnouncementFullscreen(HDC dc, HWND target) {
        SetTextCharacterExtra(dc, letterSpacing);
        RECT screen{};
        GetClientRect(target, &screen);
        FillRect(dc, &screen, backgroundBrush);
        RECT header{0, 0, screen.right, 96};
        HBRUSH headerBrush = CreateSolidBrush(kHeader);
        FillRect(dc, &header, headerBrush);
        DeleteObject(headerBrush);
        HBRUSH mark = CreateSolidBrush(kAccent);
        HPEN noPen = CreatePen(PS_NULL, 0, kAccent);
        HGDIOBJ oldBrush = SelectObject(dc, mark);
        HGDIOBJ oldPen = SelectObject(dc, noPen);
        Ellipse(dc, 48, 26, 88, 66);
        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(noPen);
        DeleteObject(mark);
        SetBkMode(dc, TRANSPARENT);
        SelectObject(dc, titleFont);
        SetTextColor(dc, RGB(235, 255, 251));
        RECT brand{104, 18, screen.right - 40, 56};
        DrawTextW(dc, L"CrucibleCpp 公告", -1, &brand,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        SelectObject(dc, subtitleFont);
        SetTextColor(dc, RGB(143, 211, 201));
        RECT sub{106, 56, screen.right - 40, 78};
        DrawTextW(dc, L"Announcement", -1, &sub,
                  DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        const int cardWidth = std::min(1200, std::max(400, static_cast<int>(screen.right) - 120));
        const int cardLeft = (screen.right - cardWidth) / 2;
        RECT card{cardLeft, 118, cardLeft + cardWidth, screen.bottom - 40};
        HBRUSH cardBrush = CreateSolidBrush(kPanel);
        HPEN border = CreatePen(PS_SOLID, 1, kBorder);
        HGDIOBJ oldB = SelectObject(dc, cardBrush);
        HGDIOBJ oldP = SelectObject(dc, border);
        RoundRect(dc, card.left, card.top, card.right, card.bottom, 16, 16);
        SelectObject(dc, oldB);
        SelectObject(dc, oldP);
        DeleteObject(cardBrush);
        DeleteObject(border);
    }

    void layout(int width, int height) {
        const int margin = 18;
        const int top = 92 + announcementHeight;
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
        MoveWindow(standard, editX, y, 108, 200, TRUE);
        auto buttonWidthFor = [&](HWND control, int minimum) {
            wchar_t text[128]{};
            GetWindowTextW(control, text, 128);
            HDC dc = GetDC(window);
            HGDIOBJ oldFont = SelectObject(dc, uiFont);
            SIZE size{};
            GetTextExtentPoint32W(dc, text, static_cast<int>(wcslen(text)), &size);
            SelectObject(dc, oldFont);
            ReleaseDC(window, dc);
            const int scaledMinimum = std::max(1, minimum * uiFontSize / 13);
            return std::max(scaledMinimum, static_cast<int>(size.cx) + 22);
        };
        struct ActionButton { HWND control; int width; };
        std::vector<ActionButton> rowOne;
        std::vector<ActionButton> rowTwo;
        rowOne.push_back({run, buttonWidthFor(run, 62)});
        rowOne.push_back({cancel, buttonWidthFor(cancel, 48)});
        if (enableCompareGcc) rowOne.push_back({compareGcc, buttonWidthFor(compareGcc, 100)});
        rowOne.push_back({compileOutput, buttonWidthFor(compileOutput, 58)});
        rowTwo.push_back({options, buttonWidthFor(options, 46)});
        rowTwo.push_back({copyCli, buttonWidthFor(copyCli, 76)});
        rowTwo.push_back({settingsButton, buttonWidthFor(settingsButton, 58)});
        const int gap = 6;
        const int actionStart = editX + 110;
        const int availableAction = std::max(100, width - actionStart - margin);
        auto totalWidth = [&](const std::vector<ActionButton>& row) {
            int total = 0;
            for (const auto& button : row) total += button.width + gap;
            return total;
        };
        const int actionRows = totalWidth(rowOne) + totalWidth(rowTwo) <= availableAction ? 1 : 2;
        int actionX = actionStart;
        auto placeRow = [&](const std::vector<ActionButton>& row, int rowY) {
            actionX = actionStart;
            for (const auto& button : row) {
                MoveWindow(button.control, actionX, rowY, button.width, 29, TRUE);
                actionX += button.width + gap;
            }
        };
        const int actionY = top + row * 3;
        if (actionRows == 1) {
            std::vector<ActionButton> all = rowOne;
            all.insert(all.end(), rowTwo.begin(), rowTwo.end());
            placeRow(all, actionY);
        } else {
            placeRow(rowOne, actionY);
            placeRow(rowTwo, actionY + row);
        }
        if (announcementVisible) {
            MoveWindow(announcementBanner, margin, 80, width - 2 * margin, 104, TRUE);
            ShowWindow(announcementBanner, SW_SHOW);
        } else {
            ShowWindow(announcementBanner, SW_HIDE);
        }
        if (colorPickMode && colorOverlay) {
            RECT mainClient{};
            GetClientRect(window, &mainClient);
            POINT origin{0, 0};
            ClientToScreen(window, &origin);
            SetWindowPos(colorOverlay, HWND_TOPMOST, origin.x, origin.y,
                         mainClient.right, mainClient.bottom, SWP_SHOWWINDOW);
        }
        const int formRows = 4 + (actionRows - 1);
        formRect = {margin - 8, top - 12, width - margin + 8, top + row * formRows + 12};
        y = top + row * formRows + 24;
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
    void showSettings();
    void openAnnouncementFullscreen();

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
        const std::wstring path = settingsPath().wstring();
        WritePrivateProfileStringW(L"gui", key, value.c_str(), path.c_str());
        // Flush the profile cache immediately. If the process is closed by
        // Task Manager or crashes, a cached write would otherwise be lost.
        WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
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
        agreementAccepted = getIni(L"agreement", L"0") == L"1" &&
                            getIni(L"agreementVersion", L"0") == L"2";
        alwaysShowAnnouncement = getIni(L"alwaysShowAnnouncement", L"1") != L"0";
        enableCompareGcc = getIni(L"enableCompareGcc", L"1") != L"0";
        uiFontSize = std::clamp(_wtoi(getIni(L"uiFontSize", L"13").c_str()), 10, 26);
        codeFontSize = std::clamp(_wtoi(getIni(L"codeFontSize", L"15").c_str()), 10, 28);
        letterSpacing = std::clamp(_wtoi(getIni(L"letterSpacing", L"0").c_str()), 0, 4);
        for (const auto& def : themeColorDefs()) {
            *def.value = colorFromHex(getIni(def.key), *def.value);
        }
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
        putIni(L"alwaysShowAnnouncement", alwaysShowAnnouncement ? L"1" : L"0");
        putIni(L"enableCompareGcc", enableCompareGcc ? L"1" : L"0");
        putIni(L"uiFontSize", std::to_wstring(uiFontSize));
        putIni(L"codeFontSize", std::to_wstring(codeFontSize));
        putIni(L"letterSpacing", std::to_wstring(letterSpacing));
        for (const auto& def : themeColorDefs()) {
            putIni(def.key, colorToHex(*def.value));
        }
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
        SetTextCharacterExtra(dc, letterSpacing);
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
        DrawTextW(dc, L"GCC 13 / 14 / 15  ·  Windows x64  ·  v3.1.1", -1,
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
        SetTextCharacterExtra(item->hDC, letterSpacing);
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
            rebuildFonts();
            applyThemeColors();
            applyDisplaySettings();
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
                const COLORREF background = control == outputView ? kOutputBackground
                                                                   : kDiagBackground;
                SetBkMode(dc, OPAQUE);
                SetBkColor(dc, background);
                SetTextColor(dc, kText);
                return reinterpret_cast<LRESULT>(control == outputView
                                                     ? outputBackgroundBrush
                                                     : diagBackgroundBrush);
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
            case IDC_COMPARE_GCC: if (enableCompareGcc) startAux(1); return 0;
            case IDC_SETTINGS: showSettings(); return 0;
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
        case kAnnouncementMessage:
            finishAnnouncement();
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
        case WM_MOUSEMOVE:
            if (colorPickMode) {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                updateColorPickHover(this, point);
                return 0;
            }
            break;
        case WM_LBUTTONDOWN:
            if (colorPickMode) {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                selectColorRegion(this, point, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (colorPickMode && wParam == VK_ESCAPE) {
                stopColorPickMode(this);
                return 0;
            }
            break;
        case WM_CLOSE:
            if (running) {
                MessageBoxW(window, L"预处理仍在进行，请等待完成。",
                            L"CrucibleCpp GUI", MB_ICONINFORMATION);
                return 0;
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (announcementWorker.joinable()) announcementWorker.detach();
            KillTimer(window, 1);
            KillTimer(window, 2);
            if (!skipSaveOnExit) saveSettings();
            if (uiFont) DeleteObject(uiFont);
            if (titleFont) DeleteObject(titleFont);
            if (subtitleFont) DeleteObject(subtitleFont);
            if (announcementTitleFont) DeleteObject(announcementTitleFont);
            if (announcementBodyFont) DeleteObject(announcementBodyFont);
            if (announcementCloseFont) DeleteObject(announcementCloseFont);
            if (codeFont) DeleteObject(codeFont);
            if (backgroundBrush) DeleteObject(backgroundBrush);
            if (panelBrush) DeleteObject(panelBrush);
            if (outputBackgroundBrush) DeleteObject(outputBackgroundBrush);
            if (diagBackgroundBrush) DeleteObject(diagBackgroundBrush);
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
            state->app->putIni(L"agreementVersion", L"2");
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

LRESULT CALLBACK AnnouncementBannerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!app) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<AppState*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        app->paintAnnouncement(dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONUP:
        app->clickAnnouncement(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_SETCURSOR: {
        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(window, &point);
        RECT client{};
        GetClientRect(window, &client);
        const bool overClose = point.x >= client.right - 52 && point.y <= 48;
        const bool overExpand = point.x >= client.right - 220 && point.x <= client.right - 46 &&
                                point.y >= 70 && point.y <= 104;
        const bool overLink = !app->announcement.link.empty() &&
                              point.x >= 40 && point.x <= client.right - 230 &&
                              point.y >= 72 && point.y <= 102;
        if (overClose || overExpand || overLink) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return TRUE;
        }
        break;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

struct AnnouncementFullscreenState {
    AppState* app = nullptr;
    HWND window = nullptr;
    HWND title = nullptr;
    HWND body = nullptr;
    HWND close = nullptr;
    HWND link = nullptr;
};

LRESULT CALLBACK AnnouncementFullscreenProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<AnnouncementFullscreenState*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = static_cast<AnnouncementFullscreenState*>(create->lpCreateParams);
            state->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        } else {
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }
    switch (message) {
    case WM_CREATE: {
        state->title = CreateWindowExW(0, L"STATIC", utf8ToWide(state->app->announcement.title).c_str(),
                                       WS_CHILD | WS_VISIBLE | SS_LEFT,
                                       90, 138, 800, 48, window, nullptr,
                                       state->app->instance, nullptr);
        SendMessageW(state->title, WM_SETFONT,
                     reinterpret_cast<WPARAM>(state->app->titleFont), TRUE);
        state->body = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
                                      editMultiline(utf8ToWide(state->app->announcement.body)).c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                          ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                                      90, 198, 800, 400, window,
                                      reinterpret_cast<HMENU>(2001), state->app->instance,
                                      nullptr);
        SendMessageW(state->body, WM_SETFONT,
                     reinterpret_cast<WPARAM>(state->app->uiFont), TRUE);
        state->link = CreateWindowExW(0, L"BUTTON",
                                      state->app->announcementLinkText.c_str(),
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                      90, 500, 160, 38, window,
                                      reinterpret_cast<HMENU>(2002), state->app->instance,
                                      nullptr);
        state->close = CreateWindowExW(0, L"BUTTON", L"关闭",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                       760, 500, 132, 38, window,
                                       reinterpret_cast<HMENU>(IDCANCEL), state->app->instance,
                                       nullptr);
        SendMessageW(state->link, WM_SETFONT,
                     reinterpret_cast<WPARAM>(state->app->uiFont), TRUE);
        SendMessageW(state->close, WM_SETFONT,
                     reinterpret_cast<WPARAM>(state->app->uiFont), TRUE);
        if (state->app->announcementLink.empty()) {
            ShowWindow(state->link, SW_HIDE);
        }
        RECT client{};
        GetClientRect(window, &client);
        SendMessageW(window, WM_SIZE, 0,
                     MAKELPARAM(client.right - client.left, client.bottom - client.top));
        return 0;
    }
    case WM_SIZE: {
        const int width = LOWORD(lParam);
        const int height = HIWORD(lParam);
        if (width <= 0 || height <= 0 || !state->title) return 0;
        const int cardWidth = std::min(1200, std::max(400, width - 120));
        const int cardLeft = (width - cardWidth) / 2;
        MoveWindow(state->title, cardLeft + 50, 136, cardWidth - 100, 52, TRUE);
        const int buttonY = height - 82;
        MoveWindow(state->body, cardLeft + 50, 198, cardWidth - 100,
                   std::max(100, buttonY - 210), TRUE);
        MoveWindow(state->link, cardLeft + 50, buttonY, 170, 40, TRUE);
        MoveWindow(state->close, cardLeft + cardWidth - 182, buttonY, 132, 40, TRUE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        state->app->paintAnnouncementFullscreen(dc, window);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, kPanel);
        SetTextColor(dc, kText);
        return reinterpret_cast<LRESULT>(state->app->panelBrush);
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == 2002 && !state->app->announcementLink.empty()) {
            ShellExecuteW(window, L"open", state->app->announcementLink.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
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

void showAnnouncementFullscreen(AppState* app) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = AnnouncementFullscreenProc;
        windowClass.hInstance = app->instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = app->backgroundBrush;
        windowClass.lpszClassName = L"CrucibleCppAnnouncementFullscreen";
        if (!RegisterClassExW(&windowClass)) return;
        registered = true;
    }
    AnnouncementFullscreenState state;
    state.app = app;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = std::max(480, static_cast<int>(work.right - work.left));
    const int height = std::max(360, static_cast<int>(work.bottom - work.top));
    EnableWindow(app->window, FALSE);
    HWND dialog = CreateWindowExW(
        WS_EX_TOPMOST, L"CrucibleCppAnnouncementFullscreen", L"CrucibleCpp 公告",
        WS_POPUP | WS_VISIBLE, work.left, work.top, width, height,
        app->window, nullptr, app->instance, &state);
    if (!dialog) {
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

void AppState::openAnnouncementFullscreen() {
    showAnnouncementFullscreen(this);
}

struct SettingsState {
    AppState* app = nullptr;
    HWND window = nullptr;
    HWND announcement = nullptr;
    HWND compare = nullptr;
    HWND fontHeader = nullptr;
    HWND colorHeader = nullptr;
    HWND uiValue = nullptr;
    HWND uiTrack = nullptr;
    HWND codeValue = nullptr;
    HWND codeTrack = nullptr;
    HWND spacingValue = nullptr;
    HWND spacingTrack = nullptr;
    HWND wheel = nullptr;
    HWND colorMode = nullptr;
    HWND applyColor = nullptr;
    HWND colorStatus = nullptr;
    HWND reset = nullptr;
    HWND fullscreen = nullptr;
    HWND ok = nullptr;
    HWND cancel = nullptr;
    HWND announcementNote = nullptr;
    HWND compareNote = nullptr;
    bool fontOpen = false;
    bool colorOpen = false;
    bool fullScreenMode = false;
    RECT normalRect{};
};

void updateFontLabels(SettingsState* state) {
    if (!state) return;
    wchar_t buffer[64]{};
    swprintf_s(buffer, L"界面字体大小：%d px",
               static_cast<int>(SendMessageW(state->uiTrack, TBM_GETPOS, 0, 0)));
    SetWindowTextW(state->uiValue, buffer);
    swprintf_s(buffer, L"代码字体大小：%d px",
               static_cast<int>(SendMessageW(state->codeTrack, TBM_GETPOS, 0, 0)));
    SetWindowTextW(state->codeValue, buffer);
    swprintf_s(buffer, L"字间距：%d px",
               static_cast<int>(SendMessageW(state->spacingTrack, TBM_GETPOS, 0, 0)));
    SetWindowTextW(state->spacingValue, buffer);
}

int settingsExpandedHeight(const SettingsState* state) {
    int height = 380;
    if (state->fontOpen) height += 112;
    if (state->colorOpen) height += 320;
    return height;
}

void layoutSettings(SettingsState* state, int width, int height) {
    if (!state || !state->window) return;
    const int margin = 22;
    const int row = 36;
    int y = 122;
    MoveWindow(state->fontHeader, margin, y, width - 2 * margin, 34, TRUE);
    y += row;
    if (state->fontOpen) {
        MoveWindow(state->uiValue, margin, y, 155, 22, TRUE);
        MoveWindow(state->uiTrack, margin + 158, y - 6, width - 2 * margin - 158, 32, TRUE);
        MoveWindow(state->codeValue, margin, y + 40, 155, 22, TRUE);
        MoveWindow(state->codeTrack, margin + 158, y + 34, width - 2 * margin - 158, 32, TRUE);
        MoveWindow(state->spacingValue, margin, y + 80, 155, 22, TRUE);
        MoveWindow(state->spacingTrack, margin + 158, y + 74, width - 2 * margin - 158, 32, TRUE);
        y += 112;
    }
    MoveWindow(state->colorHeader, margin, y, width - 2 * margin, 34, TRUE);
    y += row;
    if (state->colorOpen) {
        const int wheelHeight = std::min(240, std::max(160, height - y - 130));
        MoveWindow(state->wheel, margin, y, width - 2 * margin, wheelHeight, TRUE);
        y += wheelHeight + 8;
        MoveWindow(state->colorMode, margin, y, 190, 32, TRUE);
        MoveWindow(state->applyColor, margin + 198, y, 150, 32, TRUE);
        MoveWindow(state->colorStatus, margin + 356, y + 5, width - 2 * margin - 356, 24, TRUE);
    }
    const int footer = height - 54;
    MoveWindow(state->reset, margin, footer, 150, 34, TRUE);
    MoveWindow(state->fullscreen, width - 334, footer, 96, 34, TRUE);
    MoveWindow(state->ok, width - 232, footer, 96, 34, TRUE);
    MoveWindow(state->cancel, width - 126, footer, 96, 34, TRUE);
}

void releaseWheelCache(AppState* app) {
    if (!app) return;
    if (app->wheelMem) {
        if (app->wheelOldBitmap) {
            SelectObject(app->wheelMem, app->wheelOldBitmap);
        }
        DeleteDC(app->wheelMem);
        app->wheelMem = nullptr;
    }
    if (app->wheelBitmap) {
        DeleteObject(app->wheelBitmap);
        app->wheelBitmap = nullptr;
    }
    app->wheelBits = nullptr;
    app->wheelOldBitmap = nullptr;
    app->wheelW = 0;
    app->wheelH = 0;
    app->wheelCachedVal = -1;
    app->wheelBarHue = -1;
    app->wheelBarSat = -1;
}

bool ensureWheelCache(AppState* app, HDC reference, int width, int height) {
    if (!app || width <= 0 || height <= 0) return false;
    if (app->wheelBitmap && app->wheelW == width && app->wheelH == height) {
        return true;
    }
    releaseWheelCache(app);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(reference, &info, DIB_RGB_COLORS,
                                      &bits, nullptr, 0);
    if (!bitmap || !bits) return false;
    HDC memory = CreateCompatibleDC(reference);
    if (!memory) {
        DeleteObject(bitmap);
        return false;
    }
    app->wheelBitmap = bitmap;
    app->wheelMem = memory;
    app->wheelBits = bits;
    app->wheelOldBitmap = SelectObject(memory, bitmap);
    app->wheelW = width;
    app->wheelH = height;
    app->wheelCachedVal = -1;
    app->wheelBarHue = -1;
    app->wheelBarSat = -1;
    std::fill_n(static_cast<std::uint32_t*>(bits),
                static_cast<std::size_t>(width) * height, 0x00FFFFFFu);
    return true;
}

std::uint32_t colorPixel(COLORREF color) {
    return (static_cast<std::uint32_t>(GetRValue(color)) << 16) |
           (static_cast<std::uint32_t>(GetGValue(color)) << 8) |
           static_cast<std::uint32_t>(GetBValue(color));
}

LRESULT CALLBACK ColorWheelProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!app) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<AppState*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }
    auto pick = [&](int x, int y) {
        RECT client{};
        GetClientRect(window, &client);
        const int fieldHeight = std::max(40, static_cast<int>(client.bottom) - 42);
        if (y < fieldHeight) {
            app->pickerHue = std::clamp(x * 360 / std::max(1, static_cast<int>(client.right)), 0, 359);
            app->pickerSat = std::clamp(y * 100 / std::max(1, fieldHeight), 0, 100);
        } else {
            app->pickerVal = std::clamp(x * 100 / std::max(1, static_cast<int>(client.right)), 0, 100);
        }
        app->pickerColor = hsvToRgb(app->pickerHue, app->pickerSat, app->pickerVal);
        InvalidateRect(window, nullptr, FALSE);
        if (app->settingsState) {
            PostMessageW(app->settingsState->window, WM_APP + 7, 0, 0);
        }
    };
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        const int width = static_cast<int>(client.right);
        const int height = static_cast<int>(client.bottom);
        const int fieldHeight = std::max(40, height - 42);
        if (!ensureWheelCache(app, dc, width, height)) {
            EndPaint(window, &paint);
            return 0;
        }
        auto* pixels = static_cast<std::uint32_t*>(app->wheelBits);
        if (app->wheelCachedVal != app->pickerVal) {
            for (int y = 0; y < fieldHeight; ++y) {
                const int sat = std::clamp(y * 100 / std::max(1, fieldHeight), 0, 100);
                for (int x = 0; x < width; ++x) {
                    const int hue = std::clamp(x * 360 / std::max(1, width), 0, 359);
                    pixels[static_cast<std::size_t>(y) * width + x] =
                        colorPixel(hsvToRgb(hue, sat, app->pickerVal));
                }
            }
            app->wheelCachedVal = app->pickerVal;
        }
        if (app->wheelBarHue != app->pickerHue ||
            app->wheelBarSat != app->pickerSat) {
            const int top = std::max(fieldHeight, height - 34);
            const int bottom = std::max(top + 1, height - 10);
            for (int x = 0; x < width; ++x) {
                const int value = std::clamp(x * 100 / std::max(1, width), 0, 100);
                const std::uint32_t color =
                    colorPixel(hsvToRgb(app->pickerHue, app->pickerSat, value));
                for (int y = top; y < bottom; ++y) {
                    pixels[static_cast<std::size_t>(y) * width + x] = color;
                }
            }
            app->wheelBarHue = app->pickerHue;
            app->wheelBarSat = app->pickerSat;
        }
        BitBlt(dc, 0, 0, width, height, app->wheelMem, 0, 0, SRCCOPY);
        const int crossX = app->pickerHue * width / 360;
        const int crossY = app->pickerSat * fieldHeight / 100;
        HPEN shadow = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));
        HGDIOBJ oldPen = SelectObject(dc, shadow);
        MoveToEx(dc, crossX - 7, crossY, nullptr);
        LineTo(dc, crossX + 7, crossY);
        MoveToEx(dc, crossX, crossY - 7, nullptr);
        LineTo(dc, crossX, crossY + 7);
        HPEN white = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        SelectObject(dc, white);
        MoveToEx(dc, crossX - 7, crossY, nullptr);
        LineTo(dc, crossX + 7, crossY);
        MoveToEx(dc, crossX, crossY - 7, nullptr);
        LineTo(dc, crossX, crossY + 7);
        SelectObject(dc, oldPen);
        DeleteObject(shadow);
        DeleteObject(white);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_LBUTTONDOWN:
        SetCapture(window);
        pick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == window) {
            pick(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        }
        return 0;
    case WM_LBUTTONUP:
        if (!app->colorSelection.empty()) applyPickerColor(app);
        ReleaseCapture();
        return 0;
    case WM_SIZE:
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_NCDESTROY:
        releaseWheelCache(app);
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void commitSettings(SettingsState* state) {
    if (!state) return;
    state->app->alwaysShowAnnouncement =
        SendMessageW(state->announcement, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state->app->enableCompareGcc =
        SendMessageW(state->compare, BM_GETCHECK, 0, 0) == BST_CHECKED;
    state->app->uiFontSize = static_cast<int>(
        SendMessageW(state->uiTrack, TBM_GETPOS, 0, 0));
    state->app->codeFontSize = static_cast<int>(
        SendMessageW(state->codeTrack, TBM_GETPOS, 0, 0));
    state->app->letterSpacing = static_cast<int>(
        SendMessageW(state->spacingTrack, TBM_GETPOS, 0, 0));
    if (!state->app->colorSelection.empty()) applyPickerColor(state->app);
    state->app->saveSettings();
    state->app->rebuildFonts();
    state->app->applyDisplaySettings();
    state->app->layoutForWindow();
}

LRESULT CALLBACK SettingsProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = static_cast<SettingsState*>(create->lpCreateParams);
            state->window = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        } else {
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }
    switch (message) {
    case WM_CREATE: {
        const DWORD labelStyle = WS_CHILD | WS_VISIBLE | SS_LEFT;
        const DWORD checkStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX;
        HWND title = CreateWindowExW(0, L"STATIC", L"设置", labelStyle,
                                     22, 14, 500, 38, window, nullptr,
                                     state->app->instance, nullptr);
        state->announcement = CreateWindowExW(0, L"BUTTON", L"每次启动展示公告",
                                              checkStyle, 22, 58, 520, 26, window,
                                              reinterpret_cast<HMENU>(3001),
                                              state->app->instance, nullptr);
        state->compare = CreateWindowExW(0, L"BUTTON", L"显示“对比 g++ -E”按钮",
                                         checkStyle, 22, 88, 520, 26, window,
                                         reinterpret_cast<HMENU>(3002),
                                         state->app->instance, nullptr);
        state->fontHeader = CreateWindowExW(0, L"BUTTON", L"▶  字体与间距",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                            22, 122, 600, 34, window,
                                            reinterpret_cast<HMENU>(4001), state->app->instance,
                                            nullptr);
        state->colorHeader = CreateWindowExW(0, L"BUTTON", L"▶  颜色与调色盘",
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                             22, 158, 600, 34, window,
                                             reinterpret_cast<HMENU>(4002), state->app->instance,
                                             nullptr);
        state->uiValue = CreateWindowExW(0, L"STATIC", L"", labelStyle,
                                         22, 200, 160, 22, window, nullptr,
                                         state->app->instance, nullptr);
        state->uiTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                         WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
                                         180, 194, 500, 32, window,
                                         reinterpret_cast<HMENU>(4003), state->app->instance,
                                         nullptr);
        state->codeValue = CreateWindowExW(0, L"STATIC", L"", labelStyle,
                                          22, 240, 160, 22, window, nullptr,
                                          state->app->instance, nullptr);
        state->codeTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                          WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
                                          180, 234, 500, 32, window,
                                          reinterpret_cast<HMENU>(4004), state->app->instance,
                                          nullptr);
        state->spacingValue = CreateWindowExW(0, L"STATIC", L"", labelStyle,
                                              22, 280, 160, 22, window, nullptr,
                                              state->app->instance, nullptr);
        state->spacingTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"",
                                              WS_CHILD | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
                                              180, 274, 500, 32, window,
                                              reinterpret_cast<HMENU>(4005), state->app->instance,
                                              nullptr);
        state->wheel = CreateWindowExW(WS_EX_CLIENTEDGE, L"CrucibleCppColorWheel", L"",
                                       WS_CHILD, 22, 200, 600, 220, window, nullptr,
                                       state->app->instance, state->app);
        state->colorMode = CreateWindowExW(0, L"BUTTON", L"开始界面取色",
                                           WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                           22, 430, 190, 32, window,
                                           reinterpret_cast<HMENU>(4007), state->app->instance,
                                           nullptr);
        state->applyColor = CreateWindowExW(0, L"BUTTON", L"应用颜色",
                                            WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON,
                                            220, 430, 150, 32, window,
                                            reinterpret_cast<HMENU>(4008), state->app->instance,
                                            nullptr);
        state->colorStatus = CreateWindowExW(0, L"STATIC", L"点击“开始界面取色”，然后悬停正式界面。",
                                             WS_CHILD | SS_LEFT,
                                             378, 435, 270, 24, window, nullptr,
                                             state->app->instance, nullptr);
        state->reset = CreateWindowExW(0, L"BUTTON", L"重置本地缓存",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                       22, 300, 150, 34, window,
                                       reinterpret_cast<HMENU>(3003), state->app->instance,
                                       nullptr);
        state->fullscreen = CreateWindowExW(0, L"BUTTON", L"全屏",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                            600, 300, 96, 34, window,
                                            reinterpret_cast<HMENU>(4006), state->app->instance,
                                            nullptr);
        state->ok = CreateWindowExW(0, L"BUTTON", L"确定",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                    700, 300, 96, 34, window,
                                    reinterpret_cast<HMENU>(IDOK), state->app->instance,
                                    nullptr);
        state->cancel = CreateWindowExW(0, L"BUTTON", L"取消",
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                        806, 300, 96, 34, window,
                                        reinterpret_cast<HMENU>(IDCANCEL), state->app->instance,
                                        nullptr);
        for (HWND control : {title, state->announcement, state->compare, state->fontHeader,
                             state->colorHeader, state->uiValue, state->uiTrack,
                             state->codeValue, state->codeTrack, state->spacingValue,
                             state->spacingTrack, state->colorMode, state->applyColor,
                             state->colorStatus, state->reset, state->fullscreen,
                             state->ok, state->cancel}) {
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(state->app->uiFont), TRUE);
        }
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(state->app->titleFont), TRUE);
        SendMessageW(state->announcement, BM_SETCHECK,
                     state->app->alwaysShowAnnouncement ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(state->compare, BM_SETCHECK,
                     state->app->enableCompareGcc ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(state->uiTrack, TBM_SETRANGE, TRUE, MAKELPARAM(10, 26));
        SendMessageW(state->codeTrack, TBM_SETRANGE, TRUE, MAKELPARAM(10, 28));
        SendMessageW(state->spacingTrack, TBM_SETRANGE, TRUE, MAKELPARAM(0, 4));
        SendMessageW(state->uiTrack, TBM_SETPOS, TRUE, state->app->uiFontSize);
        SendMessageW(state->codeTrack, TBM_SETPOS, TRUE, state->app->codeFontSize);
        SendMessageW(state->spacingTrack, TBM_SETPOS, TRUE, state->app->letterSpacing);
        updateFontLabels(state);
        ShowWindow(state->uiValue, SW_HIDE); ShowWindow(state->uiTrack, SW_HIDE);
        ShowWindow(state->codeValue, SW_HIDE); ShowWindow(state->codeTrack, SW_HIDE);
        ShowWindow(state->spacingValue, SW_HIDE); ShowWindow(state->spacingTrack, SW_HIDE);
        ShowWindow(state->wheel, SW_HIDE); ShowWindow(state->colorMode, SW_HIDE);
        ShowWindow(state->applyColor, SW_HIDE); ShowWindow(state->colorStatus, SW_HIDE);
        state->app->settingsState = state;
        SetTimer(window, 11, 400, nullptr);
        RECT client{};
        GetClientRect(window, &client);
        layoutSettings(state, client.right - client.left, client.bottom - client.top);
        return 0;
    }
    case WM_SIZE:
        layoutSettings(state, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_HSCROLL:
        updateFontLabels(state);
        state->app->uiFontSize = static_cast<int>(
            SendMessageW(state->uiTrack, TBM_GETPOS, 0, 0));
        state->app->codeFontSize = static_cast<int>(
            SendMessageW(state->codeTrack, TBM_GETPOS, 0, 0));
        state->app->letterSpacing = static_cast<int>(
            SendMessageW(state->spacingTrack, TBM_GETPOS, 0, 0));
        state->app->putIni(L"uiFontSize", std::to_wstring(state->app->uiFontSize));
        state->app->putIni(L"codeFontSize", std::to_wstring(state->app->codeFontSize));
        state->app->putIni(L"letterSpacing", std::to_wstring(state->app->letterSpacing));
        state->app->rebuildFonts();
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kText);
        return reinterpret_cast<LRESULT>(state->app->backgroundBrush);
    }
    case WM_APP + 7: {
        wchar_t status[128]{};
        if (state->app->colorPickMode) {
            swprintf_s(status, L"取色中：已选 %zu 块；Ctrl 多选，Esc 结束",
                       state->app->colorSelection.size());
            SetWindowTextW(state->colorMode, L"取色中（右键结束）");
        } else {
            swprintf_s(status, L"已选 %zu 块；点击“应用颜色”写入",
                       state->app->colorSelection.size());
            SetWindowTextW(state->colorMode, L"开始界面取色");
        }
        SetWindowTextW(state->colorStatus, status);
        InvalidateRect(state->wheel, nullptr, TRUE);
        InvalidateRect(state->colorStatus, nullptr, TRUE);
        return 0;
    }
    case WM_TIMER:
        if (wParam == 11) {
            // Safety-net persistence: even if a slider notification is lost,
            // the current control values are flushed while the dialog is open.
            const int uiSize = static_cast<int>(
                SendMessageW(state->uiTrack, TBM_GETPOS, 0, 0));
            const int codeSize = static_cast<int>(
                SendMessageW(state->codeTrack, TBM_GETPOS, 0, 0));
            const int spacing = static_cast<int>(
                SendMessageW(state->spacingTrack, TBM_GETPOS, 0, 0));
            if (uiSize != state->app->uiFontSize ||
                codeSize != state->app->codeFontSize ||
                spacing != state->app->letterSpacing) {
                state->app->uiFontSize = uiSize;
                state->app->codeFontSize = codeSize;
                state->app->letterSpacing = spacing;
                state->app->putIni(L"uiFontSize", std::to_wstring(uiSize));
                state->app->putIni(L"codeFontSize", std::to_wstring(codeSize));
                state->app->putIni(L"letterSpacing", std::to_wstring(spacing));
                state->app->rebuildFonts();
            }
            return 0;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == 4001) {
            state->fontOpen = !state->fontOpen;
            SetWindowTextW(state->fontHeader, state->fontOpen ? L"▼  字体与间距" : L"▶  字体与间距");
            for (HWND control : {state->uiValue, state->uiTrack, state->codeValue,
                                 state->codeTrack, state->spacingValue, state->spacingTrack}) {
                ShowWindow(control, state->fontOpen ? SW_SHOW : SW_HIDE);
            }
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            const int desired = std::min(settingsExpandedHeight(state),
                                         static_cast<int>(work.bottom - work.top) - 40);
            SetWindowPos(window, nullptr, 0, 0, 850, desired,
                         SWP_NOMOVE | SWP_NOZORDER);
            return 0;
        }
        if (LOWORD(wParam) == 4002) {
            state->colorOpen = !state->colorOpen;
            SetWindowTextW(state->colorHeader, state->colorOpen ? L"▼  颜色与调色盘" : L"▶  颜色与调色盘");
            for (HWND control : {state->wheel, state->colorMode, state->applyColor, state->colorStatus}) {
                ShowWindow(control, state->colorOpen ? SW_SHOW : SW_HIDE);
            }
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            const int desired = std::min(settingsExpandedHeight(state),
                                         static_cast<int>(work.bottom - work.top) - 40);
            SetWindowPos(window, nullptr, 0, 0, 850, desired,
                         SWP_NOMOVE | SWP_NOZORDER);
            return 0;
        }
        if (LOWORD(wParam) == 4007) {
            startColorPickMode(state->app);
            SetWindowTextW(state->colorMode, L"取色中（右键结束）");
            SetWindowTextW(state->colorStatus, L"悬停正式界面中的块，点击选择；Ctrl 多选，右键结束。");
            return 0;
        }
        if (LOWORD(wParam) == 4008) {
            applyPickerColor(state->app);
            SetWindowTextW(state->colorStatus, L"颜色已应用到选中的块。");
            return 0;
        }
        if (LOWORD(wParam) == 3003) {
            if (MessageBoxW(window,
                            L"此操作会导致设置选项清空，用户协议状态清空，软件启动计数器清空！",
                            L"重置本地缓存",
                            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
                return 0;
            }
            std::wstring errorText;
            if (!scheduleCacheReset(errorText)) {
                MessageBoxW(window, errorText.c_str(), L"重置本地缓存",
                            MB_OK | MB_ICONERROR);
                return 0;
            }
            state->app->skipSaveOnExit = true;
            MessageBoxW(window,
                        L"本地缓存将在下次启动时清空。\n软件现在将退出。",
                        L"重置本地缓存", MB_OK | MB_ICONINFORMATION);
            ExitProcess(0);
        }
        if (LOWORD(wParam) == 4006) {
            if (!state->fullScreenMode) {
                GetWindowRect(window, &state->normalRect);
                RECT work{};
                SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
                SetWindowPos(window, HWND_TOP, work.left, work.top,
                             work.right - work.left, work.bottom - work.top,
                             SWP_SHOWWINDOW);
                state->fullScreenMode = true;
                SetWindowTextW(state->fullscreen, L"还原");
            } else {
                SetWindowPos(window, HWND_TOP, state->normalRect.left, state->normalRect.top,
                             state->normalRect.right - state->normalRect.left,
                             state->normalRect.bottom - state->normalRect.top, SWP_SHOWWINDOW);
                state->fullScreenMode = false;
                SetWindowTextW(state->fullscreen, L"全屏");
            }
            return 0;
        }
        if (LOWORD(wParam) == IDOK) {
            commitSettings(state);
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            stopColorPickMode(state->app);
            DestroyWindow(window);
            return 0;
        }
        break;
    case WM_CLOSE:
        commitSettings(state);
        stopColorPickMode(state->app);
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        KillTimer(window, 11);
        stopColorPickMode(state->app);
        state->app->settingsState = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        delete state;
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

void showSettingsDialog(AppState* app) {
    if (app->settingsState) {
        SetForegroundWindow(app->settingsState->window);
        return;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = SettingsProc;
        windowClass.hInstance = app->instance;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = app->backgroundBrush;
        windowClass.lpszClassName = L"CrucibleCppSettingsWindow";
        RegisterClassExW(&windowClass);
        WNDCLASSEXW wheelClass{};
        wheelClass.cbSize = sizeof(wheelClass);
        wheelClass.lpfnWndProc = ColorWheelProc;
        wheelClass.hInstance = app->instance;
        wheelClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wheelClass.hbrBackground = nullptr;
        wheelClass.lpszClassName = L"CrucibleCppColorWheel";
        RegisterClassExW(&wheelClass);
        registered = true;
    }
    auto* state = new SettingsState{};
    state->app = app;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int width = 850;
    const int height = 380;
    HWND dialog = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, L"CrucibleCppSettingsWindow",
        L"CrucibleCpp 设置", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        work.left + std::max(20, (static_cast<int>(work.right - work.left) - width) / 2),
        work.top + std::max(20, (static_cast<int>(work.bottom - work.top) - height) / 2),
        width, height, app->window, nullptr, app->instance, state);
    if (!dialog) {
        delete state;
        return;
    }
    UpdateWindow(dialog);
}

void AppState::showSettings() {
    showSettingsDialog(this);
}

std::vector<ColorRegion> colorRegions(AppState* app) {
    std::vector<ColorRegion> regions;
    auto addControl = [&](HWND control, int index, const wchar_t* label) {
        if (!control || !IsWindowVisible(control)) return;
        RECT rect{};
        GetWindowRect(control, &rect);
        MapWindowPoints(HWND_DESKTOP, app->window, reinterpret_cast<POINT*>(&rect), 2);
        regions.push_back({index, rect, label});
    };
    if (app->announcementVisible) {
        addControl(app->announcementBanner, 3, L"公告卡片");
    }
    addControl(app->outputView, 10, L"输出背景");
    addControl(app->diagView, 11, L"诊断背景");
    addControl(app->run, 6, L"强调色");
    addControl(app->compareGcc, 6, L"强调色");
    addControl(app->compileOutput, 6, L"强调色");
    addControl(app->options, 7, L"浅强调");
    addControl(app->copyCli, 6, L"强调色");
    addControl(app->settingsButton, 6, L"强调色");
    addControl(app->cancel, 9, L"取消/危险");
    addControl(app->browseInput, 6, L"强调色");
    addControl(app->browseOutput, 6, L"强调色");
    addControl(app->browseGcc, 6, L"强调色");
    addControl(app->detectGcc, 6, L"强调色");
    addControl(app->status, 5, L"次要文字");
    for (HWND label : app->labels) addControl(label, 4, L"正文");
    RECT client{};
    GetClientRect(app->window, &client);
    regions.push_back({2, app->formRect, L"面板"});
    regions.push_back({1, RECT{0, 0, client.right, 76}, L"顶部栏"});
    return regions;
}

int colorRegionIndexAt(AppState* app, POINT point) {
    const auto regions = colorRegions(app);
    for (const auto& region : regions) {
        if (PtInRect(&region.rect, point)) return region.colorIndex;
    }
    RECT form = app->formRect;
    const int edge = 6;
    if (point.x >= form.left - edge && point.x <= form.right + edge &&
        point.y >= form.top - edge && point.y <= form.bottom + edge) {
        if (point.x <= form.left + edge || point.x >= form.right - edge ||
            point.y <= form.top + edge || point.y >= form.bottom - edge) {
            return 8;
        }
    }
    RECT client{};
    GetClientRect(app->window, &client);
    if (point.y < 76) return 1;
    if (PtInRect(&form, point)) return 2;
    return 0;
}

void syncPickerFromSelection(AppState* app) {
    if (app->colorSelection.empty()) return;
    const auto defs = themeColorDefs();
    const int index = app->colorSelection.front();
    if (index < 0 || index >= static_cast<int>(defs.size())) return;
    app->pickerColor = *defs[index].value;
    rgbToHsv(app->pickerColor, app->pickerHue, app->pickerSat, app->pickerVal);
}

void startColorPickMode(AppState* app) {
    if (!app) return;
    RECT client{};
    GetClientRect(app->window, &client);
    POINT origin{client.left, client.top};
    ClientToScreen(app->window, &origin);
    if (!app->colorOverlay) {
        WNDCLASSEXW overlayClass{};
        overlayClass.cbSize = sizeof(overlayClass);
        overlayClass.lpfnWndProc = ColorOverlayProc;
        overlayClass.hInstance = app->instance;
        overlayClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        overlayClass.hbrBackground = nullptr;
        overlayClass.lpszClassName = L"CrucibleCppColorOverlay";
        RegisterClassExW(&overlayClass);
        app->colorOverlay = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"CrucibleCppColorOverlay", L"", WS_POPUP | WS_VISIBLE,
            origin.x, origin.y, client.right - client.left, client.bottom - client.top,
            nullptr, nullptr, app->instance, app);
        if (!app->colorOverlay) return;
        SetLayeredWindowAttributes(app->colorOverlay, RGB(255, 0, 255), 0,
                                   LWA_COLORKEY);
    }
    app->colorPickMode = true;
    SetWindowPos(app->colorOverlay, HWND_TOP, origin.x, origin.y,
                 client.right - client.left, client.bottom - client.top,
                 SWP_SHOWWINDOW);
    app->colorHover = -1;
    InvalidateRect(app->colorOverlay, nullptr, TRUE);
}

void stopColorPickMode(AppState* app) {
    if (!app) return;
    if (GetCapture() == app->window) ReleaseCapture();
    app->colorPickMode = false;
    app->colorHover = -1;
    if (app->colorOverlay) ShowWindow(app->colorOverlay, SW_HIDE);
    if (app->settingsState) {
        PostMessageW(app->settingsState->window, WM_APP + 7, 0, 0);
    }
    InvalidateRect(app->window, nullptr, TRUE);
}

void updateColorPickHover(AppState* app, POINT point) {
    if (!app || !app->colorPickMode) return;
    const int index = colorRegionIndexAt(app, point);
    if (index != app->colorHover) {
        app->colorHover = index;
        InvalidateRect(app->colorOverlay, nullptr, TRUE);
    }
}

void selectColorRegion(AppState* app, POINT point, bool ctrl) {
    if (!app || !app->colorPickMode) return;
    const int index = colorRegionIndexAt(app, point);
    if (!ctrl) app->colorSelection.clear();
    auto found = std::find(app->colorSelection.begin(), app->colorSelection.end(), index);
    if (found == app->colorSelection.end()) {
        app->colorSelection.push_back(index);
    } else if (ctrl) {
        app->colorSelection.erase(found);
    }
    syncPickerFromSelection(app);
    InvalidateRect(app->colorOverlay, nullptr, TRUE);
    if (app->settingsState) {
        PostMessageW(app->settingsState->window, WM_APP + 7, 0, 0);
    }
}

void paintColorOverlay(AppState* app, HDC dc) {
    if (!app || !app->colorPickMode) return;
    RECT client{};
    GetClientRect(app->colorOverlay, &client);
    HBRUSH key = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(dc, &client, key);
    DeleteObject(key);
    const auto regions = colorRegions(app);
    HPEN selectedPen = CreatePen(PS_DOT, 1, kDanger);
    HPEN hoverPen = CreatePen(PS_SOLID, 3, kAccent);
    for (const auto& region : regions) {
        if (std::find(app->colorSelection.begin(), app->colorSelection.end(),
                      region.colorIndex) != app->colorSelection.end()) {
            HGDIOBJ old = SelectObject(dc, selectedPen);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, region.rect.left, region.rect.top, region.rect.right,
                      region.rect.bottom);
            SelectObject(dc, oldBrush);
            SelectObject(dc, old);
        }
    }
    for (const auto& region : regions) {
        if (region.colorIndex != app->colorHover) continue;
        HGDIOBJ old = SelectObject(dc, hoverPen);
        HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, region.rect.left, region.rect.top, region.rect.right,
                  region.rect.bottom);
        SelectObject(dc, oldBrush);
        SelectObject(dc, old);
        RECT label{region.rect.left + 4, region.rect.top + 4,
                   region.rect.left + 190, region.rect.top + 30};
        HBRUSH labelBrush = CreateSolidBrush(kAccent);
        FillRect(dc, &label, labelBrush);
        DeleteObject(labelBrush);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        HGDIOBJ font = SelectObject(dc, app->uiFont);
        TextOutW(dc, label.left + 6, label.top + 5, region.label,
                 static_cast<int>(wcslen(region.label)));
        SelectObject(dc, font);
        break;
    }
    DeleteObject(selectedPen);
    DeleteObject(hoverPen);
}

void applyPickerColor(AppState* app) {
    if (!app || app->colorSelection.empty()) return;
    const auto defs = themeColorDefs();
    for (int index : app->colorSelection) {
        if (index >= 0 && index < static_cast<int>(defs.size())) {
            *defs[index].value = app->pickerColor;
            app->putIni(defs[index].key, colorToHex(app->pickerColor));
        }
    }
    app->applyThemeColors();
    InvalidateRect(app->window, nullptr, TRUE);
}

LRESULT CALLBACK ColorOverlayProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* app = reinterpret_cast<AppState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!app) {
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            app = static_cast<AppState*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        } else {
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        paintColorOverlay(app, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        MapWindowPoints(window, app->window, &point, 1);
        updateColorPickHover(app, point);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        MapWindowPoints(window, app->window, &point, 1);
        selectColorRegion(app, point, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
        return 0;
    }
    case WM_RBUTTONUP:
        stopColorPickMode(app);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
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

namespace {

std::filesystem::path appDataConfigDirectory() {
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", buffer,
                                                 static_cast<DWORD>(std::size(buffer)));
    std::filesystem::path base = length > 0 ? std::filesystem::path(buffer)
                                            : std::filesystem::path(L".");
    return base / L"CrucibleCpp";
}

std::filesystem::path cacheResetMarker() {
    wchar_t buffer[MAX_PATH * 2]{};
    const DWORD length = GetTempPathW(static_cast<DWORD>(std::size(buffer)), buffer);
    std::filesystem::path base = length > 0 ? std::filesystem::path(buffer)
                                            : std::filesystem::path(L".");
    return base / L"CrucibleCpp-reset.flag";
}

bool scheduleCacheReset(std::wstring& errorText) {
    std::ofstream marker(cacheResetMarker(), std::ios::binary | std::ios::trunc);
    if (!marker) {
        errorText = L"无法创建重置标记文件。";
        return false;
    }
    marker << "reset";
    marker.close();
    return true;
}

bool performPendingCacheReset(std::wstring& errorText) {
    const std::filesystem::path marker = cacheResetMarker();
    if (!std::filesystem::exists(marker)) return true;
    std::error_code error;
    const std::filesystem::path directory = appDataConfigDirectory();
    for (int attempt = 0; attempt < 12; ++attempt) {
        error.clear();
        std::filesystem::remove_all(directory, error);
        if (!error) break;
        Sleep(150);
    }
    if (error) {
        errorText = L"删除本地缓存失败：\n" + directory.wstring() +
                    L"\n\n错误：" + utf8ToWide(error.message()) +
                    L"\n请关闭杀毒软件对配置目录的占用后重新启动。";
        return false;
    }
    error.clear();
    std::filesystem::remove(marker, error);
    return true;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    HANDLE instanceMutex = CreateMutexW(nullptr, TRUE,
                                        L"Local\\CrucibleCppGuiSingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr, L"CrucibleCpp GUI 已经在运行。",
                    L"CrucibleCpp GUI", MB_OK | MB_ICONINFORMATION);
        if (instanceMutex) CloseHandle(instanceMutex);
        return 0;
    }
    {
        std::wstring resetError;
        if (!performPendingCacheReset(resetError)) {
            MessageBoxW(nullptr, resetError.c_str(), L"重置本地缓存",
                        MB_OK | MB_ICONERROR);
            if (instanceMutex) CloseHandle(instanceMutex);
            return 1;
        }
    }
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
    state.announcementTitleFont = CreateFontW(-16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                              DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    state.announcementBodyFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    state.announcementCloseFont = CreateFontW(-36, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                              DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    state.codeFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, L"Consolas");
    state.backgroundBrush = CreateSolidBrush(kBackground);
    state.panelBrush = CreateSolidBrush(kPanel);
    state.outputBackgroundBrush = CreateSolidBrush(kOutputBackground);
    state.diagBackgroundBrush = CreateSolidBrush(kDiagBackground);

    WNDCLASSEXW bannerClass{};
    bannerClass.cbSize = sizeof(bannerClass);
    bannerClass.style = CS_HREDRAW | CS_VREDRAW;
    bannerClass.lpfnWndProc = AnnouncementBannerProc;
    bannerClass.hInstance = instance;
    bannerClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    bannerClass.hbrBackground = nullptr;
    bannerClass.lpszClassName = L"CrucibleCppAnnouncementBanner";
    RegisterClassExW(&bannerClass);

    WNDCLASSEXW overlayClass{};
    overlayClass.cbSize = sizeof(overlayClass);
    overlayClass.lpfnWndProc = ColorOverlayProc;
    overlayClass.hInstance = instance;
    overlayClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    overlayClass.hbrBackground = nullptr;
    overlayClass.lpszClassName = L"CrucibleCppColorOverlay";
    RegisterClassExW(&overlayClass);

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

    state.fetchAnnouncement();

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









