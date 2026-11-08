# Coordination

[English](README.md) | **简体中文**

一门编译型的系统编程语言，以及用来构建它的工具链——目前以 C++20 自举中。

> **状态：pre-alpha。** 语言规范、架构文档与宿主实现都在推进中；语法、文件格式与内部 ABI 都可能随时调整，不另行通知。

## 项目简介

Coordination 是一门面向系统软件与工具链的 AOT 编译语言，目标平台为 Windows、Linux、macOS 上的 x86-64 与 arm64，交付形态是命令行工具链（`coord`），而不是 IDE 或图形安装程序。

四条既定规则驱动了其余全部设计：

| 规则 | 含义 |
| --- | --- |
| **R1** | 只做 AOT 编译——没有 JIT，没有字节码解释器 |
| **R2** | `interface` 与 `type` 只存在于编译期；interface 可多继承 interface，class 只能单继承 class |
| **R3** | 模板元编程是一等特性（`concept`、`comptime`） |
| **R4** | 没有 `#include`、没有预处理宏、没有 `namespace`；模块通过 `import` / `export` 暴露符号 |

设计原则是零成本抽象、错误尽量在编译期判定、依赖图静态可分析（因为没有宏，解析结束时 import 图即已完整），以及把跨平台当作一等公民，而不是为每个平台维护一份源码。

仓库当前存放的是 **stage-0 宿主实现**：C++20 写成的数据结构库与前端，将来会被自举的工具链替换；同时还包含语言规范及其背后的设计记录。

## 目录结构

| 路径 | 内容 |
| --- | --- |
| `structure/` | 宿主 C++ 库——树节点、二叉搜索树 / AVL 树 / 红黑树、跳表、LRU 缓存、对象池 |
| `syntax/` | 语言语法树的 C++ 原型：节点、token、span 与打印器 |
| `compiler/` | 前端——源文件管理器、词法分析、语法分析、AST、结构化诊断，以及把流程串起来的 driver |
| `tests/` | 各组件的 CTest 测试，包含三套树实现之间的差分模糊测试 |
| `cmake/` | 共享的 CMake 辅助脚本（`CompilerWarnings.cmake`） |
| `docs/` | 语言规范、架构、流程、生命周期与调研资料 |

## 环境要求

- CMake 3.21 或更新版本；仓库自带的预设需要 3.25 或更新版本
- 对 C++20 支持完整的编译器——MSVC 2022、GCC 11 或更新版本、Clang 13 或更新版本
- Ninja，所有预设都用它作为生成器
- Windows 上还需要 Visual Studio 开发者环境：`cl.exe` 在 `PATH` 中，且已设置 `VSINSTALLDIR`

## 构建

预设覆盖了所有受支持的平台。每个预设配置到 `out/build/<preset>`，安装到 `out/install/<preset>`。

```sh
cmake --preset x64-debug
cmake --build --preset x64-debug
```

| 平台 | 预设 |
| --- | --- |
| Windows | `x64-debug`、`x64-release`、`x86-debug`、`x86-release` |
| Linux | `linux-debug`、`linux-release` |
| macOS | `macos-debug`、`macos-release` |

也可以不用预设，直接做源码外构建：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

两种方式都会由 `coordination_demo` 目标产出示例程序 `coordination`。

## 运行测试

```sh
ctest --preset x64-debug
```

每个 `tests/test_*.cpp` 都会被自动收集，生成同名可执行文件与同名 CTest 测试；`CONFIGURE_DEPENDS` 会在文件增删后重新执行 glob。用例通过时只打印一行 `PASS: <名称>`，失败则直接指出出错的表达式。

## 作为库使用

这个库是纯头文件库。`coordination::coordination` 接口目标带上了头文件搜索路径（仓库根目录）与 C++20 要求，因此按从根目录算起的相对路径包含即可：

```cpp
#include "structure/tree/AVLTree.h"

#include <iostream>

int main()
{
    coordination::tree::AVLTree<int> tree{};
    tree.insert(3);
    tree.insert(1);
    tree.insert(2);

    std::cout << tree.size() << '\n';      // 3
    std::cout << *tree.min() << '\n';      // 1
    std::cout << tree.validate() << '\n';  // 1
}
```

在 CMake 工程里可以直接把源码放进来：

```cmake
add_subdirectory(third_party/coordination)
target_link_libraries(my_app PRIVATE coordination::coordination)
```

也可以借助 `FetchContent`：

```cmake
include(FetchContent)

FetchContent_Declare(coordination
    GIT_REPOSITORY https://github.com/eloyhere/coordination.git
    GIT_TAG main)

FetchContent_MakeAvailable(coordination)

target_link_libraries(my_app PRIVATE coordination::coordination)
```

## 安装

```sh
cmake --install out/build/x64-debug --prefix /some/prefix
```

安装的内容包括 `structure/`、`syntax/` 下的头文件，以及导出到 `lib/cmake/coordination/coordinationTargets.cmake` 的 `coordination::` 目标。目前还不会生成 `coordinationConfig.cmake`，所以 `find_package(coordination)` 暂时不可用——在补上之前请直接包含导出的 targets 文件。

## 语言初览

```coordination
// 一个最小模块。
export class Point {
    mutable x: integer(32);
    mutable y: integer(32);

    init(x: integer(32), y: integer(32)) {
        this.x = x;
        this.y = y;
    }

    lengthSquared(): integer(32) {
        return this.x * this.x + this.y * this.y;
    }
}
```

这个时候值得记住三条约定：标识符里不出现下划线（类型用大驼峰，其余用小驼峰），语句一律以 `;` 结束、换行不参与语法，函数声明不带 `fn` 关键字。完整规则见语言规范。

## 文档

| 文档 | 内容 |
| --- | --- |
| [docs/language-spec.md](docs/language-spec.md) | 规范草案——类型、interface、class、泛型、模块 |
| [docs/architecture.md](docs/architecture.md) | 分层、关键决策、前端、IR、后端与工具链组件 |
| [docs/process.md](docs/process.md) | 角色、变更分级、RFC 流程、测试与发布流程 |
| [docs/lifecycle.md](docs/lifecycle.md) | 版本模型、发布通道、兼容性章程 |
| [docs/research/](docs/research/) | 影响设计取舍的调研资料 |

这些文档目前均为中文，规范稳定后会补上英文版本。

## 路线图

| 阶段 | 主要交付物 |
| --- | --- |
| MVP（0–6 个月） | driver、SourceManager 与 span、词法与语法分析、`import`/`export`、名称解析与类型检查、HIR、基础单态化、最小 `comptime`、Windows / Linux 上的 LLVM x86-64 后端、`build` / `run` / `check` |
| 自举（6–18 个月） | MIR 与优化、增量缓存、包与锁文件、`fmt`、`lint`、LSP、aarch64 / macOS / WASM、PDB、C ABI、标准库 |
| 生态（18–36 个月） | 注册表与签名、文档生成、C 互操作、LTO 与 PGO、sanitizer、远程缓存、稳定的 1.0 规范 |

## 参与贡献

项目还处在早期，暂不适合大规模的外部贡献，但欢迎提 issue 与参与设计讨论。[`docs/process.md`](docs/process.md) 说明了变更如何分级与评审，上面的构建与测试命令就是任何改动都要过的门禁。

## 许可证

目前尚未选定许可证，因此代码暂保留所有权利。首个公开发布版本之前会确定许可证。
