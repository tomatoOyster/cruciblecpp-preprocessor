# CrucibleCpp Preprocessor V2.1.0

## 新增

- 多文件预处理。
- 输入文件与输出文件按顺序对应：

```powershell
.\cruciblecpp-preprocessor.exe -P `
    main.cpp iii.cpp yingt.cpp `
    -o main.i iii.i yingt.i
```

- 支持重复 `-o`：

```powershell
.\cruciblecpp-preprocessor.exe -P `
    main.cpp iii.cpp `
    -o main.i -o iii.i
```

- 输入/输出数量不匹配时报命令行错误。
- 多文件模式暂不支持 `-MD` depfile；depfile 仍只支持单输入。

## `#if` 字符常量

按 GCC 15.2 / Windows x64 对齐：

- 普通 `'a'`、`\x`、`\ooo`、`\0`。
- 多字符常量：`'ab' == 0x6162`；超过 4 字节保留最后 4 字节并警告。
- `L` 为 16 位；非 BMP 字符报错。
- `u` / `U` / `u8`；`u8` 多字节报错。
- UCN `\u` / `\U` 先按码点求值再转执行字符集。
- 普通字符按 UTF-8 执行字符集，`'\x80'` 按 signed char 提升为 `-128`。

## 字符集与 char 符号

- `-finput-charset=UTF-8|UTF-16|UTF-16LE|UTF-16BE|GBK|CP936|CP1252|ISO-8859-1|ASCII|CP<n>`
- `-fexec-charset=...` 影响 `#if` 字符常量；字符串字面量暂不转码。
- `-funsigned-char` / `-fsigned-char`；`-funsigned-char` 定义 `__CHAR_UNSIGNED__`。

## 兼容性

- Windows x64
- MinGW-w64 UCRT64 GCC 15.2.0
- C++20
- 自动从 PATH 发现 GCC include 目录

## 验证

- 51 个 CTest 全部通过。
- 多文件三输入三输出 CTest 通过。
- 字符常量测试覆盖普通/多字符/宽/u/U/u8/UCN。
- charset 测试覆盖 UTF-8/GBK/Latin-1 输入、GBK 执行字符集、`-funsigned-char`。
- 生成的三个 `.i` 均可由 GCC 编译。

## 许可证

MIT，见 `LICENSE`。
