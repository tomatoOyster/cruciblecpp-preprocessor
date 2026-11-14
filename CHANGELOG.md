# Changelog

## V2.6.3 — 2026-11-14

### 新增

- 实现 GCC 15.2 的 `#embed`：二进制嵌入、`limit`、`gnu::offset`、`prefix`、`suffix`、`if_empty`。
- 实现 `__has_embed`，返回 0/1/2（缺失/非空/空资源）。
- 增加 `--embed-dir`，支持引号、尖括号和 `--embed-dir` 搜索。
- 接入并跑通 GCC `g++.dg/cpp/embed-*` 用例。

### 测试基础设施

- 新增 `tests/differential/run_differential.py`：同一输入同时跑 CrucibleCpp 和 `g++ -E`，比较退出码、诊断行号/severity、token 流，并把两边的 `.i` 交给 GCC `-fsyntax-only`。
- 新增真实 corpus：标准库 `<vector>`、`<filesystem>`、`<windows.h>`、宏、include/`__has_include`、字符常量、trigraph 和错误路径。
- 修复差分测试暴露的问题：`__cplusplus` 缺少 `L` 后缀；`__has_builtin(__builtin_operator_new/delete)` 返回值应为 `201802`；`__has_cpp_attribute(__no_unique_address__)` 需要处理双下划线包裹。
- 接入 GCC 15.2 `g++.dg/cpp` testsuite（外部路径通过 `GCC_TESTSUITE_DIR` 配置）：当前 46 pass、0 fail、31 skip。
- 修复 GCC testsuite 暴露的 `/*` within comment 误报、shift 溢出 severity、未终止字符串 severity、`_Pragma` 缺括号、`#pragma pack` 误报、字符串化 raw string 换行、`#warning` C++23 pedantic 文案。
- 实现 UCN identifier 组合、合法性检查、规范化输出，以及 `#`/`##` 中的 UCN 拼写保留；接入并跑通 GCC 的 `ucn-*` / `ucnid-*` 用例。
- CTest 增加 `differential_corpus`，常规总数 63；配置 `GCC_TESTSUITE_DIR` 后额外运行 `gcc_testsuite_cpp`，总数 64。

## V2.6.1 — 2026-11-12

### 诊断系统升级

- 诊断输出增加 GCC 风格的源码行和 caret。
- 增加 include trace：`In file included from ...`。
- 宏重定义从 error 改为 GCC 行为：`warning: 'NAME' redefined`，并附 `note: this is the location of the previous definition`，新定义生效。
- `#error` / `#warning` 消息增加 `#error` / `#warning` 前缀。
- 缺失 include 改为 `fatal error: X: No such file or directory`。
- 未闭合条件改为 `unterminated #if`。
- 未知 preprocessing directive 改为 `invalid preprocessing directive #foo`。
- 增加 `-fdiagnostics-color=auto|always|never`、`-fno-diagnostics-color`、`-fdiagnostics-show-caret`、`-fno-diagnostics-show-caret`。
- 增加 `-W<name>` / `-Wno-<name>` / `-Werror=<name>` / `-Wno-error=<name>`。
- 实现 `#pragma GCC diagnostic push/pop/ignored/warning/error` 状态。
- 实现 `#pragma GCC system_header` 和 `-isystem` 头的默认 warning 抑制，支持 `-Wsystem-headers`。
- include trace 增加 include 所在行号。
- 新增 `#pragma GCC poison`（含 poisoned note）、`#pragma GCC dependency`（缺失 fatal、过期 warning）、`#pragma GCC target/optimize/visibility` 透传。
- 继续对齐条件指令文案：`'#else' without '#if'`、`'#elif' after '#else'`、`#if with no expression`、`unterminated argument list invoking macro 'F'`。
- 实现 `-Wundef`、`-Wunused-macros`、`-Wcomment`、`-Wtrigraphs`、`-Wdate-time`、`-Wexpansion-to-defined`、`-Wmultichar`、`-Wendif-labels`、`-Wbuiltin-macro-redefined`。
- 诊断 warning 追加 `[-W...]`，`-Werror`/`#pragma diagnostic error` 使用 `[-Werror=...]`；新增 `-f[no-]diagnostics-show-option`。
- 新增 `diagnostics` 集成测试，检查 caret、include trace、note、fatal、pragma 状态、system header、poison、dependency 和关闭 caret。
- 新增宏重定义正例，预处理输出继续交给 GCC 编译。
- CTest 为 62 个。

## V2.5.3 — 2026-11-12

### 新增

- 增加 `-trigraphs` 选项，支持 GCC 的 9 个 trigraph 序列翻译。
- trigraph 在字符集解码之后、换行规范化与反斜杠换行拼接之前处理，`??=include`、`??(`、`??)`、`??<`、`??>` 和 `??/` 拼接均可工作。
- 新增 `test_trigraph.cpp`，预处理输出会直接交给 GCC 编译。
- CTest 为 58 个。

## V2.5.2 — 2026-11-12

### 改进

- `__has_include` / `__has_include_next` 找到的头文件现在会写入 `-MD` depfile，即使该头只被探测、从未真正 `#include`，与 GCC 一致。
- `tests/test_include/main.h` 增加只探测不包含的 `probe.h` 回归用例。
- 新增 `compile_depfile` 测试，预处理出的 depfile 场景输出会直接交给 GCC 编译。
- CTest 为 56 个。

## V2.5.1 — 2026-11-12

### 修复

- `#if` 中 `u8'多字节'`：产生 GCC 相同的 error，同时取最后一个 UTF-8 code unit 参与求值。
- `L'非 BMP'`：产生 GCC 相同的 warning，并取 UTF-16 低 surrogate。
- `u'非 BMP'`：产生 GCC 相同的 error，并取 UTF-16 低 surrogate。
- 超过 32 位的 `\x`：保留低 32 位，按目标字符宽度截断并产生 warning，不再提前钳制成 `0xFFFFFFFF`。
- 新增 `char_edge_values` 集成测试；删除三个基于旧错误假设的负面测试。
- CTest 为 56 个。

## V2.5.0 — 2026-11-12

### 新增

- 从 GCC 15.2 源码和实际编译器生成完整的 `__has_*` 数据表：
  - 1699 个 `__has_builtin` builtin/type-trait；
  - 149 个 `__has_attribute` attribute；
  - 295 个 `__has_cpp_attribute` 标准/`gnu::` 属性及值；
  - 53 个 `__has_feature` / `__has_extension` C++20 特性。
- 支持 `gnu::` 作用域 C++ 属性，例如 `__has_cpp_attribute(gnu::always_inline)`。
- `__has_feature` / `__has_extension` 不再是常量 0。
- 新增 `tests/test_gcc_has.cpp`，CTest 增加到 56 个。
- 新增 `tools/generate_gcc_has_data.py`，可从 GCC 15.2 源码和编译器重新生成数据。

## V2.4.1 — 2026-11-12

### 修复

- PCH 缓存命中时更新 artifact 访问时间，LRU/天数清理现在按真实访问时间工作。
- 临时文件路径加入进程 ID，避免并发进程生成相同临时文件名。
- 删除未使用的缓存统计合并函数。
- 全量回归仍为 56/56 CTest 通过。

## V2.4.0 — 2026-11-12

### 新增

- PCH 产物缓存：`--pch-cache <dir>`，包含 artifact、manifest、depfile、选项 stamp、快照和统计目录。
- 基于头文件/`.gch`/快照内容哈希、编译器身份、编译选项和工具版本的缓存失效。
- 头文件新于 PCH、选项变化、编译器变化、空文件、错误 GCC PCH magic、内容/快照损坏检测。
- 目录锁并发控制、锁超时、陈旧锁回收。
- 原子写入、manifest 可复现、LRU/容量/天数清理和孤儿文件清理。
- 每次运行的 hit/miss/stale/corrupt/evicted/lock/bytes/hitRate 统计 JSON。
- PCH Make 依赖文件：PCH 依赖头文件、编译器、选项 stamp 和 manifest。
- CTest 增加到 56 个，新增缓存命中、失效、损坏、锁、清理、可复现和依赖测试。

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
