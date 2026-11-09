# Coordination

跨平台编译型编程语言（无 UI，CLI 工具链），以及它的宿主 C++20 实现。

- 语言规范：[docs/language-spec.md](docs/language-spec.md)
- 编译器架构：[docs/architecture.md](docs/architecture.md)
- 仓库与工程结构：[docs/project-architecture.md](docs/project-architecture.md)
- 包管理器设计：[docs/package-manager.md](docs/package-manager.md)
- 开发流程与生命周期：[docs/process.md](docs/process.md)、[docs/lifecycle.md](docs/lifecycle.md)

## 目录

| 目录 | 内容 |
|---|---|
| `compiler/` | 编译器前端：Source、Diagnostic、Token、Lexer、AST、Parser、Sema、Dependency、Driver |
| `charset/` | Unicode：UTF-8/16/32 编解码、码点分类、BOM |
| `stdlib/` | 标准库 C++ 基础层：`file`（Range/Line/范围读写）、`text`（编码识别与转换）、`process`（子进程）、`thread`（线程池/阻塞队列） |
| `structure/` | 通用容器：树、LRU、对象池、节点 |
| `syntax/` | 早期语法树原型（保留作设计参考） |
| `tests/` | CTest 单测（`ctest --test-dir out/build/x64-debug`） |
| `docs/` | 规范、架构、流程、调研 |

## 构建

```sh
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --test-dir out/build/x64-debug --output-on-failure
```

需要 CMake ≥ 3.21、Ninja、MSVC / GCC / Clang（C++20）。

## 命令行

```sh
out/build/x64-debug/coordination version
out/build/x64-debug/coordination ast   examples/hello.cn   # 打印语法树
out/build/x64-debug/coordination check examples/hello.cn   # 解析 + 折叠 + 语义 + 环检测
out/build/x64-debug/coordination build examples/main.cn -o out/main.c   # 生成 C 源码
out/build/x64-debug/coordination run out/example_main.exe              # 执行并打印退出码
```

`check` 的退出码：0 无错误，1 有诊断（E1xxx 词法 / E2xxx 语法 / E4xxx 语义 / E5002 循环继承），2 用法错误。

## 约定

- 源码文件扩展名 `.cn`；模块接口缓存 `.cif`。
- 语言没有 `fn` / `let` / `var`；用 `const` / `mutable`，函数声明为 `name(params): Type`。
- 标识符禁止下划线，只允许 UpperCamel / lowerCamel / 单个小写单词。
- 语句以 `;` 结束。
- 标准库与编译器同版本发布，版本号单一来源。

## 许可证

Apache License 2.0，见 [LICENSE](LICENSE) 与 [NOTICE](NOTICE)。
