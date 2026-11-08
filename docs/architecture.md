# Coordination 项目架构 v0.1

> 本文描述**被实现的语言**（Coordination）及其工具链的架构；仓库当前的 C++20 代码是它的宿主实现（bootstrap 阶段）。
> 输入：`docs/language-spec.md`（语言规则）、`docs/research/toolchain-architecture.md`（调研与选型依据）。

## 1. 约束与非目标

| 约束 | 含义 |
|---|---|
| 编译型 AOT | 无 JIT、无字节码解释器；构建产物是原生可执行文件 / 静态库 / 动态库 / WASM |
| 无 UI | 只交付 CLI（`coord`）与 LSP（编辑器协议）；不做 IDE、不做图形安装器 |
| 跨平台 | 源码与工具链在 Windows / Linux / macOS、x86-64 / arm64 上行为一致 |
| 语言规则 | interface/type 仅编译期、class 单继承、模板元编程、无 include 宏与 namespace，只有 import/export |

**非目标（v1.0 前不做）**：GUI、稳定原生 ABI、全量运行时反射、默认 GC、包注册表之外的商业化服务。

## 2. 总体分层

```text
┌──────────────────────────────────────────────────────────────────┐
│ 驱动层  coord build|run|check|test|fmt|lint|doc|publish|lsp     │
│         TargetSpec · sysroot · linker · opt level · action graph │
└───────────────┬──────────────────────────────────────────────────┘
                ▼
┌──────────────────────────────────────────────────────────────────┐
│ 前端层  SourceManager → Lexer → Parser → AST/CST                 │
│         → 名称解析 → 类型检查 → HIR（typed, generic）            │
│         旁路：模块图 / import-export / .cif 接口缓存             │
└───────────────┬──────────────────────────────────────────────────┘
                ▼
┌──────────────────────────────────────────────────────────────────┐
│ IR 层   HIR → MIR（CFG + 清理边）→ MIR opt → 单态化 → CGU        │
│         comptime 解释器（MIR 上求值，带燃料配额）                │
└───────────────┬──────────────────────────────────────────────────┘
                ▼
┌──────────────────────────────────────────────────────────────────┐
│ 后端层  LLVM IR → LLVM opt → object（DWARF / PDB）               │
└───────────────┬──────────────────────────────────────────────────┘
                ▼
┌──────────────────────────────────────────────────────────────────┐
│ 链接层  lld（ELF / PE-COFF / Mach-O / wasm-ld）                  │
│         产物：exe / static lib / shared lib / wasm               │
└──────────────────────────────────────────────────────────────────┘
横切：查询式增量缓存 · 结构化诊断 · 依赖图 · 包与构建图 · LSP 服务
```

驱动层只回答"做什么、按什么顺序、调用谁"，不承担语义；语义全部在 HIR/MIR 之前完成。因为语言没有 `include` 与宏，`import` 图在解析后立即可得，不需要重放预处理状态——这是相对 C/C++ 工具链的结构性优势，也是增量缓存能做得干净的前提。

## 3. 仓库结构：现状与目标

| 目录 | 现状 | 目标 |
|---|---|---|
| `structure/` | 宿主 C++ 库：节点、BST/AVL/红黑树、LRU、跳表 | 编译器与运行时的内部数据结构；后续补 hash map、arena、bit set |
| `syntax/` | C++ 版语法树原型（SyntaxNode/Token/Span/Printer） | 作为前端设计验证；正式前端由 `coordination` 语言自身实现后替换 |
| `tests/` | CTest 驱动的 C++ 单测（含三棵树差分模糊测试） | 保留为宿主库测试；另建 `conformance/` 语言一致性测试 |
| `cmake/` `CMakeLists.txt` | 头文件库 + demo + CTest + install | 增加 `compiler/`、`runtime/`、`stdlib/` 目标；跨平台 CI |
| `docs/` | 规范、架构、流程、生命周期、调研 | 规范版本化，随 release 发布 |
| `compiler/`（规划） | — | driver、前端、IR、backend、诊断、缓存 |
| `stdlib/`（规划） | — | 用 coordination 写的标准库（dogfooding） |
| `runtime/`（规划） | — | panic/alloc/线程/时间等最小运行时 |
| `tools/`（规划） | — | fmt、lint、lsp、doc、包管理 |

宿主语言在 MVP 阶段固定为 C++20（当前工具链已就绪）；自举阶段用 coordination 重写标准库与至少一个前端组件，并以 stage2/stage3 输出一致作为验收。

## 4. 关键架构决策（ADR 摘要）

| ADR | 决策 | 理由 |
|---|---|---|
| ADR-001 | 发布级只维护 LLVM 一个后端；Cranelift 仅用于 `-O0`/REPL | 优化、目标覆盖、异常展开、DWARF/PDB 一次解决；双后端意味着双份 ABI 与测试矩阵 |
| ADR-002 | 泛型默认单态化，`dyn` 为显式擦除逃生门 | 零成本抽象；避免全量单态化导致编译时间与体积失控 |
| ADR-003 | `.cif` 接口文件携带泛型体（HIR/MIR），不只是签名 | interface/type 编译期消失，下游必须能独立实例化 |
| ADR-004 | `TargetSpec` 是一等公民，捆绑 lld | 三元组不足以描述 sysroot/ABI/链接器；可复现构建要求自带链接器 |
| ADR-005 | SourceId + 区间 + expansion frame 的 span；查询式增量 | import/模板实例/生成代码都会让"文件+偏移"失效 |
| ADR-006 | 唯一稳定 ABI 是 C 子集（`extern "C"` + `repr(C)`） | 原生 ABI 冻结代价过高；C 是跨语言最小公分母 |

## 5. 前端

### 5.1 SourceManager 与 span

- `SourceId` 标识源文件（含虚拟文件：生成代码、内联展开、`comptime` 产物）。
- `Span = SourceId + [start, end) + ExpansionFrame[]`；`ExpansionFrame` 记录"这段代码由哪个模板实例/生成点展开而来"。
- 行表按需构建，供诊断与 LSP 换算行列；不进入 AST 常驻内存。
- 当前 `syntax/SyntaxSpan.h` 只有 offset/length，MVP 第一件事就是升级为带 SourceId 的版本，否则跨文件诊断与增量缓存没有地基。

### 5.2 词法与语法

- 词法器保留 trivia（注释、空白），供 `coord fmt` 使用；token 带 span。
- 语法采用递归下降 + Pratt 表达式解析；语句以 `;` 结束，换行不参与语法。
- 解析器必须做错误恢复：同步到 `;`、`}`、模块边界，产出 CST 供格式化与 LSP 使用，同时产出压缩后的 AST 供语义分析。
- 没有宏，所以解析结果就是最终语法结构；`@cfg`、`@reflect`、`@derive` 都是 `annotation` 声明的标注应用（语言规范 §3.5），解析时保留为结构化节点，在名称解析阶段绑定到声明并存入 HIR。

### 5.3 名称解析与类型检查

- 模块图在解析后建立：包内允许环，跨包禁止环（见语言规范 §5）。
- 两阶段解析：先收集模块级声明（签名、类型、约束），再解析函数体；因此声明顺序无关。
- 类型检查落实语言规范 §3：interface 名义实现、菱形默认方法必须显式重写、`override` 强制、字段不参与覆盖、构造期禁虚调用、`type`/`distinct type` 的等价规则、声明点型变（`out`/`in`，§3.7）。
- 标注检查：`annotation` 声明先于使用收集；应用处校验目标类别（fn/class/var）、参数是否为编译期常量、是否重复、是否未知；`@cfg` 在其他标注之前求值。
- Proxy 生成：代码生成阶段为被标注声明生成 `Proxy<Decl>` 与静态 `AnnotationSet`（只读数据），未标注声明不产生任何包装；Proxy 布局与描述符参与 `.cif` 哈希。
- 运算符与型变解析：按语言规范 §3.7 / §3.8 做候选收集、孤儿规则校验、型变匹配与歧义诊断；运算符在 HIR 阶段就降为普通函数调用。
- 泛型定义处检查 + 实例化处复查两阶段，错误尽早暴露。

### 5.4 HIR

去糖后的带类型中间表示，仍保留泛型参数与 span。HIR 是"可缓存的语义结果"：名称解析、类型检查、接口满足性都产出 HIR，供 MIR 与 LSP 复用。

## 6. MIR 与编译期求值

- MIR 是显式 CFG：基本块、临时值、drop/清理边、数据流事实；泛型分析结果在此复用。
- `comptime` 在 MIR 解释器上求值：只接受已类型检查的 MIR，禁止未定义行为，配额为步数 / 递归深度 / 内存；超限报错并给出调用栈。
- 求值结果按"函数 MIR 哈希 + 实参 + TargetSpec"缓存，缓存条目可丢弃、可校验，不作为正确性唯一来源。

## 7. 单态化与实例缓存

- 实例键 = 模板定义 ID + 规范化实参（类型、常量、布局相关参数）+ 目标 ABI + 影响代码生成的编译选项。
- 用该键做 hash-consing、跨模块去重与磁盘缓存；符号名（mangling）只是链接层结果，不作为缓存主键。
- CGU 划分区分"稳定代码"与"实例代码"，避免改一个泛型函数导致全库重编。
- 控制手段按优先级：**度量**（实例数/耗时/体积）→ **预算**（每包实例数、递归深度、代码体积上限）→ **去重** → **显式实例化/擦除** → **ThinLTO**。
- 诊断必须打印实例化栈：从源码位置到外层实例化实参逐层展开，而不是只报最内层一行。

## 8. 后端、ABI 与链接

- 后端：LLVM IR；`-O0` 可选 Cranelift 快速路径（不作为发布承诺）。
- 目标：`TargetSpec`（arch、vendor、os、env、CPU 特性、ABI、sysroot、运行时、链接器、调试格式），三元组只是其序列化形式。
- ABI：内部 ABI 随编译器版本变化；跨包/跨语言只承诺 C 子集。Proxy（§语言规范 3.6）不属于稳定 ABI，跨 `extern "C"` 必须 `unwrap`。Windows x64 的 RCX/RDX/R8/R9 + shadow space、SysV 与 AAPCS 的差异由后端与 TargetSpec 处理，不散落在语言语义里。
- 链接：默认捆绑 lld（ELF/PE-COFF/Mach-O/WASM 全覆盖，PE 端口含 PDB）；`--linker=system|msvc|mold` 显式回退。
- 调试信息：ELF/Mach-O 用 DWARF，Windows 用 PDB/CodeView；MVP 先做行表与函数名，模板实例信息后补。

## 9. 模块接口文件 `.cif`

内容最小集：

1. 导出声明（函数/类型/接口/常量）与签名；
2. 类型定义、约束、`concept` 满足关系；
3. 依赖指纹（import 列表的接口哈希）；
4. **泛型体**（HIR/MIR 或等价的可实例化表示）与可实例化清单；
5. 语义哈希 + 编译器版本 + 目标 ABI；
6. 标注描述符（名称、编译期实参）与 Proxy 布局哈希，供跨包 `annotation_of` 与增量缓存使用。

增量编译以"接口哈希"而非源码哈希为重编依据：实现体变化不影响下游，除非该模块开启全量内联。

## 10. 工具链组件

| 组件 | 形态 |
|---|---|
| driver | 单一 `coord` 可执行文件；子命令 `new/build/run/check/test/fmt/lint/doc/publish/lsp`；并行调度、工具链发现、TargetSpec 解析 |
| 包管理 | 声明式清单 + lockfile + workspace；**不执行任意构建脚本**（v1），需要扩展时进沙箱 |
| 格式化 | 基于 CST，保留注释；`fmt --check` 幂等，不依赖类型信息 |
| lint | 跑在带类型 HIR 上，独立于错误检查；稳定 lint 名与级别 |
| LSP | `coord lsp` 独立进程，stdio JSON-RPC；复用容错解析与类型检查，支持快照/取消/增量同步 |
| 缓存 | 查询式 red-green + 内容寻址；键含编译器版本、TargetSpec、flags、源码/接口哈希 |
| 诊断 | 结构化（文本 + JSON）：错误码、主/次 span、note/help、实例化栈、可应用修复 |

## 11. 运行时与内存模型

- 无 GC、无运行时类型系统；对象默认栈分配，堆分配显式（`new`/allocator）。
- 标注实体的运行时表示 = 负载 + 指向静态 `AnnotationSet` 的指针（语言规范 §3.6）；未标注实体没有额外头部。`AnnotationSet` 每声明一份、只读、可共享。
- RAII + 确定性析构：`deinit` 在作用域结束按逆序调用；`defer` 作为语句级清理。
- 可选 arena / 引用计数类型作为标准库设施，不进入语言核心。
- 并发：线程安全由类型标记表达（`Send`/`Sync` 类概念），首版先做作用域并发与消息传递，暂不承诺 async 运行时。

## 12. 平台分级

| 级别 | 平台 | 承诺 |
|---|---|---|
| Tier 1 | Windows x64 (MSVC)、Linux x64 (glibc)、macOS arm64 | 每次提交跑 CI；发布阻塞 |
| Tier 2 | Linux aarch64、Windows arm64、wasm32-wasi | 保证构建，测试按周跑 |
| Tier 3 | 其余（含 `*-none-*` freestanding） | 社区维护，不保证 |

## 13. 风险与缓解

| 风险 | 缓解 |
|---|---|
| LLVM C++ API 无稳定 ABI | 固定 LLVM 大版本；codegen 层用自有接口隔离，尽量只用稳定 C API；每季度升级演练 |
| 单态化导致编译时间/体积爆炸 | 实例预算、去重、显式实例化、`dyn` 逃生门、ThinLTO |
| 调试信息在模板实例下失真 | MVP 先行表；实例栈优先作为诊断能力 |
| 自举不可验收 | MVP 阶段就记录 stage0/stage1 方案与 stage2/stage3 一致性判据 |
| 范围蔓延（语言+后端+包管理+IDE 同时做） | 阶段门禁 + 公开路线图；v1.0 前不做 GUI/注册表商业化 |

## 14. 阶段路线

| 阶段 | 交付物 | 验收 |
|---|---|---|
| MVP（0–6 月） | driver、SourceManager/span、lexer/parser、import/export、名称解析/类型检查、HIR、基础单态化、最小 comptime、LLVM x86-64（Windows/Linux）、行表、`build/run/check` | 泛型容器 + 接口示例可编译运行；解析器可模糊测试；错误均带文件/区间；同输入构建可复现 |
| 自举（6–18 月） | MIR + opt、查询式增量、包/构建/锁文件、fmt、lint、LSP、aarch64/macOS/WASM、PDB、C ABI、标准库 | 三平台 CI 全绿；增量构建 ≤ 全量 20%；标准库用 coordination 写；LSP 可用 |
| 生态（18–36 月） | 注册表与签名、文档生成、C 互操作、LTO/PGO、sanitizer、远程缓存、调试适配器、版本与弃用策略 | 外部包与真实项目；5+ 目标；性能与 Clang -O2 差距 ≤ 15%；规范 1.0 + 兼容性承诺 |
