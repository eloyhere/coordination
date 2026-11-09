# coordination 编译器与工具链架构调研

> 结论：coordination 宜采用“自有前端 + HIR/MIR + LLVM 主后端 + 一体化 driver”。编译速度靠查询式增量与缓存，跨平台靠 TargetSpec 与 lld，模板靠“默认单态化 + 显式擦除 + 实例预算”。

## 1. 分层：一条可缓存、可观测的流水线

```text
[驱动层] coordination build/run/check/fmt/lint/lsp
          TargetSpec：triple · CPU · ABI · sysroot · linker
             │ action graph
             ▼
[前端层] SourceManager → Lexer → Parser → AST/CST
             │ import/export + .cif
             ▼
         名称解析/类型检查 → HIR（typed, generic）
             │ const eval / 实例化计划
             ▼
[IR 层]  HIR → MIR（CFG, generic）→ MIR opt → Mono MIR/CGU
             │
             ▼
[后端层] LLVM IR + DWARF/PDB → LLVM opt → object
             │
             ▼
[链接层] ld.lld / lld-link / wasm-ld / mold / link.exe
         → exe / static lib / shared lib / wasm
旁路：查询缓存、依赖图、诊断/源码映射、包/构建图
```

驱动层只决定“做什么、按什么顺序、调用谁”。Clang 把驱动拆成参数解析、action 构造、工具绑定、参数翻译、执行五阶段，并把平台差异收进 ToolChain，这个边界可直接借鉴 [2]。前端保留注释与空白供格式化；无 include/宏使 import 图可在解析后立即建立。语义层做名称解析、类型检查、接口满足与泛型约束；HIR 保留类型、span 与泛型参数；MIR 是显式 CFG，承载求值顺序、临时值、清理边与数据流检查。泛型分析留在 HIR/MIR，单态化收集放在 MIR→后端，便于按实例缓存。Rust 的 HIR→MIR→LLVM-IR 分工可参考 [3]。

因 interface/type 仅编译期存在，`import` 不能靠头文件重放。接口文件（建议后缀 `.cif`）至少应含导出声明、约束、依赖指纹与**可实例化泛型体**；只有签名会让下游无法单态化。接口应内容寻址、带语义哈希，并与编译器版本绑定。无 namespace 还意味着符号可见性与修饰由 module/package 路径决定；应在 MVP 就固定模块命名、重导出规则与 mangling，否则跨包链接和调试名会冲突。

现有 `SyntaxNode`/AST 可作雏形，但 `SyntaxSpan` 只有 offset/length，没有文件身份；import 与模板实例化后会指错源文件。MVP 应先升级为 `SourceId + 字节区间 + expansion frame`，AST 改 arena + 稳定 NodeId；否则增量缓存、LSP 与跨文件诊断都缺地基。

## 2. 后端策略：LLVM 主后端，Cranelift 只做快速路径

| 路线 | 编译速度 | 优化上限 | 跨平台与调试信息 | 建议 |
|---|---|---|---|---|
| LLVM | 较慢，O2 尤其明显 | 最高，O3/LTO/ThinLTO 完整 | 目标最广；DWARF/PDB/CodeView 成熟 | **主后端** |
| Cranelift | 代码生成约快一个数量级 [14] | 中等，运行性能约低 14% [14] | x86-64/aarch64/s390x/riscv64；DWARF 线表支持起步较晚 [31] | 调试/JIT/REPL 备用 |
| QBE | 很快，官方称 2 秒级 [15] | 以 10% 代码量实现约 70% 性能 [15] | amd64/arm64/riscv64，完整 C ABI；以汇编输出为主，DI 需外部补齐 | 教学/引导，不作发布后端 |
| 自研 | 初期最慢 | 取决于投入 | 全自建，ABI/展开/调试风险最高 | 非战略必要不做 |
| C 后端/转译 | 借 C 编译器，起步快 | 受 C 编译器与转译质量限制 | 覆盖广；`#line` 映射到生成 C，语义/调试易失真 | 仅 MVP 应急 |

推荐只维护 LLVM 一个发布后端：它把优化、目标覆盖、异常/展开、DWARF/PDB 这些长期成本一次解决。Cranelift 的优势是“编译本身要快”，适合 `-O0`/JIT/REPL；但双后端意味着双份 ABI、调试信息和测试矩阵，早期不应同时承诺。QBE 的 70/10 定位适合教学或引导 [15]。C 后端只在 LLVM 尚未接通的几周内验证端到端链路；转译会扭曲模板实例、常量求值与源码映射，不宜进入关键路径。

## 3. 模板元编程：单态化是默认，擦除是显式逃生门

**单态化 vs 擦除。** C++/Rust 按具体类型生成代码，运行快，但编译时间与二进制体积随实例数增长；Rust 用 CGU 分区把稳定代码与单态化实例分开 [4]。Java 擦除把类型参数替换为边界或 Object，必要时插 cast/bridge，不为参数化类型建新类 [24]。Swift 保留运行期泛型模型，并以显式特化提案补足性能 [28]。coordination 建议默认单态化，保证值类型与零成本抽象；显式 `dyn`/`any` 擦除用于插件边界、大接口和编译热点。普通 interface 编译期消失，只有显式 `dyn` 才需最小 vtable，布局单独版本化。

**两阶段检查与实例化缓存。** 定义处检查泛型体在约束内是否自洽，实例化处再检查具体实参、常量条件和接口满足性。实例键 = 模板定义 ID + 规范化实参（类型、常量、布局）+ 目标 ABI 与数据布局 + 影响代码生成的选项；用它做 hash-consing、跨模块去重与磁盘缓存，mangling 只作链接名。CGU 继续分“稳定/实例”，避免改一个泛型函数就重编全库 [4]。导出的泛型必须随 `.cif` 发布可实例化体；增量编译既要跟踪 import 图，也要跟踪实例化图。

**编译期求值与缓存。** `constexpr/comptime` 建议跑在 MIR 解释器上：只接受已类型检查的 MIR，禁止未定义行为，设步数、内存、递归深度配额，超限报错。C++ 的同一函数可编译期/运行期求值 [25]；Rust 在 MIR 优化后求值并用查询缓存结果 [6]；Zig 用 `@setEvalBranchQuota` 显式限额 [13]。常量结果按“MIR 哈希 + 实参 + 目标 ABI/数据布局”缓存，并与诊断绑定，避免重复求值、重复报错。

**实例化爆炸控制。** 先度量（每个模板的实例数、耗时、代码体积），再预算（包/模块实例数、递归深度、代码生成量），然后去重（结构等价的类型/常量规范化）、显式实例化/外部实例化、擦除逃生门、稳定/实例 CGU 分离 + ThinLTO。模板与 comptime 是 coordination 仅有的元编程手段（无宏），诊断必须打印**实例化栈**：定义点、实参、外层实例化点，而不是只报最内层一行。

## 4. 跨平台：把目标描述做成一等公民

三元组 `<arch><sub>-<vendor>-<sys>-<env>` 只是序列化形式；内部 TargetSpec 还要 CPU/特性、ABI、sysroot、运行时、链接器与调试格式 [1]。数据布局由它决定，不能散落在 codegen。交叉编译也不止换 triple：还需要目标 sysroot、头文件/库、binutils 或链接器，Clang 文档明确这些问题常拖到构建/链接期才暴露 [1]。driver 要区分 host 工具与 target 产物，并在配置期检查依赖。

ABI 是最易低估的部分。Windows x64 用 RCX/RDX/R8/R9 传前四个整数参数、XMM0–XMM3 传浮点、调用者预留 shadow space；SysV x64 与 AArch64 AAPCS 的寄存器和聚合体规则不同 [21]。内部 ABI 可自定义，跨包/跨语言只承诺 `extern "C"` 与最小 `dyn` vtable；泛型实例用 linkonce/COMDAT 去重，或由显式实例化保证唯一。

链接器优先捆绑 lld：支持 ELF、PE/COFF、Mach-O 与 WebAssembly，PE 端口含 PDB 支持，且可作为库嵌入驱动 [16]；WASM 由 `wasm-ld` 负责，动态链接仍不成熟 [17]。Windows 保留 `link.exe` 回退 [22]；Linux 可选 mold 加速 ELF，但它不覆盖 Windows/macOS [27]。默认捆绑 lld 保证可复现，`--linker=system|msvc|mold` 作为显式选择。库方面，语言包默认静态归档 + `.cif`，最终可执行文件再链接；共享库只承诺 C ABI 边界。静态库要处理重复符号与初始化顺序，动态库要处理导出表、RPATH/`@rpath` 与版本脚本；这些应由 TargetSpec 生成链接参数，而不是让用户手写。Windows 明确 `/MT` 与 `/MD`，Linux 明确 glibc 动态链接与 `--static` 的边界。

freestanding 与 WASM 是独立目标族：`*-none-*` 不假设 libc/OS，需要 `_start`、panic handler、分配器钩子；WASM 分 `wasm32-unknown-unknown` 与 `wasm32-wasi`，后者按 WASI 提供能力 [13]。`wasm-ld` 默认 section GC、严格函数签名校验，调试信息与 `--compress-relocations` 冲突，需要目标级开关 [17]。

## 5. 工具链组件：一个 driver，一套缓存，一套诊断

| 组件 | 建议 |
|---|---|
| driver | 单一 `coordination`；子命令 `build/run/check/fmt/lint/lsp`；TargetSpec、action graph、工具链发现、并行调度；前端库化供 LSP/测试复用 |
| 包管理/构建 | 声明式清单 + lockfile + workspace；内建构建图与缓存；早期不执行任意构建脚本 |
| 格式化 | 基于 CST/保真 AST，保留注释与空白；`fmt --check` 幂等，不依赖类型信息 |
| lint | 跑在带类型 HIR 上，独立于错误检查；稳定 lint 名、级别与机器可应用修复 |
| LSP | `coordination lsp` 独立进程，stdio JSON-RPC；复用容错解析/类型检查；快照、取消、增量；参考 clangd [23] |
| 调试信息 | ELF/Mach-O 用 DWARF，Windows 用 PDB/CodeView；先行表，后变量/内联/模板实例；用 `llvm-dwarfdump`/`llvm-pdbutil` 验收 [18][19][20][30] |
| 增量与缓存 | red-green 查询 + 内容寻址；键含编译器版本、TargetSpec、flags、源码/接口哈希；先模块/函数级，后跨机共享 [5] |
| 诊断/源码映射 | 文本 + JSON、错误码、主/次 span、note/help、可应用性；模板实例化栈；LSP 共用 [7] |

两个契约要提前定。第一，诊断对象带 `SourceId` 与 `ExpansionFrame[]`，错误码首日稳定，否则生态工具无法依赖。第二，缓存条目可丢弃、可校验，不能成为正确性的唯一来源；Rust 的 red-green 算法证明只比较哈希即可跳过大量下游查询 [5]。安装布局固定为 `bin/lib/share/sysroots`，driver 按自身路径发现捆绑 lld 与运行时，避免依赖 PATH。编译器自身的构建可继续用 CMake；这里说的是 coordination 用户程序的构建系统，两者不要混用。包管理可借 Cargo 的 workspace/lockfile [8][9]，但构建与包管理放在同一 driver，减少 rustc/cargo 式参数翻译。工具链版本用清单字段固定；Go 的自动切换方便，但依赖校验与下载链 [10]，早期不宜默认自动下载。

## 6. 参考实现：借组织方式，不借全部复杂度

- **rustc/cargo**：查询系统、HIR/MIR、单态化 CGU、workspace 都值得借；但查询化是后期改造，coordination 应从 MVP 按查询边界设计 [3][5]。
- **go toolchain**：单命令、内建缓存、GOOS/GOARCH、工具链自动切换；Go 1.5 用旧编译器自举新编译器，证明“先可用、再自举”可行 [10][11][12]。不要照搬 GC 运行时假设。
- **swiftpm**：`Package.swift`、`swift build/test`、SourceKit-LSP 与模块稳定性构成闭环 [26][29]。可借“包管理 + LSP + 模块接口”一体设计；不建议照搬“清单即任意代码”，构建确定性与供应链风险太高。
- **zig build**：`build.zig`、捆绑交叉工具链、全量缓存与 `comptime` 展示了极致一体化 [13]。可借 hermetic 与缓存理念；其构建 API 不稳，coordination 应用声明式清单 + 受限脚本。
- **clangd**：独立进程、复用编译器前端、背景索引、编译数据库 [23]。coordination 应直接提供 `coordination lsp`，不让编辑器插件自己解析源码。

## 7. 分阶段落地路线

周期是规划假设，是否进入下一阶段以验收门槛为准。

| 阶段 | 建议周期 | 交付物 | 验收标准 |
|---|---|---|---|
| MVP | 约 0–6 个月 | driver、SourceManager/span、lexer/parser（AST/CST）、import/export、类型检查、HIR、基础单态化、最小 const eval、LLVM x86_64（Windows/Linux）、DWARF 行表、`build/run/check`、测试框架 | 泛型容器与接口示例可跑；前端/类型测试全绿；解析器可模糊测试；错误带文件/区间；同输入构建可复现；10k 行级示例稳定 |
| 自举/可用 | 约 6–18 个月 | MIR + MIR opt、查询式增量、包/构建/锁文件、formatter、lint、LSP、静态/动态库、aarch64/macOS/WASM、PDB、C ABI、标准库 | 三个 OS、至少两种架构的 CI 全绿；单函数改动后的增量构建 ≤ 全量 20%；标准库与关键工具用 coordination 编写；若自举，stage2/stage3 一致；LSP 补全/跳转/诊断可用 |
| 生态 | 约 18–36 个月 | 注册表与签名/校验、文档、C 互操作、LTO/PGO、sanitizer、远程缓存、调试适配器、版本与废弃策略、安全响应 | 有外部包与真实项目；5 个以上目标；运行性能与 Clang -O2 差距 ≤15%；语言规范 1.0 与兼容承诺；无未修复的高危供应链/编译器安全问题 |

MVP 的关键不是功能多，而是固定三个契约：span/诊断、TargetSpec、`.cif`。自举的关键不是重写一遍，而是标准库、构建工具和至少一个前端组件真正用 coordination 写，并用 stage2/stage3 一致性验收。生态阶段再引入注册表、远程缓存与 LTO，避免过早背上兼容性包袱。

## 8. 主要风险与决策建议

- **LLVM 依赖**：C++ API 无稳定 ABI，升级牵动全编译器。固定 LLVM 大版本，codegen 层用自有接口隔离，尽量只暴露稳定 C API，并做季度升级演练。
- **模板时间/体积**：单态化是默认路径，必须有实例预算、去重、显式实例化与擦除逃生门，否则大型项目会在 1–2 年后集中爆发。
- **调试信息**：模板实例与内联让源码映射复杂化；MVP 先保证行表与函数名，模板实例栈的优先级高于完整变量信息。
- **自举与 ABI**：MVP 就记录 stage0 语言与 stage2/stage3 验证方式；除 C ABI 外不承诺稳定 ABI，`.cif` 版本化并记录接口哈希。

建议写入 ADR：① 发布级只支持 LLVM，Cranelift 仅作 `-O0`/JIT；② 默认单态化 + 显式擦除 + 规范化实例键/预算/缓存；③ TargetSpec 与捆绑 lld 为一等公民，查询式增量与结构化诊断从 MVP 就位。

## 参考资料

1. Clang Cross-compilation：https://clang.llvm.org/docs/CrossCompilation.html
2. Clang Driver Design & Internals：https://clang.llvm.org/docs/DriverInternals.html
3. rustc 编译器总览：https://rustc-dev-guide.rust-lang.org/overview.html
4. rustc 单态化与 CGU：https://rustc-dev-guide.rust-lang.org/backend/monomorph.html
5. rustc 增量编译（red-green）：https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation.html
6. rustc 常量求值：https://rustc-dev-guide.rust-lang.org/const-eval.html
7. rustc 诊断与 span：https://rustc-dev-guide.rust-lang.org/diagnostics.html
8. Cargo Book：https://doc.rust-lang.org/cargo/
9. Cargo workspace：https://doc.rust-lang.org/cargo/reference/workspaces.html
10. Go Toolchains：https://go.dev/doc/toolchain
11. Go 1.5 发布说明（自举）：https://go.dev/doc/go1.5
12. go 命令与构建缓存：https://pkg.go.dev/cmd/go
13. Zig Language Reference（comptime/Targets/WebAssembly/Build System）：https://ziglang.org/documentation/master/
14. Cranelift：https://cranelift.dev/
15. QBE：https://c9x.me/compile/
16. LLD：https://lld.llvm.org/
17. lld WebAssembly 端口：https://lld.llvm.org/WebAssembly.html
18. LLVM PDB 文件格式：https://llvm.org/docs/PDB/index.html
19. DWARF 标准：https://dwarfstd.org/
20. Microsoft PE/COFF 格式：https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
21. Microsoft x64 调用约定：https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170
22. MSVC 链接器：https://learn.microsoft.com/en-us/cpp/build/reference/linking?view=msvc-170
23. clangd：https://clangd.llvm.org/
24. Java 类型擦除：https://docs.oracle.com/javase/tutorial/java/generics/erasure.html
25. C++ constexpr：https://learn.microsoft.com/en-us/cpp/cpp/constexpr-cpp?view=msvc-170
26. Swift Package Manager 文档：https://docs.swift.org/latest/documentation/packagemanagerdocs/
27. mold：https://github.com/rui314/mold
28. Swift 显式特化提案：https://github.com/swiftlang/swift-evolution/blob/main/proposals/0460-specialized.md
29. Swift ABI 稳定性与模块稳定性：https://www.swift.org/blog/abi-stability-and-more/
30. LLVM 源码级调试信息：https://llvm.org/docs/SourceLevelDebugging.html
31. Cranelift DWARF 线表支持讨论：https://bytecodealliance.github.io/zulip-archive/stream/217576-git-cranelift/topic/cranelift.20.2F.20Issue.20.23350.20DWARF.20line.20table.20support.html
