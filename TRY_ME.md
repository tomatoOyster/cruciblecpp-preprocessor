# 试用说明

## 可执行文件

```text
build\cruciblecpp-preprocessor.exe
```

这是 Windows x64 静态链接版本（不需要额外 MinGW DLL），版本 `2.1.0`，目标为 **MinGW-w64 UCRT64 GCC 15.2.0 + C++20**。

## 先跑两个示例

```powershell
cd cruciblecpp-preprocessor

.\build\cruciblecpp-preprocessor.exe --version

# 示例 1
.\build\cruciblecpp-preprocessor.exe -P examples\hello.cpp -o output\hello.i
g++ -std=c++20 output\hello.i -o output\hello.exe
.\output\hello.exe

# 示例 2
.\build\cruciblecpp-preprocessor.exe -P examples\advanced_macro.cpp -o output\advanced_macro.i
g++ -std=c++20 output\advanced_macro.i -o output\advanced_macro.exe
.\output\advanced_macro.exe
```

已经生成好的示例文件在 `output\` 下，也可以直接运行：

```powershell
.\output\hello.exe
.\output\advanced_macro.exe
.\output\std-demo.exe
```

`std-demo` 的源文件是 `examples/std_demo.cpp`，使用 `<iostream>`/`<vector>`/`<string>`/`<algorithm>`，已经过本工具预处理并由 GCC 编译。

## 处理自己的项目头（引号或尖括号）

```powershell
.\build\cruciblecpp-preprocessor.exe `
    -P `
    -I your\include `
    -o output\your_file.i `
    your_file.cpp
```

`#include "..."` 和 `#include <project/foo.h>` 都可以，只要路径在 `-I` / `-isystem` 中。

## 处理标准库头

现在会自动从 `PATH` 找 GCC，并读取它的 include 搜索路径。直接这样即可：

```powershell
.\build\cruciblecpp-preprocessor.exe -P -o output\std.i your_std_file.cpp
```

查看找到的目录：

```powershell
.\build\cruciblecpp-preprocessor.exe --print-gcc-includes
```

关闭自动发现：

```powershell
.\build\cruciblecpp-preprocessor.exe -nostdinc -P -o output\std.i your_std_file.cpp
```

如果 `PATH` 里没有 GCC，可以手动传 `-isystem`；CMake 集成示例见 `README.md` 的 “CMake 依赖图集成”。

## CMake 依赖文件

```powershell
.\build\cruciblecpp-preprocessor.exe `
    -P `
    -I your\include `
    -MD -MF output\your_file.d -MT output\your_file.i `
    -o output\your_file.i `
    your_file.cpp
```

`output\your_file.d` 可交给 CMake/Ninja 的 `DEPFILE`。

## 重要限制

- 只兼容 **Windows x64 + MinGW-w64 UCRT64 + GCC 15.2.0 + C++20**。
- 需要 `PATH` 中能找到 GCC；找不到时需手动传 `-isystem`。
- 硬编码 GCC 15.2 profile；换 GCC 版本、MSVCRT、32 位或 C++ 标准需要重新生成/调整 profile。
- 尖括号 header name 支持 `/` 和直接反斜杠路径；注释夹在 `#include` 与 header name 之间属于边界情况。
- 当前未实现 PCH、modules、`#embed`。
