# CrucibeCpp Preprocessor V1.0.1

补丁版本：修复生成 `.i` 文件中大量空行的问题。

## 修复内容

- 不再为每个源文件行输出一个空行。
- 默认模式在文本块前输出精确行标记，GCC 诊断行号仍然正确。
- `-P` 模式也使用紧凑输出。
- 空行只在未闭合的宏参数内部保留，避免拆断跨行宏调用。

## 效果

用户 `main.cpp`（包含 `<iostream>`、`<windows.h>`、`<vector>` 等）：

```text
修复前：约 932,522 行，空行约 804,028（86.2%）
修复后：约 150,804 行，空行约 659（0.44%）
```

生成文件仍然可以由 GCC 直接编译：

```powershell
g++ -std=c++20 -O2 main.i -o main.exe
```

## 兼容性

- Windows x64
- MinGW-w64 UCRT64 GCC 15.2.0
- C++20
- 不自动搜索 sysroot；标准库头路径仍需显式传入

## 验证

- 31/31 CTest 通过。
- `<vector>`、`<string>`、`<iostream>`、`<filesystem>`、`<windows.h>` 等真实头文件通过。
- 用户 `main.cpp` 预处理 0 错误，GCC 编译/链接 0 错误。

## 许可证

MIT，见 `LICENSE`。
