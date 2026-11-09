# 主流语言实现的工程架构：仓库、构建与发布

> 调研时间 2026-11。样本：rustc + cargo、Go、Swift、Kotlin（K2）、Zig、.NET Roslyn、TypeScript。本文只谈工程组织——目录划分、构建系统、模块边界、增量与缓存、测试、CI、发布；编译器内部流水线与后端选型见 [toolchain-architecture.md](toolchain-architecture.md)。方法：逐个读仓库目录、构建脚本、CI 配置与官方文档，能核对的写成事实，其余按工程判断给出建议；链接见文末。

## 1. 七个样本

七个样本按三个维度挑：是否自举、单仓还是多仓、构建系统是否自研。Rust 与 Go 代表“自举 + 内建构建”，Swift 代表“多仓 + CMake”，Kotlin 代表 JVM 生态的 Gradle 工程，Zig 代表“构建系统本身就是语言的一部分”，Roslyn 与 TypeScript 代表“编译器作为库/服务”的形态。

### rustc + cargo：单仓、Cargo workspace、查询式增量

`rust-lang/rust` 是单仓单 workspace：`compiler/` 约五十个 `rustc_*` crate，`library/` 放 core/alloc/std/proc_macro，`tests/` 放测试，`src/` 放 bootstrap、tools、ci、doc，`src/llvm-project` 是 LLVM 子模块。构建由 `src/bootstrap`（x.py）驱动 cargo，先取官方发布的 stage0 编译器，再逐级产出 stage1/stage2，`config.toml` 固定 LLVM 与目标。crate 既是模块边界也是并行编译单元，依赖被刻意压浅，查询系统用函数指针切断 crate 间依赖。增量是查询系统加 dep-graph 的 red-green 算法：输入全绿则不重算，重算后哈希不变仍标绿。测试由 compiletest 统一驱动 `tests/{ui,run-make,mir-opt,incremental}`，另有 tidy 仓库规范检查、rustdoc 测试与 crater 生态回归；CI 是 GitHub Actions 加 `src/ci` 容器镜像，按 tier 铺平台。发布是六周火车：nightly/beta/stable 三通道，rustup 分发，edition 三年一版。启示：crate 的粒度就是并行与增量的粒度，先划边界再写代码。

### Go：src/cmd 扁平布局与 cmd/dist 自举

`golang/go` 把几乎所有东西放 `src/`：`src/cmd/{compile,link,go,dist,asm}` 是命令，`src/cmd/compile/internal/{syntax,types,ir,ssa,obj,amd64}` 是编译器各阶段，`src/runtime`、`src/internal`、`src/go/types` 与标准库同层，`test/` 是编译器回归集，`api/` 存 API 快照。构建走 `make.bash`（Windows 为 `make.bat`）：先用宿主 C 编译器编出 `cmd/dist`，再由 dist 构建整条工具链；Go 1.5 起编译器用 Go 写，靠上一版 Go 自举。包即目录，`internal/` 用路径强制可见性；标准库不在独立仓，与编译器同树同版本。增量是包级构建缓存（GOCACHE，内容寻址的 action ID），没有函数级增量，但跨项目共享。测试为 `go test ./...`、`test/` 下的 run.go 回归、`go test -fuzz`，以及 build.golang.org 的多平台 builder。发布半年一版（2 月/8 月），Go 1 只承诺源码兼容、不承诺二进制兼容，只支持最近两个大版本。启示：单一 `src/cmd` 布局让工具与编译器同树演进，但包级缓存意味着改一行要重编整包，与 Coordination 想要的函数级增量不是一回事。

### Swift：多仓拆分，CMake + Ninja

Swift 是少数主动拆仓的：`swift` 主仓含编译器、标准库与 SourceKit；`swift-driver` 用 Swift 重写驱动；`swift-package-manager`、`swift-syntax`、`swift-format`、`sourcekit-lsp`、`swift-llbuild`、`swift-testing`、`swiftly` 各自成仓，LLVM 用 swiftlang 分支。主仓用 CMake + Ninja，`utils/build-script` 封装配置与构建，产物在 `build/<config>`；标准库在 `stdlib/public` 按 core/Foundation 分层，模块以 `.swiftmodule`/`.swiftinterface` 交付，支撑 ABI 与模块稳定。驱动独立成库，可被 SwiftPM 复用；SwiftPM 的构建图交给 llbuild。增量由 swift-driver 的 build record 做文件级判断，SwiftPM 侧由 llbuild 承担。测试分 `test/`（lit，按语言特性分层）、`validation-test/`、`unittests/`，CI 是多平台 swift-ci。发布用 `swift-6.x` 标签与 `release/6.x` 分支，工具链经 swiftly 安装。启示：拆仓的代价在版本对齐，Coordination 在只有一套发布节奏之前不要拆。

### Kotlin：K2 前端重写与 Gradle 自举

`JetBrains/kotlin` 单仓：`compiler/fir` 是 K2 前端（FIR），`compiler/fir2ir` 与 `compiler/ir` 通向 JVM/JS/Native/Wasm 各后端（`compiler/backend`、`js/`、`kotlin-native/`），`libraries/stdlib` 与 `libraries/tools`（Gradle 插件）同仓，`compiler/testData` 是测试数据源。构建用 Gradle（Kotlin DSL）与复合构建，`./gradlew dist` 出产物；编译器用上一版 Kotlin 自举，K1/K2 并存期可用 `-language-version 1.9` 回退。FIR 是统一数据结构，编译器与 IDE 共用同一套分析结果，这是 K2 最关键的工程决策。增量在 Gradle 侧：classpath 快照（fine/coarse-grained）决定重编范围，叠加构建缓存、配置缓存与 Kotlin daemon。测试由 `tests-gen` 从 `testData` 生成 JUnit 用例，避免手写重复；CI 是 TeamCity 多平台。发布按 2.x 语言版本走，stdlib 单独发 Maven 包但与编译器版本强绑定。启示：编译器与 IDE 共用同一份语义结果最省成本，`coordination lsp` 应直接复用类型检查器，而不是另写一套分析。

### Zig：build.zig 就是构建系统

`ziglang/zig` 单仓：`src/` 是自举的编译器（Zig 写），`lib/std` 是标准库，`lib/compiler` 放 build runner 与 test runner，`test/` 放行为测试，`doc/langref` 是语言参考。构建不依赖 CMake/make：`build.zig` 用 Zig 描述 DAG，`build.zig.zon` 声明依赖，`zig build` 并发执行并把结果缓存到 `.zig-cache`（可随时删除），安装前缀是 `zig-out`。模块是编译单元，std 与编译器同树、版本严格绑定；build 脚本同时承担包描述与交叉编译编排，一次 `-Dtarget` 可产多目标。增量靠 `.zig-cache` 与 `-fincremental`，构建系统本身带 `--fuzz` 与 `--watch`。测试是源码里的 `test` 声明，`zig test` 或 build 图的 test step 执行，doctest 用标识符命名。CI 是 GitHub Actions 的五平台矩阵加社区 build bots；发布仍是 0.x，不承诺语言稳定。启示：用被测语言写构建脚本，能在语言早期就压出表达力问题；但它的 API 年年变，Coordination 应把清单做成声明式、把脚本关进沙箱。

### Roslyn：Arcade + NuGet 交付

`dotnet/roslyn` 单仓：`src/Compilers/{Core,CSharp,VisualBasic}` 分 Portable 与 Test，`src/Workspaces`、`src/Features`、`src/EditorFeatures` 向上分层，`eng/` 放 Arcade 工程基础设施。构建用 Arcade SDK 驱动的 MSBuild，`build.cmd`/`build.sh` 统一 restore/build/test；产品形态是 NuGet 包（Microsoft.CodeAnalysis.*），由 .NET SDK 与 VS 消费，而不是独立可执行文件。边界上 Compilers 是公共 API 契约，Workspaces/Features 建在其上；分析器与源生成器是插件模型，第三方以 NuGet 包注入编译。csc 每次调用都从头编译，不做持久化增量，增量在 IDE：red/green 增量语法树与快照复用；`/deterministic`、`/pathmap` 保证可复现。测试是 xUnit 单元与基线测试，CI 走 Azure Pipelines 的 Windows/Linux/macOS；Arcade 把 SDK 版本、分析器、签名与发布流程统一收口，第三方依赖集中在 `eng/Versions.props`。发布与 VS/SDK 节奏绑定，包版本随 SDK 递增，语言版本由 `LangVersion` 控制。启示：把编译器当库交付，IDE、SDK、构建服务器才能复用同一份前端；Coordination 的 `coordination` 也应先有库形态，再有命令行。

### TypeScript：LKG 自举 + 基线测试

`microsoft/TypeScript` 单仓：`src/compiler`（scanner/parser/checker/emitter）、`src/services`（语言服务）、`src/server`（tsserver）、`src/harness`，`tests/cases` 与 `tests/baselines` 是测试主体。构建用 hereby 调度 tsc：源码由仓库内 `lib/` 的上一版 tsc（LKG）编译到 `built/local`，实现自举且不依赖外部编译器；构建、测试与打包共用同一条 hereby 任务链，本地入口是 `hereby runtests`。边界是 compiler/services/server 三层，services 同时供 tsserver 与编辑器。增量是 `--incremental` 的 `.tsbuildinfo` 加 `--build` 项目引用，watch 模式复用内存 AST。测试以基线为主：改动后 diff 基线即回归报告，另有 fourslash 语言服务测试与单元测试；CI 是 Azure Pipelines 加 GitHub Actions。发布为 npm 的 `typescript` 包，语义化版本，`next`/`beta`/`rc` 预发布通道，约每季度一个 minor。启示：对编译器/emit 这类输出确定的组件，基线测试极其划算；Coordination 应对诊断文本与 `fmt` 输出建黄金文件。

## 2. 横向主题

**单仓 vs 多仓。** 除 Swift 外都是单仓：编译器、标准库、工具、测试可在同一提交原子修改，CI 一次跑完，版本号单一来源。Swift 拆仓换来复用（swift-syntax 被外部工具依赖）与独立节奏，代价是跨仓改动要多次 PR、版本要对齐。Coordination 现在规模小，单仓是对的；等 `coordination fmt`/`lsp` 要被第三方嵌入时再拆。

**编译器与标准库的版本耦合。** Rust、Go、Zig 同树同版本，语言特性与 std 的 unstable 接口一起演进；Kotlin 的 stdlib 单独发 Maven 包，但用语言版本约束 API；Swift 靠 ABI/模块稳定让旧 stdlib 可用；TS 把 `lib.d.ts` 与编译器同包发布。规律是同仓开发、同版本发布，跨版本只承诺接口文件（`.cif`/`.swiftinterface`/`.d.ts`）。

**Bootstrap。** 六种做法：Rust 用上一版 stable 做 stage0，逐级到 stage2 并校验 stage2/stage3 一致；Go 1.5 起用 Go 写编译器、用上一版自举；Zig 用 zig-bootstrap 从 C 工具链拉起；Kotlin 用上一版编译器；TS 用 LKG；Roslyn 用上一版 SDK。Coordination 的 stage0 是 C++ 前端，必须现在就把 stage1/stage2 的验收判据写进文档。

**工具放哪。** 同仓是主流：Rust 的 clippy/rustfmt/rustdoc 在 `src/tools`，Go 的 gofmt/vet 在 `src/cmd`，Kotlin 的 Gradle 插件在 `libraries/tools`，TS 的语言服务在 `src/services`；Swift 把 formatter 与 LSP 拆出去。建议核心工具同仓同版本、共享前端库，IDE 适配器可外置。

**生成物怎么进构建。** Swift 用 gyb 生成部分内置类型与 SIL 节点，Kotlin 用 tests-gen 生成测试，TS 用 LKG 编译自身，Rust 的 LLVM 是子模块、Swift 的 LLVM 是并列仓，都在编译前预构建。做法一致：生成步骤是构建图里的普通节点，产物进缓存，默认不提交到仓库；需要人审的生成物用单独的 update 步骤跑。Coordination 的 `.cif` 与表驱动代码生成也应走这条路。

**构建系统谱系与依赖。** 七家分别用 Cargo、cmd/dist、CMake + Ninja、Gradle、build.zig、MSBuild/Arcade、hereby，没有一家以 Bazel 为主链——Bazel 的收益在超大多语言单仓，代价是工具链全自建。依赖管理都有锁文件与哈希：Cargo.lock、go.sum/vendor、Package.resolved、Gradle 校验和、NuGet 锁定、package-lock、build.zig.zon 的 URL + hash；共同策略是最小依赖、可 vendor、构建期不取最新版。

**测试的三种范式。** 一是基线/黄金文件：TS 的 `tests/baselines`、Roslyn 的基线测试、Kotlin 的 `testData`，改动后 diff 即回归报告，适合诊断文本与代码生成；二是源码内声明：Zig 的 `test` 块与 Rust 的 doctest，测试跟着代码走，不容易腐烂；三是生成式：Kotlin 用 `tests-gen` 从数据生成 JUnit 用例，避免几千个手写文件。模糊测试是补充：Go 原生 `go test -fuzz`，Zig 构建系统带 `--fuzz`，Rust 生态用 cargo-fuzz 外挂。Coordination 应两者都要：诊断与格式化用基线，lexer/parser/类型检查用 fuzz。

**CI 矩阵。** 七家都按“每 PR 少平台、定时全矩阵”分层：Rust 用 tier 定义承诺级别，并用容器镜像固定环境；Go 靠 build.golang.org 的 builder 覆盖 OS/架构组合；Swift 是多平台 swift-ci；Kotlin 用 TeamCity；Zig 是五平台 GitHub Actions 矩阵；Roslyn 与 TS 是三大 OS 的 Azure Pipelines。共性是平台分级写进文档、环境容器化、失败能归因到具体平台——这也是 Coordination 的 Tier1/Tier2 该有的样子。

## 3. 对 Coordination 的十条建议

1. 保持单仓，先分 `compiler/`、`stdlib/`、`runtime/`、`tools/`、`tests/`、`charset/` 六个顶层区，暂不拆仓。
2. 两套构建系统分开：宿主 C++ 继续 CMake + Presets + Ninja；语言自身构建由 `coordination build` 负责，边界写进文档。
3. `compiler/` 立刻按阶段拆子目录（driver/frontend/ir/backend/support/cif），每个子目录一个 CMake target，禁止反向依赖。
4. 把现在 `compiler/*.h` 的平铺改为 `frontend/` 下的 lexer/parser/ast/sema/hir，`structure/` 收敛为 `support/`，`syntax/` 原型标记 deprecated。
5. `stdlib/` 与编译器同仓同版本，用 coordination 写；接口以 `.cif` 发布，版本号单一来源——标准库若单独发版，语言与库的兼容矩阵会指数膨胀。
6. 测试四分层：`tests/unit`（CTest）、`tests/conformance`（语言一致性）、`tests/ui`（编译失败基线）、`tests/fuzz`（lexer/parser/类型检查）。
7. 增量从 MVP 就按查询边界设计：键 = 源/接口哈希 + 编译器版本 + TargetSpec + flags，缓存目录可丢弃、可校验，不能成为正确性的唯一来源。
8. CI 分 tier：Tier1 三平台每次提交，Tier2 交叉编译按周；本地与 CI 共用 CMakePresets，避免两套命令。
9. 工具进 `tools/`（fmt/lint/lsp/doc/pkg），链接前端库而非复制代码；LSP 独立进程，复用容错解析。
10. 第三方依赖白名单 + vendor 到 `third_party/`，锁版本与哈希，构建期禁止联网；bootstrap 判据（stage2/stage3）写进 ADR。

## 4. 推荐目录树

```text
coordination/
├─ CMakeLists.txt  CMakePresets.json
├─ cmake/                  # 警告、安装、工具链
├─ compiler/
│  ├─ driver/              # coordination 子命令、action graph、TargetSpec
│  ├─ frontend/            # source、lexer、parser、ast、sema、hir
│  ├─ ir/                  # mir、comptime、单态化
│  ├─ backend/             # llvm codegen、调试信息
│  ├─ support/             # diagnostic、span、arena、容器（原 structure/）
│  └─ cif/                 # 接口文件读写与哈希
├─ stdlib/                 # core、alloc、io、fmt、test（coordination 写）
├─ runtime/                # panic、alloc、线程、start
├─ charset/                # UTF-8/16/32、分类、大小写、规范化
├─ tools/                  # fmt、lint、lsp、doc、pkg
├─ tests/                  # unit、conformance、ui、fuzz、bench
├─ third_party/            # vendor 的 LLVM 等（锁版本）
├─ docs/                   # spec、architecture、research
└─ out/                    # build、install、cache（git 忽略）
```

迁移顺序：先建 `compiler/` 子目录并把现有头文件按阶段搬入，再加 `stdlib/` 与 `tests/conformance/`，最后接 `tools/`。`charset/` 保持独立顶层：lexer、字符串字面量与 `character(8/16/32)` 都要用它，而它不依赖编译器其余部分，先独立可复用的模块最省事。

## 参考资料

1. [rustc 源码结构](https://rustc-dev-guide.rust-lang.org/compiler-src.html)
2. [rustc 增量编译（red-green）](https://rustc-dev-guide.rust-lang.org/queries/incremental-compilation.html)
3. [rust-lang/rust 仓库](https://github.com/rust-lang/rust)
4. [Cargo Book](https://doc.rust-lang.org/cargo/)
5. [Go 发布策略](https://go.dev/doc/devel/release)
6. [Go 1 兼容承诺](https://go.dev/doc/go1compat)
7. [golang/go 仓库](https://github.com/golang/go)
8. [Swift 源码仓清单](https://www.swift.org/documentation/source-code/)
9. [Swift 贡献与测试要求](https://www.swift.org/contributing/)
10. [swift-driver](https://github.com/swiftlang/swift-driver)
11. [Swift Package Manager](https://github.com/swiftlang/swift-package-manager)
12. [K2 迁移指南](https://kotlinlang.org/docs/k2-compiler-migration-guide.html)
13. [Kotlin 增量编译与缓存](https://kotlinlang.org/docs/gradle-compilation-and-caches.html)
14. [JetBrains/kotlin 仓库](https://github.com/JetBrains/kotlin)
15. [Zig Build System](https://ziglang.org/learn/build-system/)
16. [Zig 语言参考（test/build）](https://ziglang.org/documentation/master/)
17. [ziglang/zig 仓库](https://github.com/ziglang/zig)
18. [Roslyn 构建、测试与调试](https://github.com/dotnet/roslyn/blob/main/docs/wiki/Building-Testing-and-Debugging.md)
19. [dotnet/roslyn 仓库](https://github.com/dotnet/roslyn)
20. [TypeScript 构建说明](https://github.com/microsoft/TypeScript/wiki/Building-TypeScript)
21. [TypeScript incremental](https://www.typescriptlang.org/tsconfig/incremental.html)
22. [microsoft/TypeScript 仓库](https://github.com/microsoft/TypeScript)
