# Windows UCRT64 GCC 15.2 预处理器：目标、现状与剩余差距

> 当前工程：[cruciblecpp-preprocessor/](cruciblecpp-preprocessor/)  
> 当前版本：**V2.1.0**
>
> 兼容矩阵：**Windows x64 + MinGW-w64 UCRT64 + GCC 15.2.0 + C++20**

---

## 0. 目标

做一个在 Windows 上替代 `g++ -E` 的独立预处理器：

- 支持 `#include "..."` 和 `#include <...>`；
- 从 `PATH` 查找 GCC 并自动读取其隐式 include 搜索列表；用户或 CMake 仍可显式传 `-I` / `-isystem`；
- 兼容 GCC 15.2 的预定义宏、pragmas、`__has_*`；
- 输出 GCC 可直接编译的 `.i`；
- 接入 CMake/Ninja 的 depfile；
- 只保证 Windows x64 + UCRT64 GCC 15.2.0 + C++20。

明确不保证：

- Linux/macOS；
- MSVC/Clang；
- MSVCRT 版 MinGW、32 位 GCC；
- 其他 GCC 小版本；
- GCC PCH 二进制格式的解析/直接消费；PCH 的 `ignore/warn/error/report` 策略和外部 `.gch` artifact 缓存已支持；
- modules、`#embed`；
- 任意 sysroot；自动发现只针对 `PATH` 里的 GCC。

---

## 1. 当前完成度

| 维度 | 当前状态 | 判断 |
|---|---|---|
| `""` / `<>` include | 都已支持；`<>` 不查当前目录 | 90% |
| 搜索路径 | `-I` / `-isystem` + PATH 中 GCC 自动发现 | 95% |
| GCC 15.2 预定义宏 | 硬编码 profile | 90% |
| `__has_include` | quote/angle 都支持；宏展开后二次处理 | 90% |
| `#if` 字符常量 | 普通/多字符/L/u/U/u8/UCN，按 GCC 15.2 Windows x64 | 90% |
| charset | `-finput-charset` / `-fexec-charset` / `-funsigned-char` | 85% |
| `__has_builtin` / `__has_attribute` | GCC 15.2 源码 + 编译器生成的完整表 | 100% |
| `__has_cpp_attribute` | 标准属性 + `gnu::` 完整表 | 100% |
| pragma | `once`、`push/pop`、`message`、`system_header`、未知透传 | 70%–80% |
| 标准库头 | `<vector>`、`<string>`、`<iostream>`、`<filesystem>`、`<chrono>` 等已验证 | 85% |
| CMake depfile | `-MD/-MF/-MT/-MQ`，含 `__has_include` 探测，Ninja 增量实测 | 95% |
| PCH 策略/缓存 | `.gch` 策略、artifact 缓存、并发锁、失效/清理/统计、PCH depfile | 90% |
| PCH 二进制 | 不解析 GCC `.gch` 内容 | 0%（有意不实现） |
| 行号/诊断 | splice 物理行映射、源码行/caret、include trace | 90% |
| header-name 词法 | 直接 quote/angle；反斜杠/`//` 已支持并有测试 | 90% |
| 真实项目 corpus | 只有小型标准头测试 | 30% |
| fuzz / 资源上限 / 安全 | 基础限制，无 fuzz | 30% |
| CI / 发布 / 许可证 | GitHub Release、exe/zip、LICENSE、CHANGELOG 已具备；CI 未完成 | 60% |

综合：**约 85%** 完成“Windows UCRT64 GCC 15.2 受控项目”的生产目标。

---

## 2. 已实现能力

### 2.1 include

- `#include "x.h"`：当前文件目录 → `-I` → `-isystem` → PATH 中 GCC 的 include 目录；
- `#include <x.h>`：`-I` → `-isystem` → PATH 中 GCC 的 include 目录，不查当前目录；
- 宏展开成 quote/angle 都支持；
- `#include_next`；
- `__has_include` / `__has_include_next`；
- contextual header-name lexer：
  - 支持 `<C:\foo\bar.h>`；
  - 支持 `<a//b.h>`；
  - 支持带空格的 header name；
  - 注释夹在 `#include` 与 header name 之间仍是边界；
- depfile 记录 quote/angle 头，也记录 `__has_include` / `__has_include_next` 探测到的头。

### 2.2 GCC 15.2 profile

- `src/GccProfileData.inc` 硬编码；
- `__GNUC__`、`_WIN32`、`__MINGW64__`、`__x86_64__`、`__SIZEOF_*`、`__FLT_*`、`__cpp_*` 等；
- `__cplusplus` 由 `-std` 控制；
- `-D` 可覆盖 profile 宏；源码级重定义报错；
- `tools/generate_gcc_profile.ps1` 可重新生成。

### 2.3 `__has_*`

- `__has_include` / `__has_include_next`；
- `__has_builtin` 完整 GCC 15.2 builtin/type-trait 表；
- `__has_attribute` 完整 GCC 15.2 attribute 表；
- `__has_cpp_attribute` 完整标准属性 + `gnu::` 表；
- `__has_feature` / `__has_extension` 完整 C++20 语言特性表；
- `#ifdef __has_builtin` 等与 GCC 15.2 对齐；
- 宏展开产生的 `__has_*` 会二次求值（libstdc++ 的 `_GLIBCXX_HAS_BUILTIN` 链依赖这个）。

### 2.4 pragma

- `#pragma once`；
- `#pragma push_macro/pop_macro`；
- `#pragma message`；
- `#pragma GCC system_header` 被消费，不写进 `.i`；
- `#pragma GCC diagnostic` 及其他未知 pragma 透传；
- `_Pragma`。

### 2.5 CMake depfile

- `-MD/-MMD`；
- `-MF`、`-MT`、`-MQ`、`-MP`；
- 记录主文件、forced include、quote/angle include；
- 路径去重、空格/# 转义；
- 目标路径统一 `/`；
- 已用 CMake + Ninja `add_custom_command(DEPFILE ...)` 实测增量构建；
- CMake 仍可把 `CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES` 作为 `-isystem` 显式传入；不传时自动发现。

### 2.6 输出

- GNU 行标记 `# <line> "<file>"`，GCC `.i` 直接接受；
- `-P`、`-C`；
- Unicode 路径；
- token 间隔安全；
- 输出文件临时文件 + 重命名，带 Windows 短暂占用重试。

### 2.7 行号

- FileReader 记录 splice 偏移；
- Tokenizer 把逻辑行映射回物理行；
- `__LINE__` / 诊断使用物理行；
- `#line` 仍可覆盖。

---

## 3. 已验证的标准库头

已实际执行：

```text
预处理 -> g++ -std=c++20 -fsyntax-only
```

通过的头包括：

- `<cstddef>`、`<type_traits>`
- `<vector>`、`<string>`、`<iostream>`
- `<algorithm>`、`<memory>`、`<map>`
- `<filesystem>`、`<thread>`、`<regex>`
- `<chrono>`、`<format>`、`<print>`
- `<ranges>`、`<coroutine>`、`<expected>`、`<span>`
- `<bit>`、`<numbers>`、`<source_location>`
- `<syncstream>`、`<latch>`、`<semaphore>`、`<barrier>`、`<stop_token>`
- `<execution>`、`<numeric>`、`<random>`、`<locale>`
- `<sstream>`、`<fstream>`、`<iomanip>`、`<cstdio>`
- `<stacktrace>`、`<stdfloat>`
- `<windows.h>`（MinGW Windows SDK，包含宏展开产生的 `defined`）

这不是“所有标准头”的证明，但覆盖了主要模板/宏/pragma 路径。

---

## 4. 剩余主要差距

### 4.1 真实项目 corpus（P1，最高业务风险）

当前标准头测试是单翻译单元，不覆盖：

- 多目录 CMake 项目；
- 第三方 header-only 库；
- `#include_next` 长尾；
- 复杂 `__has_include` 条件；
- 大项目增量依赖。

**建议**：至少 3 个真实 UCRT64 GCC 项目，全部预处理 + GCC 编译 + 与 `g++ -E -P` token 差分。

### 4.2 header-name 剩余边界（P2）

- `#include /*comment*/ <...>`；
- `__has_include` 参数中的反斜杠/`//`；
- 宏展开产生的非常规 header name。

**建议**：把 raw header-name 扫描扩展到 `__has_include` 和注释跳过。

### 4.3 `__has_builtin` / `__has_attribute` 长尾（已完成）

- 已由 GCC 15.2 源码 + 编译器查询生成完整表；
- 作用域属性（`gnu::always_inline`）已支持；
- 表与 GCC 15.2 绑定，换版本需用 `tools/generate_gcc_has_data.py` 重新生成。

### 4.4 pragma 长尾（P1）

- `#pragma GCC diagnostic` 透传，但没有内部诊断状态；
- `#pragma GCC poison/dependency/target/optimize/visibility` 只是透传；
- system header 只做了“消费”，没有完整警告抑制模型。

### 4.5 健壮性 / 安全（P1）

- 有宏展开步数和 include 深度上限；
- 无 token/输出/内存/时间预算；
- 无 fuzz / ASan / UBSan；
- 顶层异常处理仍不完整；
- 输出文件仍存在 TOCTOU/并发写风险；
- PCH 缓存有目录锁和原子 rename，但未做 fsync/掉电持久性保证。

### 4.6 性能（P2）

- 宏展开前端插入有 O(n²) 风险；
- 无 include guard 缓存；
- 全量读入 + 全量输出缓冲；
- 标准头预处理约数秒，真实大项目可能偏慢。

### 4.7 CI / 发布 / 支持（P1）

- 仅本地 Windows 验证；
- 无 CI；
- 已有 GitHub Release、独立 exe 和 zip 包，但没有系统安装器；
- 已有 LICENSE/CHANGELOG、支持矩阵和故障排查说明。

---

## 5. 剩余里程碑

### M1：真实项目 corpus（1–2 周）

- 3 个真实项目；
- 与 GCC token 差分；
- 修复 corpus 问题。

### M2：pragma 收敛（1 周）

- builtin/attribute/feature 完整表已完成；
- 系统头诊断策略；
- `#pragma GCC diagnostic` 位置测试；
- `__has_include` raw header-name。

### M3：健壮性/安全（1–2 周）

- 顶层异常处理；
- 资源预算；
- fuzz + ASan/UBSan；
- 安全写文件；
- 并发/符号链接测试。

### M4：发布/性能（1 周）

- CI；
- 系统安装器；
- 真实项目 include guard 缓存/基准。

总剩余的主要工作是真实 corpus、fuzz/CI 和性能收敛，不是当前功能不可用。

---

## 6. 验收清单

- [x] `#include <...>` 可用，且无需手动 `-isystem`。
- [x] 从 PATH 自动发现 GCC include 目录。
- [x] `#include "..."` 可用。
- [x] `<>` 不搜索当前目录。
- [x] `__has_include(<...>)` 可用。
- [x] 标准库头（主要集合）预处理 + GCC 编译。
- [x] GCC 15.2 宏 profile。
- [x] 常用 `__has_builtin`/`__has_attribute`。
- [x] `#pragma GCC system_header` 不污染 `.i`。
- [x] CMake depfile + Ninja 增量。
- [x] 物理行号映射。
- [x] 直接 `#include <...>` 的反斜杠/`//` 支持。
- [ ] 3 个真实项目 corpus。
- [ ] fuzz/ASan/UBSan。
- [ ] CI/安装包/LICENSE。
- [ ] `__has_include` 极端 header-name 边界。

---

## 7. 结论

当前版本已经跨过“`<vector>` 能不能用”的门槛：

- 标准库头可以预处理并编译；
- `<>` 搜索、完整 `__has_*`、system_header、depfile 都已打通；
- 60 个 CTest 通过；
- CMake + Ninja 的标准头项目实测通过；
- 不传 `-isystem` 的自动发现标准头测试通过。

下一步最有价值的是：**真实项目 corpus + pragma 长尾 + fuzz/资源/CI**。
