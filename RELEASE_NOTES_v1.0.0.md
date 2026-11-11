# CrucibleCpp Preprocessor V1.0.0

首个公开发行版本：Windows x64 独立 C++20 预处理器，直接替代 `g++ -E` 的预处理步骤。

## 亮点

- 支持 `#include "..."` 和 `#include <...>`。
- `<>` 搜索 `-I` / `-isystem`，不搜索当前目录。
- 支持 `#include_next`、`__has_include(<...>)`、宏展开后的 quote/angle include。
- 支持 Windows 反斜杠路径和 `<a//b.h>`。
- 硬编码 MinGW-w64 UCRT64 GCC 15.2.0 / C++20 宏 profile。
- 常用 `__has_builtin` / `__has_attribute` / `__has_cpp_attribute`。
- `#pragma GCC system_header` 消费，不污染 `.i`。
- CMake/Ninja depfile：`-MD/-MMD`、`-MF/-MT/-MQ/-MP`。
- GNU 行标记；物理行号映射。
- 静态 Windows x64 可执行文件，不依赖额外 MinGW DLL。

## 已验证

- 31 个 CTest 全部通过。
- `<vector>`、`<string>`、`<iostream>`、`<filesystem>`、`<chrono>`、`<format>`、`<ranges>` 等标准库头。
- `<windows.h>` / MinGW Windows SDK。
- CMake + Ninja 标准头项目和增量 depfile。
- 用户真实程序：`main.cpp`（含 `<windows.h>`）预处理 0 错误，GCC 对 `.i` 编译/链接 0 错误。

## 系统要求

- Windows x64
- MinGW-w64 UCRT64 GCC 15.2.0
- C++20

## 快速开始

```powershell
.\cruciblecpp-preprocessor.exe --version

.\cruciblecpp-preprocessor.exe -P `
    -isystem D:/vsC++/ucrt64/include/c++/15.2.0 `
    -isystem D:/vsC++/ucrt64/include/c++/15.2.0/x86_64-w64-mingw32 `
    -isystem D:/vsC++/ucrt64/include/c++/15.2.0/backward `
    -isystem D:/vsC++/ucrt64/lib/gcc/x86_64-w64-mingw32/15.2.0/include `
    -isystem D:/vsC++/ucrt64/include `
    -isystem D:/vsC++/ucrt64/lib/gcc/x86_64-w64-mingw32/15.2.0/include-fixed `
    main.cpp -o main.i

g++ -std=c++20 -O2 main.i -o main.exe
```

CMake 项目推荐把 `CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES` 作为 `-isystem` 传入，示例见 README。

## 已知限制

- 只保证 Windows x64 + MinGW-w64 UCRT64 + GCC 15.2.0 + C++20。
- 不自动搜索 sysroot；标准库头路径必须显式传入。
- 不支持 PCH、modules、`#embed`。
- `__has_*` 是常用集合，不是 GCC 完整表。
- 尚未覆盖真实多项目 corpus、fuzz/ASan、CI、安装包。
- `#include /*comment*/ <...>` 等 header-name 边界仍未完全覆盖。

## 许可证

MIT，见 `LICENSE`。
