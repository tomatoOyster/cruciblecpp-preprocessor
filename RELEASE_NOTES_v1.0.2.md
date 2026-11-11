# CrucibleCpp Preprocessor V1.0.2

项目名称更正版本。

## 名称变更

此前项目名少了一个 `l`，本次统一补上并修改所有相关标识：

```text
CrucibleCpp
cruciblecpp
namespace crucible
```

统一修改：

- 可执行文件：`cruciblecpp-preprocessor.exe`
- 仓库：<https://github.com/tomatoOyster/cruciblecpp-preprocessor>
- CMake 项目：`cruciblecpp_preprocessor`
- CMake 目标：`cruciblecpp-preprocessor`
- C++ namespace：`crucible`
- 发行包：`cruciblecpp-preprocessor-v1.0.2-win64.zip`
- 文档、示例、测试和命令行输出

## 功能

- Windows x64 独立 C++20 预处理器。
- 支持 `#include "..."` 和 `#include <...>`。
- 支持 `#include_next`、`__has_include`、GCC 15.2 宏 profile。
- 支持 CMake/Ninja depfile。
- 紧凑输出：默认模式用行标记保持诊断行号，不再输出大量空行。

## 系统要求

- Windows x64
- MinGW-w64 UCRT64 GCC 15.2.0
- C++20

## 快速开始

```powershell
.\cruciblecpp-preprocessor.exe --version

g++ -std=c++20 -O2 main.i -o main.exe
```

标准库头的 include 路径仍需显式传入；CMake 项目可使用 `CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES`。

## 验证

- 31/31 CTest 通过。
- `<vector>`、`<string>`、`<iostream>`、`<filesystem>`、`<windows.h>` 等真实头文件通过。
- 用户真实 `main.cpp` 预处理 0 错误，GCC 编译/链接 0 错误。

## 许可证

MIT，见 `LICENSE`。
