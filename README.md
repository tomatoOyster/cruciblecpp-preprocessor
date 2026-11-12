# CrucibleCpp Preprocessor

> **V2.3.0** — Windows x64 独立 C++ 预处理器，自动发现 PATH 中的 GCC 头文件目录，直接替代 `g++ -E` 的预处理步骤。
>
> [下载 V2.3.0](https://github.com/tomatoOyster/cruciblecpp-preprocessor/releases/latest) · [Changelog](CHANGELOG.md)

一个用 C++20 实现的独立 C++ 预处理器，兼容矩阵锁定为：

> **Windows x64 + MinGW-w64 UCRT64 + GCC 15.2.0 + C++20**

它读取一个源文件，完成翻译阶段 1–4（文件读取、续行、预处理 token 化、预处理指令与宏展开），输出可交给该 GCC 继续编译的 `.i` 文本。

## 官方 VS Code 插件

官方插件：**[CrucibleCpp for VS Code](https://github.com/tomatoOyster/cruciblecpp-vscode)**

- 下载 VSIX：[GitHub Releases](https://github.com/tomatoOyster/cruciblecpp-vscode/releases/latest)
- 功能：预处理当前文件、只读 `.i` 预览、Problems 诊断、保存 `.i`、与 `g++ -E` diff、读取 `compile_commands.json`、查看 GCC include 路径。
- 安装：从 Release 下载 `.vsix` 后执行
  ```powershell
  code --install-extension path\to\cruciblecpp-vscode-0.4.1.vsix
  ```
- 插件需要本仓库构建的 `cruciblecpp-preprocessor.exe`；如果不在工作区 `build`/`bin` 中，在 VS Code 设置里配置 `cruciblecpp.executable`。
- 宏动画和 include 图需要 CrucibleCpp **2.2.0** 或更新版本。

## 预编译头（PCH）

GCC 的 `g++ -E` **不会使用** `.gch`；PCH 只在真正编译时使用。因此本预处理器对 PCH 的语义是：

- 默认忽略 `<header>.gch`，读取头文件源码并正常预处理。
- 不解析 GCC 的二进制 PCH 格式；该格式与 GCC 版本、目标平台和选项绑定。
- 如果只有 `.gch` 而没有头文件源码，会明确报错，行为与 `g++ -E` 一致。
- `-include` 强制包含的 `cmake_pch.hxx` 也按源码处理，兼容 CMake 的 PCH 布局。
- `.gch` 不会进入 depfile、include 图或宏 trace。

PCH 诊断策略：

```text
--pch=ignore    默认，静默忽略
--pch=warn      发现 .gch 时输出 warning
--pch=error     发现 .gch 时直接报错
--pch=report    在 --analysis-json 的 "pch" 数组中记录被忽略的 PCH
```

构建系统传入的 `-Winvalid-pch` / `-Wno-invalid-pch` 会作为兼容参数接受。

**范围声明：**

- 支持 `#include "..."` 和 `#include <...>`。
- 引号头：当前文件目录 → `-I` → `-isystem` → PATH 中 GCC 的 include 目录。
- 尖括号头：`-I` → `-isystem` → PATH 中 GCC 的 include 目录；**不搜索当前文件目录**。
- 启动时从 `PATH` 查找 `g++.exe` / `gcc.exe` / `c++.exe`，执行 `g++ -E -v` 获取其隐式 include 搜索列表；不需要手动传 `-isystem`。
- `-nostdinc` 可关闭自动发现；显式 `-I` / `-isystem` 仍然有效。
- 硬编码 GCC 15.2（C++20）预定义宏 profile；include 路径在运行时查询，宏 profile 不查询。
- CMake 仍可把 `CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES` 作为 `-isystem` 显式传入；现在这是可选的。
- 只针对 Windows x64 + MinGW-w64 UCRT64 + GCC 15.2.0 + C++20；不支持其他 ABI/GCC 版本/Linux/MSVC/Clang。
- 提供 CMake/Ninja 可用的 depfile（`-MD/-MF/-MT`）。

项目不依赖第三方库，核心代码只在标准库之上实现（`GccProfileData.inc` 是从 GCC 15.2 生成并硬编码的数据）。

快速试用见 **[TRY_ME.md](TRY_ME.md)**；完整目标、现状和剩余差距见 **[GCC_COMPAT_TARGET.md](GCC_COMPAT_TARGET.md)**。

## 功能

- 文件读取：
  - UTF-8 BOM 处理、CRLF/CR 归一化为 LF；
  - 反斜杠续行（line splicing）；
  - raw string 内容不参与续行；
  - 最后一行缺少换行时补全并给出 warning。
- 预处理 token 化：
  - 标识符、pp-number、字符串/字符字面量、raw string、用户定义字面量后缀；
  - 字符串前缀 `u8`、`u`、`U`、`L`；
  - 注释、digraph（`<%`、`%>`、`<:`、`:>`、`%:`、`%:%:`）；
  - 最长匹配标点；
  - 通用字符名（UCN）和 UTF-8 标识符。
- 宏：
  - 对象宏、函数宏、空参数、可变参数宏；
  - 标准 hide set / blue paint 语义；
  - 实参预展开；
  - `#` 字符串化、`##` token 粘贴、placemarker；
  - C++20 `__VA_OPT__`；
  - GNU 扩展：`, ## __VA_ARGS__` 逗号消除、命名可变参数。
- 条件编译：
  - `#if`、`#ifdef`、`#ifndef`、`#elif`、`#else`、`#endif`；
  - C++23 的 `#elifdef`、`#elifndef`、`#warning`（作为扩展也可在较早标准下使用）；
  - `defined`、`__has_include`、`__has_include_next`、`__has_cpp_attribute`；
  - `__has_builtin`、`__has_attribute`、`__has_feature`、`__has_extension`（GCC profile 兼容集合）；
  - 硬编码 GCC 15.2 预定义宏（`__GNUC__`、`_WIN32`、`__x86_64__`、`__SIZEOF_*`、`__cpp_*` 等）；
  - 整数常量、优先级、短路求值、除零诊断；
  - `#if` 字符常量：普通、`L`、`u`、`U`、`u8`，多字符常量、UTF-8 源码字符、`\x`/`\ooo`/UCN；行为对齐 GCC 15.2 / Windows x64。
- 文件包含：
  - `#include "..."`：当前文件目录 → `-I` / `-isystem`；
  - `#include <...>`：`-I` / `-isystem`，不搜索当前文件目录；
  - 宏展开后的 quote/angle include；
  - `#include_next`；
  - `__has_include(<...>)` / `__has_include("...")`；
  - include 深度限制；
  - `#pragma once` 与 include guard 的语义处理（guard 不做额外缓存优化）；
  - `-MD/-MMD` + `-MF/-MT/-MQ` 生成 CMake/Ninja depfile；
  - 已用真实 libstdc++ 头（`<vector>`、`<string>`、`<iostream>`、`<filesystem>` 等）验证。
- 特殊指令：
  - `#line`、`#error`、`#warning`、空指令 `#`；
  - `#pragma once`、`push_macro`、`pop_macro`、`message`；
  - 未知 pragma 默认原样透传；
  - `_Pragma`，包括在宏中生成 pragma。
- 预定义宏：
  - `__FILE__`、`__LINE__`、`__DATE__`、`__TIME__`、`__TIMESTAMP__`；
  - `__cplusplus`、`__STDC_HOSTED__`、`__STDCPP_DEFAULT_NEW_ALIGNMENT__`；
  - `__COUNTER__`、`__INCLUDE_LEVEL__`、`__BASE_FILE__`。
- 输出：
  - 规范化 token 间隔，避免输出后 token 粘连；
  - 默认输出 GNU 风格行标记 `# <line> "<file>"`，`-P` 可关闭；
  - 支持保留注释（`-C`）；
  - 支持 stdout 或原子替换写入 `-o` 文件。
- 诊断：
  - `文件:行:列: error/warning/note`；
  - include 栈；
  - 最大错误数量；
  - 非零退出码。

## 目录结构

```text
cruciblecpp-preprocessor/
├── README.md
├── CMakeLists.txt
├── Makefile
├── include/
│   ├── Preprocessor.h
│   ├── Token.h
│   ├── Charset.h
│   ├── FileReader.h
│   ├── GccDiscovery.h
│   ├── Tokenizer.h
│   ├── MacroRegistry.h
│   ├── MacroExpander.h
│   ├── ConditionalEngine.h
│   ├── IncludeHandler.h
│   ├── SpecialOperator.h
│   └── OutputWriter.h
├── src/
│   ├── main.cpp
│   ├── Preprocessor.cpp
│   ├── Token.cpp
│   ├── Charset.cpp
│   ├── FileReader.cpp
│   ├── GccDiscovery.cpp
│   ├── Tokenizer.cpp
│   ├── MacroRegistry.cpp
│   ├── MacroExpander.cpp
│   ├── ConditionalEngine.cpp
│   ├── IncludeHandler.cpp
│   ├── SpecialOperator.cpp
│   ├── OutputWriter.cpp
│   └── GccProfileData.inc   # 从 MinGW GCC 15.2 生成的硬编码宏 profile
├── tests/
│   ├── CMakeLists.txt
│   ├── check_depfile.cmake
│   ├── test_simple.cpp
│   ├── test_function_macro.cpp
│   ├── test_conditional.cpp
│   ├── test_include/
│   │   ├── main.h
│   │   └── sub.h
│   ├── test_pound_ops.cpp
│   ├── test_gcc_profile.cpp
│   ├── test_character_constants.cpp
│   ├── test_charset_latin1.cpp
│   ├── test_charset_gbk.cpp
│   ├── test_charset_exec_gbk.cpp
│   ├── test_charset_unsigned.cpp
│   ├── test_standard_headers.cpp
│   ├── test_windows_header.cpp
│   ├── test_angle_header_name.cpp
│   └── negative/
│       └── ...（应失败的输入，包括缺失的尖括号头）
├── tools/
│   └── generate_gcc_profile.ps1
├── examples/
│   ├── hello.cpp
│   └── advanced_macro.cpp
└── output/
    └── .gitkeep
```

## 构建

### CMake（推荐）

需要 CMake 3.20+、支持 C++20 的编译器，以及可用的构建工具（Ninja、Make 或 MSVC）。

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

在 Windows + MinGW 下：

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure
```

如果环境没有 Ninja/Make，也可以直接用 g++ 编译：

```powershell
g++ -std=c++20 -Wall -Wextra -Iinclude `
    src/Charset.cpp src/Token.cpp src/FileReader.cpp src/GccDiscovery.cpp src/Tokenizer.cpp `
    src/MacroRegistry.cpp src/MacroExpander.cpp `
    src/ConditionalEngine.cpp src/IncludeHandler.cpp `
    src/SpecialOperator.cpp src/OutputWriter.cpp `
    src/Preprocessor.cpp src/main.cpp `
    -lshell32 -o cruciblecpp-preprocessor.exe
```

`-lshell32` 仅 Windows 需要，用于读取 Unicode 命令行参数。

### GCC 15.2 profile

`src/GccProfileData.inc` 是从 MinGW GCC 15.2（C++20）生成的硬编码预定义宏 profile。换 GCC 版本或目标标准时需要重新生成：

```powershell
.\tools\generate_gcc_profile.ps1
```

脚本等价于：

```powershell
g++ -std=c++20 -dM -E -x c++ - > gcc_profile.txt
# 去掉 #define __cplusplus 行后，包装成 src/GccProfileData.inc 中的 raw string
```

当前实现会过滤 `__cplusplus`，由 `-std` 控制；profile 与 **GCC 15.2.0 / x86_64 / UCRT64 / C++20** 绑定。`-D` 可以覆盖 profile 宏，源码级重定义仍报错。

如果目标机器是 MSVCRT 版 MinGW、32 位 GCC、或其他 GCC 小版本，必须重新生成 profile；当前 profile 不保证与它们一致。

### Makefile

`Makefile` 是 CMake 的薄包装：

```bash
make
make test
```

## 使用

```powershell
# 输出到 stdout（默认带行标记）
.\build\cruciblecpp-preprocessor.exe examples\hello.cpp

# 输出到文件
.\build\cruciblecpp-preprocessor.exe -P examples\hello.cpp -o output\hello.i

# 添加 include 目录、定义宏
.\build\cruciblecpp-preprocessor.exe -I include -DDEBUG=1 input.cpp -o output\input.i

# 保留注释、关闭行标记
.\build\cruciblecpp-preprocessor.exe -C -P input.cpp -o output\input.i
```

编译输出的示例：

```powershell
g++ -std=c++20 -fsyntax-only output\hello.i
```

### 命令行选项

```text
-o, --output <file...>       输出文件；单文件可省略，多文件必须按输入顺序给出
-I, --include-dir <dir>      用户头文件目录（可重复）
-isystem <dir>               系统头文件目录（可重复；优先于自动发现）
-nostdinc                    关闭 PATH 中 GCC 的自动 include 发现
--print-gcc-includes         打印发现的 GCC include 目录并退出
-D, --define <name[=value]>  定义宏（可重复，按顺序应用）
-U, --undef <name>           取消宏（可重复，按顺序应用）
-include <file>              强制包含文件（可重复）
-P, --no-line-markers        不输出行标记
-C, --keep-comments          保留注释
-std=<c++17|c++20|c++23>     设置 __cplusplus（默认 c++20）
-finput-charset=<name>       源文件字符集（默认 UTF-8）
-fexec-charset=<name>        执行字符集，影响 #if 字符常量（默认 UTF-8）
-funsigned-char              普通 char 按无符号处理
-fsigned-char                普通 char 按有符号处理（默认）
--analysis-json <file>       输出 include 边和宏展开 trace 的 JSON
--pch=ignore|warn|error|report
                             PCH 策略（默认 ignore）
-MD, -MMD                    生成 CMake/Ninja depfile
-MF <file>                   依赖文件路径
-MT <target>                 依赖目标
-MQ <target>                 依赖目标（Make 转义）
-MP                          兼容 CMake（当前无额外动作）
-Werror                      警告视为错误
--strict                     未知指令/未知 pragma 视为错误
--drop-unknown-pragmas       丢弃未知 pragma
--no-output-on-error         发生错误时不写输出文件
--max-errors <n>             最大错误数（默认 20）
--max-include-depth <n>      include 最大深度（默认 200）
-v, --verbose                输出处理统计
-newyear                     输出距离下一个 1 月 1 日的天数并退出
--help                       帮助
--version                    版本
```

`-D` 也支持函数宏：

```powershell
.\build\cruciblecpp-preprocessor.exe -D "F(x)=x+1" input.cpp
```

### 多文件

输入在前、输出在后：

```powershell
.\build\cruciblecpp-preprocessor.exe -P `
    main.cpp iii.cpp yingt.cpp `
    -o main.i iii.i yingt.i
```

或重复 `-o`：

```powershell
.\build\cruciblecpp-preprocessor.exe -P `
    main.cpp iii.cpp `
    -o main.i -o iii.i
```

输入与输出数量必须一致；`-MD` depfile 目前只支持单输入。

`-D` / `-U` 按出现顺序应用。例如：

```powershell
-D FOO=1 -U FOO -D FOO=2
```

最终 `FOO` 为 `2`。

### 标准库头

启动时会从 `PATH` 查找 GCC，调用：

```text
g++ -std=c++20 -E -v -x c++ -
```

解析 `#include <...> search starts here:` 到 `End of search list.` 之间的目录，自动加入尖括号搜索路径。

因此直接这样即可：

```powershell
.\build\cruciblecpp-preprocessor.exe -P input.cpp -o output\input.i
```

查看实际发现的路径：

```powershell
.\build\cruciblecpp-preprocessor.exe --print-gcc-includes
```

关闭自动发现：

```powershell
.\build\cruciblecpp-preprocessor.exe -nostdinc -isystem D:/path/to/include input.cpp -o output\input.i
```

`-v` 也会打印使用的 GCC 路径和每个 include 目录。

CMake 仍可显式传 `CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES`，但不再是必需步骤。

### 退出码

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 预处理错误（诊断已输出） |
| 2 | 命令行、输入/输出文件错误 |

## CMake 依赖图集成

不要把本工具硬塞进 `CMAKE_CXX_COMPILER_LAUNCHER`；它是替代 `-E` 的独立预处理步骤。推荐用 `add_custom_command` + `DEPFILE`：

```cmake
# 可选：显式传 GCC 隐式 include 目录。
# 不传时，预处理器会从 PATH 自动发现。
set(CPP_PRE_INCLUDES)
foreach(dir IN LISTS CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
    list(APPEND CPP_PRE_INCLUDES -isystem "${dir}")
endforeach()

add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/main.i
    COMMAND cruciblecpp-preprocessor
            -I ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CPP_PRE_INCLUDES}
            -MD -MF ${CMAKE_CURRENT_BINARY_DIR}/main.i.d
            -MT ${CMAKE_CURRENT_BINARY_DIR}/main.i
            -o ${CMAKE_CURRENT_BINARY_DIR}/main.i
            ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    DEPFILE ${CMAKE_CURRENT_BINARY_DIR}/main.i.d
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    VERBATIM
)

add_custom_target(preprocess_main ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/main.i)
```

然后可以把生成的 `.i` 作为 GCC 的编译输入。依赖文件记录实际读取的 quote/angle include；修改任一头文件都会触发重新预处理。

## 设计说明

### 处理流程

```text
main
  -> Preprocessor
       -> FileReader      读文件、归一化、续行
       -> Tokenizer       字符 -> 预处理 token
       -> 逻辑行分组
            -> 指令：ConditionalEngine / MacroRegistry / IncludeHandler / SpecialOperator
            -> 普通文本：MacroExpander -> OutputWriter
       -> 输出文件
```

### 宏展开

`MacroExpander` 使用 hide set 语义：

- 宏替换产生的 token 带上“当前宏名”的隐藏集；
- 重新扫描时，隐藏集里的宏不会再次展开；
- 实参预展开与 `#`/`##` 的“原始实参”规则分开处理；
- `##` 使用 placemarker 表示空实参；
- `__VA_OPT__` 在替换前展开为内容或 placemarker。

这可以正确处理：

```cpp
#define A A
A                         // A，不递归

#define f(x) x
f(f(1))                   // 1

#define g(x) g(x)
g(1)                      // g(1)，蓝漆

#define CAT(a, b) a##b
CAT(, x)                  // x
```

### 行号与诊断

- 源文件中的 `#line` 会改变后续 token 的 presumed line/file；
- 输出默认使用 GNU 行标记 `# <line> "<file>"`，因为 GCC 把 `.i` 当作已预处理文件，不重新执行 `#line`；
- `-P` 关闭行标记；
- 诊断中的 `文件:行:列` 使用 presumed file/line。

## 测试

当前包含 **54 个 CTest 用例**（正例会预处理后再用 GCC 编译，负例验证退出码）。

测试覆盖：

- 对象宏、`#undef`、重定义、预定义宏；
- 函数宏、嵌套展开、递归、空参数、可变参数、`__VA_OPT__`；
- `#`、`##`、placemarker、非法/空拼接；
- 条件编译、`defined`、`#elifdef`、表达式优先级、短路；
- include 搜索、include guard、`#pragma once`、`__has_include`；
- 尖括号 `<sub.h>`、Windows 反斜杠/双斜杠 header name、真实 libstdc++ 标准头（`<vector>`/`<string>`/`<iostream>` 等）和 `<windows.h>`；
- 不传 `-isystem` 的 PATH 自动发现标准头测试；
- 多文件三输入三输出测试；
- GCC 15.2 profile、`__has_builtin`/`__has_attribute`；
- `#line`、`#error`、`#warning`、pragma；
- CMake depfile（`-MD -MF -MT`）与依赖内容检查；
- 负例：未闭合条件、未闭合宏调用、缺失 quote/angle include、非法重定义、未闭合字符串、`-nostdinc` 下的标准头。

每个正例测试会先把 `.i` 输出编译一遍，确保输出是有效 C++。

## 已知限制

- 不实现 C++20 modules 的 `import`/`export` 语义；`import` 作为普通 token 输出。
- 不实现 C23 `#embed`（C++ 标准没有该指令）。
- 需要系统 `PATH` 中能找到 `g++.exe` / `gcc.exe` / `c++.exe`；找不到时标准库头无法自动解析，可手动传 `-isystem`。
- 自动发现只支持 Windows；其他平台会退化为显式 `-I` / `-isystem`。
- `-nostdinc` 关闭自动发现；显式路径仍然有效。
- 硬编码 GCC 15.2（C++20）宏 profile；换 GCC 版本或标准需要重新生成 profile。
- 不提供 `__has_*` 的完整 GCC 表；只覆盖常用 builtin/attribute 集合。
- 不实现 trigraph（C++17 已移除）。
- 不支持预编译头（PCH）。
- 未知 pragma 默认透传；未知指令默认 warning + 透传，`--strict` 时作为 error。
- include guard 通过正常 `#ifndef` 语义处理，未做“检测 guard 后跳过整个文件”的性能优化。
- 字符常量按 GCC 15.2 / Windows x64 实现：普通字符使用所选执行字符集，`wchar_t` 为 16 位。
- 支持 `-finput-charset` / `-fexec-charset` 的常用值：`UTF-8`、`UTF-16`、`UTF-16LE/BE`、`GBK`/`CP936`、`CP1252`、`ISO-8859-1`、`ASCII`、`CP<n>`；字符串字面量尚未按执行字符集转码，当前只影响 `#if` 字符常量。
- 支持 `-funsigned-char` / `-fsigned-char`；`-funsigned-char` 同时定义 `__CHAR_UNSIGNED__`。
- `u8` 多字节字符、`L`/`u` 的非 BMP 字符会报错；极端超过 32 位的 `\x` 溢出只做截断/警告，未逐位模拟 GCC 的全部诊断。
- 引号和尖括号 include 都支持 Windows 反斜杠路径；`#include /*comment*/ <...>` 这类“注释夹在关键字和 header name 之间”的组合仍是边界。
- depfile 记录实际处理的 quote/angle include，不记录 `__has_include` 探测。
- `#pragma GCC system_header` 会被消费，不写入 `.i`；系统头的完整警告抑制模型仍未实现。

## 许可证

MIT，见 [LICENSE](LICENSE)。发行说明见 [CHANGELOG.md](CHANGELOG.md)。

## 示例

```powershell
.\build\cruciblecpp-preprocessor.exe -P examples\advanced_macro.cpp -o output\advanced_macro.i
g++ -std=c++20 output\advanced_macro.i -o output\advanced_macro.exe
.\output\advanced_macro.exe
```
