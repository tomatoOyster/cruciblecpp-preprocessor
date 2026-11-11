# CrucibleCpp Preprocessor V2.0.0

第二个公开发行版本。

## 新功能

- 从 `PATH` 自动发现 GCC：
  - `g++.exe`
  - `gcc.exe`
  - `c++.exe`
- 执行 `g++ -std=c++20 -E -v -x c++ -`，解析 GCC 的隐式 include 搜索列表。
- `#include <...>` 现在可以直接使用标准库头，不需要手动传 `-isystem`。
- `--print-gcc-includes` 打印发现结果。
- `-nostdinc` / `--no-gcc-includes` 关闭自动发现。
- `-v` 打印 GCC 路径、版本和 include 目录。

## 搜索顺序

```text
#include "..."
  当前文件目录
  → -I
  → -isystem
  → PATH 中 GCC 的 include 目录

#include <...>
  -I
  → -isystem
  → PATH 中 GCC 的 include 目录
```

`<>` 仍然不搜索当前文件目录。

## 示例

```powershell
.\cruciblecpp-preprocessor.exe -P main.cpp -o main.i
g++ -std=c++20 -O2 main.i -o main.exe
```

查看 GCC 路径：

```powershell
.\cruciblecpp-preprocessor.exe --print-gcc-includes
```

## 验证

- 34 个 CTest。
- 无显式 `-isystem` 的 `<vector>`/`<string>`/`<iostream>` 测试。
- `-nostdinc` 负例。
- `<windows.h>` 和用户 `main.cpp` 回归。
- 完整静态 Windows x64 发行包。

## 许可证

MIT，见 `LICENSE`。
