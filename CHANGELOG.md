# Changelog

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
