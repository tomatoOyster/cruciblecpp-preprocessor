# Changelog

## V2.3.0 — 2026-11-12

### 新增

- PCH 策略：`--pch=ignore|warn|error|report`，默认行为与 `g++ -E` 一致，忽略 `.gch` 并读取头文件源码。
- `.gch` 检测、缺失头文件源码时的明确错误、`--analysis-json` 中的 `pch` 记录。
- CMake PCH 风格 `-include cmake_pch.hxx` 兼容。
- 接受 `-Winvalid-pch` / `-Wno-invalid-pch` 作为构建系统兼容参数。
- CTest 增加到 54 个，新增 PCH 策略、depfile、CMake forced include 和缺失源码测试。

### 说明

- 不解析 GCC 二进制 PCH 格式；`g++ -E` 本身不使用 PCH。

## V2.2.0 — 2026-11-12

### 新增

- `-newyear`：直接输出距离下一个 1 月 1 日的天数，忽略其他命令行内容。
- `--analysis-json <file>`：输出 include 边、宏展开调用树、参数原文/展开结果、定义位置和 `#`/`##` 步骤，供 VS Code 插件做宏动画和 include 图。
- 宏展开 trace 保留源文件事件，避免被 libstdc++ 内部宏淹没。
- CTest 增加到 53 个，覆盖 `-newyear` 和参数忽略行为。

## V2.1.0 — 2026-11-11

### 新增

- 多文件预处理：支持多个输入文件与按顺序对应的多个输出文件。
- 两种写法：
  ```text
  cruciblecpp-preprocessor -P a.cpp b.cpp -o a.i b.i
  cruciblecpp-preprocessor -P a.cpp b.cpp -o a.i -o b.i
  ```
- 输入/输出数量不匹配时返回命令行错误。
- 多文件模式暂不支持 depfile；`-MD` 只允许单输入。
- CTest 增加到 51 个，覆盖三输入三输出、字符常量与 charset 边界。
- `#if` 字符常量求值对齐 GCC 15.2 / Windows x64。
- 支持 `-finput-charset`、`-fexec-charset`、`-funsigned-char`、`-fsigned-char`。

## V2.0.0 — 2026-11-11

### 新增

- 自动从 `PATH` 查找 `g++.exe` / `gcc.exe` / `c++.exe`。
- 通过 `g++ -std=c++20 -E -v -x c++ -` 获取 GCC 的隐式 include 搜索列表。
- 尖括号标准库头无需再手动传 `-isystem`。
- `--print-gcc-includes` 查看发现的路径。
- `-nostdinc` / `--no-gcc-includes` 关闭自动发现。
- `-v` 输出使用的 GCC、版本和 include 目录。

### 测试

- 新增“无显式 `-isystem` 的标准头”测试。
- 新增 `-nostdinc` 负例。
- 完整 CTest 34 个用例。

## V1.0.2 — 2026-11-11

### 变更

- 项目名统一为 **`CrucibleCpp`**，补上此前缺失的 `l`。
- 可执行文件、CMake 目标、namespace、仓库名、发行包名统一为 `cruciblecpp` / `crucible`。
- 仓库地址改为 <https://github.com/tomatoOyster/cruciblecpp-preprocessor>。

## V1.0.1 — 2026-11-11

### 修复

- 输出不再为每个源文件行生成空行。标准库/Windows SDK 头较多时，旧输出中约 86% 是空行；现在约 0.4%–0.5%。
- 默认模式在文本块前输出精确行标记，保持诊断行号；`-P` 模式也使用紧凑输出。
- 空行只在未闭合的宏参数内部保留，避免拆断跨行宏调用。

### 验证

- 31/31 CTest 通过。
- 用户 `main.cpp`（含 `<windows.h>`）预处理 0 错误；GCC 编译/链接 0 错误。
- 生成的 `main.i` 行数从约 93 万降到约 15 万，空行从约 80 万降到约 659。

## V1.0.0 — 2026-11-11

首个公开发行版本。

### 核心能力

- 独立 C++20 预处理器，输出 GCC 可直接编译的 `.i`。
- 支持 `#include "..."` 和 `#include <...>`。
- `#include <...>` 搜索 `-I` / `-isystem`，不搜索当前目录。
- 支持 `#include_next`、`__has_include`、宏展开后的 quote/angle include。
- 支持 Windows 反斜杠路径和 `<a//b.h>` 形式的 header name。
- Windows x64 MinGW-w64 UCRT64 GCC 15.2.0 硬编码宏 profile。
- 常用 `__has_builtin` / `__has_attribute` / `__has_cpp_attribute`。
- `#pragma once`、`push_macro/pop_macro`、`message`、`GCC system_header`。
- CMake/Ninja depfile：`-MD/-MMD`、`-MF/-MT/-MQ/-MP`。
- 物理行号映射，支持 splice 后的 `__LINE__` 和诊断。
- Unicode 路径、静态 Windows 可执行文件。

### 已验证

- 31 个 CTest。
- `<vector>`、`<string>`、`<iostream>`、`<filesystem>`、`<chrono>`、`<format>`、`<ranges>` 等标准库头。
- `<windows.h>` / MinGW Windows SDK。
- CMake + Ninja 标准头项目和增量 depfile。

### 已知限制

- 只保证 Windows x64 + MinGW-w64 UCRT64 + GCC 15.2.0 + C++20。
- 不自动搜索 sysroot；标准库头路径必须显式传入。
- 不支持 PCH、modules、`#embed`。
- `__has_*` 是常用集合，不是 GCC 完整表。
- 尚未覆盖真实多项目 corpus、fuzz/ASan、CI、安装包。
- 当前仓库未附带第三方依赖；`GccProfileData.inc` 为 GCC 15.2 宏数据。
