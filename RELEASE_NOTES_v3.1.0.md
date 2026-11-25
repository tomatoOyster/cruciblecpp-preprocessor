# CrucibleCpp v3.1.0

Windows x64 独立 C++20 预处理器，本轮加入原生 Windows GUI Preview。

## 新增

- 原生 Win32 GUI，同 CLI 共用 `cpp_preproc`，不依赖 Qt / WebView2 / Electron / Dear ImGui。
- 源文件和输出 `.i` 路径必须显式选择；空路径点击“预处理”会提示“请先选择目录”。
- GCC 自动检测、C++11/14/17/20、工作线程预处理、取消。
- Include 目录、宏定义、PCH 策略、行标记、保留注释选项。
- 与 `g++ -E` 对比：忽略行标记和空行，差异显示在诊断区。
- 编译 `.i`：强制按 C++ 编译，抑制系统头文件警告噪音，输出 `.exe`。
- 设置持久化、拖放源文件、复制等价 CLI 命令。
- 自绘按钮、悬停动画、DPI 与窗口尺寸自适应布局。

## 修复

- 只读输出/诊断控件滚动时旧文字残留、完全重叠。
- C++ 标准下拉框和“预处理”按钮重叠。
- 高 DPI 下信息区没有铺满窗口。

## 说明

- GUI 为 Preview，CLI 保持独立且稳定。
- 版本：`3.1.0`。
- 许可证：V3.0.0+ 为 CC BY-NC 4.0；V2.9.0- 为 MIT。
- 仅支持 GCC（13/14/15），不支持 Clang/MSVC。

## 资产

- `cruciblecpp-3.1.0-win64-cli.zip`
- `cruciblecpp-3.1.0-win64-gui.zip`
- `SHA256SUMS.txt`

