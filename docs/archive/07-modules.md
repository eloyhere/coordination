# 07 · 模块划分

编译器按“可独立演进、单向依赖”的原则拆成下列模块。当前大多数是头文件接口
（`libs/coord/include/coord/modules/`），核心流水线仍由 `support/syntax/ast/
parser/sema/interp/codegen/driver` 实现；`Compiler` 是它们的门面。

## 1. 前端

| 模块 | 文件 | 职责 | 状态 |
| --- | --- | --- | --- |
| `Identifier` | `modules/identifier.h` | 标识符驻留（id ↔ text），供符号表/索引复用 | ✅ |
| `Syntax` | `syntax.h` | TokenKind、Token | ✅ |
| `Lexer` | `syntax.h` / `src/lexer.cpp` | Unicode 源码 → Token | ✅ |
| `AbstractSyntaxTree` | `ast.h` / `modules/abstract_syntax_tree.h` | AST 节点、`AbstractSyntaxTree = ModuleAst` | ✅ |
| `Parser` | `parser.h` | 递归下降 + Pratt | ✅ |
| `Sema` | `sema.h` / `modules/compiler.h` | 名字/类型/继承/Scope/RAII 检查；`using Sema = Checker` | ✅ |
| `Operator` | `modules/operator.h` | 运算符优先级、结合性、可重载性 | ✅ |
| `Scope` | `modules/scope.h` | 通用 Scope 栈（每个 `{}` 一层） | ✅ 通用版 |
| `Access` | `modules/access.h` | public/private/protected/default | ✅ 已接入 Parser/Sema/Codegen |
| `Modifier` | `modules/modifier.h` | static/virtual/override/final/const/abstract 集合 | ✅ 类型；Parser 仍按独立标志解析 |
| `Class` / `Method` / `Function` / `Variable` | `modules/symbols.h` | 面向索引/工具链的符号描述符 | ✅ 描述层 |

## 2. 模块、包、工程

| 模块 | 文件 | 职责 | 状态 |
| --- | --- | --- | --- |
| `Module` / `ModuleManager` / `ModuleScanner` | `modules/module.h` | `.coo` 物理模块发现、名字、注册 | ✅ 头文件实现 |
| `Package` / `PackageManager` / `PackageScanner` | `modules/package.h` | `coordination.toml` 包发现、模块聚合 | ✅ 头文件实现 |
| `Project` / `ProjectManager` / `ProjectScanner` | `modules/project.h` | 向上查找工程根/清单 | ✅ 头文件实现 |
| `Dependency` / `DependencyManager` / `DependencyScanner` | `modules/dependency.h` | `[dependencies]` 解析、拓扑排序、环检测 | ✅ 头文件实现 |

这些模块尚未接入 `coorc build` 的工程模式（P5）；当前 CLI 仍是单文件。

## 3. 运行时与文本

| 模块 | 文件 | 职责 | 状态 |
| --- | --- | --- | --- |
| `Unicode` | `modules/unicode.h` | 码点校验、UTF-8 编解码、BOM、字符分类、标识符规则 | ✅ |
| `Character` | `modules/character.h` | 语言 `char` 的宿主模型（Unicode 标量） | ✅ |
| `Charset` | `modules/charset.h` | UTF-8/16/32 + BOM 解码为内部 UTF-8 | ✅ |
| `File` | `modules/file.h` | UTF-8 文件读写/存在性；Stream 模式按行/字节偏移读写 | ✅ |
| `Stream` | `modules/stream.h` | 跨平台文本流：LF 规范化、`readByStream`/`writeByStream`、`readByLine`/`writeByLine`、`readByByteOffset`/`writeByByteOffset` | ✅ |
| `Thread` | `modules/thread.h` | RAII 线程边界 | ✅ |
| `Atomic` | `modules/atomic.h` | `Atomic<T>` 与常用别名 | ✅ |

## 4. 编译、链接、求值

| 模块 | 文件 | 职责 | 状态 |
| --- | --- | --- | --- |
| `Compiler` | `modules/compiler.h` | 调用驱动流水线（Lex→Parse→Sema→Eval→Lower→Link） | ✅ 门面 |
| `Evaluator` | `modules/compiler.h` | 包装 `Interpreter`，编译期求值 | ✅ 门面 |
| `Linker` | `modules/compiler.h` | 宿主 C++ 编译器/链接器边界 | ✅ 门面 |
| `Driver` | `driver.h` / `src/driver.cpp` | 生命周期、产物、`--emit-cpp`/`build`/`run` | ✅ |

## 5. 依赖方向

```
Identifier / Access / Modifier / Operator / Scope / Unicode
        ↓
Syntax → Lexer → AST → Parser → Sema → Evaluator → Compiler → Linker
        ↓
Module/Package/Project/Dependency（独立工具链层，使用 File/Scope）
        ↓
coorc CLI
```

- 前端不依赖包管理；扫描器不依赖类型检查器。
- `modules/*` 默认头文件实现，避免为工具链层引入额外链接单位。
- `Compiler`/`Evaluator`/`Linker` 是稳定门面；内部实现可逐阶段迁移。

## 6. Unicode 与 BOM 契约

- 源文件编码：UTF-8、UTF-16LE/BE、UTF-32LE/BE；BOM 可选。
- 内部统一转 UTF-8；`Span` 偏移是 UTF-8 字节偏移。
- 字符串/字符字面量是 Unicode；`char` 是 32 位标量。
- 标识符允许 Unicode 字母/数字/下划线（简化 XID 规则，见
  `modules/unicode.h`）；关键字仍为 ASCII。
- 非法码点替换为 U+FFFD，不中断词法分析。

## 6.1 Stream 与换行契约

- 内部逻辑文本使用 LF（`\n`）；读取时把 CRLF/CR 统一为 LF。
- 写回一律写 LF，因此行号/字节偏移在所有平台上一致；一致性由编译器保证。
- 行号 0 基；`readByLine` 返回不含换行的行内容。
- 字节偏移是规范化 UTF-8 文本的字节偏移，不是原始文件字节。
- `readByByteOffset(offset)` 返回到文件末尾的文本；
  `writeByByteOffset(offset, content)` 从 offset 覆盖写入，越界追加。
- `writeByLine(index, content)` 只替换已存在的行；content 不得含换行。
- `readByStream()` / `writeByStream()` 读写整段规范文本。
- `File` 的 Stream 方法：`openStream`、`readByLine`、`readByStream`、
  `readByByteOffset`、`writeByLine`、`writeByStream`、`writeByByteOffset`。

## 7. 尚未完成

- 扫描器/管理器接入 `coorc` 的工程模式；
- `Class/Method/Function/Variable` 描述符由 Parser 自动填充（目前手工构造）；
- `Operator` 表接入 Parser 的优先级判断；
- `super`、构造初始化列表与枚举方法/接口（P1 剩余）。
