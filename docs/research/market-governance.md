# coordination 市场、治理与版本策略调研

**口径**（2026-11-09）：TIOBE 为 2026 年 11 月榜；Stack Overflow 为 2025 年调查（31,771 份）；GitHub Octoverse 为 2025 年报（数据至 2025-08）；Rust 调查为 2025 年度（7,156 份，2026-03-02 发布）。TIOBE 统计搜索与提及，不等于岗位数；SO 与 Octoverse 均有自选择偏差，宜看趋势而非绝对值。

## 一、市场数据：存量在 C/C++，入口在既有生态

TIOBE 2026 年 11 月：Python 20.86%、C 11.48%、Java 8.89%、C++ 8.35%；Rust 跌出前十（Go 补位），Dart 首次进入前二十（[TIOBE](https://www.tiobe.com/tiobe-index/)，2026-11）。C/C++ 合计约 19.8%，系统层存量与迁移成本仍是新语言的最大阻力。

招聘数据没有全球统一口径：TIOBE 不含岗位，SO 只问使用，Octoverse 只统计 GitHub 活动。这里用三个指标交叉验证：专业开发者使用率、Rust 年度调查的招聘意愿、Android/Linux 的工业案例。对新语言而言，关键不是“哪个榜更高”，而是“谁愿意为迁移买单”。

Stack Overflow 2025：专业开发者中 Python 54.8%、TypeScript 48.8%、C++ 21.8%、Go 17.4%、Rust 14.5%、Kotlin 11.5%（[SO 2025](https://survey.stackoverflow.co/2025/technology)）。Rust 已越过玩具阶段，但仍低于 Go 与 TypeScript。

Octoverse 2025：GitHub 有 1.8 亿+ 开发者，一年新增 3,600 万、累计 6.3 亿仓库；TypeScript 按贡献者数升至第一（同比 +66.63%），Python 增长 48.78%；约 80% 新仓库只用六种语言（[GitHub Octoverse 2025](https://github.blog/news-insights/octoverse/octoverse-a-new-developer-joins-github-every-second-as-ai-leads-typescript-to-1/)）。入口高度集中，新语言很难靠“空白项目”获客，必须嵌入既有生态。

工业采用看“杀手负载”而非热度：Android 内存安全漏洞占比从 2019 年 76% 降至 2024 年 24%，新代码转向内存安全语言（Rust 是平台侧主力）是主因（[Google Security Blog](https://security.googleblog.com/2024/09/eliminating-memory-safety-vulnerabilities-Android.html)，2024-09-25）；Linux 6.1（2022 年底）合并 Rust 支持，先基础设施后驱动（[LWN](https://lwn.net/Articles/910762/)，2022-10-13）；Rust 2025 调查确认组织招聘意愿上升，同时把编译时间、复杂度、维护者资金列为担忧（[Rust Blog](https://blog.rust-lang.org/2026/03/02/2025-State-Of-Rust-Survey-results/)，2026-03-02）。Rust 在 TIOBE 回落、在生产上升，说明热度指标与工程采用并不同步。

## 二、新语言进入市场的五道关

**杀手场景**：Go 押中云原生 CLI，Rust 押中“无 GC 的内存安全”，Kotlin 押中 Android + Java 互操作，TypeScript 押中 JS 类型化。coordination 不宜做“更好的 C++”，应切“既有 C++/CMake 项目的增量模块”：无 GC、单文件二进制、C ABI 边界，先服务 CLI、代码生成与构建工具。

**互操作**：C ABI 是跨语言最小公分母。提供 `extern "C"`、`repr(C)`、明确所有权与错误码、禁止异常跨边界；C++ 先经 C shim，避开名字修饰与标准库 ABI；同时给 CMake config、头文件和 pkg-config，让接入成本低于重写。

**迁移成本**：允许同一构建内 C++ 与 coordination 共存，从叶子模块替换；提供 `coord fix`、迁移指南与兼容层，不要求一次性重写。

**工具链**：一个安装包、一个 `coord` 命令（new/build/run/test/fmt/lint/doc/publish）、秒级反馈、可读错误码、LSP、锁文件、交叉编译、可复现构建；编译时间要作为产品指标。

**文档**：Book（从零到可用）+ Reference（权威语义）+ Cookbook（任务导向）+ 10 个真实项目；文档进 CI、随版本发布、中英双语，并便于 AI 编码代理检索（[Octoverse 2026](https://octoverse.github.com/) 的主题即 agent 参与开发）。教程按任务组织（解析参数、读写文件、调用 C 库、发布二进制），而不是按语法章节排列；每个示例都要能在 CI 中运行，避免文档腐烂。

## 三、治理：五种范式与共同骨架

| 项目 | 提案与评审 | 最终决定 | 实现关系 |
|---|---|---|---|
| Rust | RFC 仓库，任何人可提；团队评审 + 最终评论期（FCP） | 团队共识，争议由负责人裁决；Leadership Council 协调 | RFC 合并后可实现，nightly feature gate 验证后稳定 |
| Swift | 论坛 pitch → proposal → workgroup 公开评审 | workgroup 决定；Core Team 定战略、可授权或否决 | 设计评审与实现评审分离 |
| Python | PEP（Python Enhancement Proposal），需核心开发者 sponsor | Steering Council 最终裁决，可授权 PEP-Delegate | 参考实现完成后才 Final；Provisional 可回退 |
| TC39 | Stage 0→1→2→2.7→3→4，champion 推进 | 全会共识；Stage 4 需两个实现 + Test262 + 实战 | 规范与测试先行，每年 7 月发布 |
| Kotlin | KEEP（Kotlin Evolution and Enhancement Process）：探索→KEEP→预览→稳定/撤销 | Lead Language Designer 拍板；稳定组件的破坏性变更需 Language Committee 批准 | EAP 预览；语言/API 版本开关 |

来源：[Rust RFC](https://rust-lang.github.io/rfcs/0002-rfc-process.html)、[Rust 治理](https://forge.rust-lang.org/governance/)、[Swift Evolution](https://www.swift.org/swift-evolution/)、[PEP 1](https://peps.python.org/pep-0001/)、[TC39](https://tc39.es/process-document/)、[Kotlin](https://kotlinlang.org/docs/kotlin-evolution-principles.html)。

共同骨架：公开提案、指定 champion、分阶段成熟、设计与实现分离、小机构最终负责、记录被拒方案。差异在决策权：Rust 偏团队共识，Swift 把领域决策下放给 workgroup、Core Team 保留战略否决，Python 靠核心开发者选举的理事会与 sponsor 制，TC39 用代表共识和 Stage 4 的双实现门槛，Kotlin 由公司主导、Language Committee 专管稳定组件的破坏性变更。coordination 必须明确“谁拍板”，否则 RFC 会沦为辩论俱乐部。

## 四、版本与兼容：把破坏关进笼子

SemVer 2.0.0 只约束公共 API：major 破坏、minor 新增、patch 修复；0.y.z 可随时变；已发布版本不可修改；弃用至少保留一个 minor 再在 major 移除（[semver.org](https://semver.org/)）。coordination 应把编译器/CLI 的 semver 与语言破坏分开：前者管工具链，后者用 edition 承载。

Rust edition 值得借鉴：2015/2018/2021/2024 按 crate 选择，跨 edition 必须互操作，`cargo fix` 自动迁移；2024 edition 随 1.85.0 于 2025-02-20 发布（[Edition Guide](https://doc.rust-lang.org/edition-guide/editions/index.html)）。edition 不是 semver major，不分裂生态，把关键字级破坏集中到可选的三年窗口。Rust 还用 nightly/beta/stable 三通道、6 周列车、feature gate，只支持最新 stable（[Rust Book](https://doc.rust-lang.org/book/appendix-07-nightly-rust.html)）。

弃用要可执行：Rust `#[deprecated(since, note)]`；Kotlin `@Deprecated` 分 WARNING/ERROR/HIDDEN，并可用语言/API 版本回退；Python 要求弃用警告至少跨两个 minor，现在更倾向 5 年（[PEP 387](https://peps.python.org/pep-0387/)）；Swift 用 `@available(*, deprecated)`；TC39 因 Web 兼容极少删除已发布特性。

ABI 不要轻易承诺：Rust 无稳定原生 ABI，默认布局无保证，跨语言稳定边界只保证 `extern "C"`/`repr(C)` 子集（[Rust Reference](https://doc.rust-lang.org/reference/type-layout.html)）；Swift 5 起在 Apple 平台 ABI 稳定，其他平台分别评估（[Swift.org](https://www.swift.org/blog/abi-stability-and-more/)，2019-02-07）；Kotlin/JVM 有二进制兼容协议；Go 只承诺源码兼容、不承诺二进制兼容（[Go 1](https://go.dev/doc/go1compat)）。coordination 应把“源码兼容 + C ABI”作为 1.0 承诺，原生 ABI 留到后端成熟后。

源码兼容承诺必须写清例外：安全漏洞、编译器 bug、未定义行为、平台 API 变化。例外越少越好，且每次例外都要有公告、影响范围与迁移建议，否则第一次例外就会透支信任。C++ 没有跨编译器稳定 ABI，coordination 以 C++ 为宿主时更不能把 C++ ABI 当接口。

## 五、生态运营：注册表是长期负债

包仓库：crates.io 由 Rust 团队运营，2025-07 上线 Trusted Publishing（OIDC 短期令牌，[RFC 3691](https://rust-lang.github.io/rfcs/3691-trusted-publishing-cratesio.html)）；npm 于 2023-09 让 Sigstore provenance GA（[Sigstore 博客](https://blog.sigstore.dev/npm-provenance-ga/)，2023-10-03），beta 期 3,800+ 项目、5 亿次下载，npm 每天约 4 万次发布；PyPI 2023-04 上线 Trusted Publishing（[PyPI Blog](https://blog.pypi.org/posts/2023-04-20-introducing-trusted-publishers/)）。共同方向：不可变版本、校验和、yank 而非删除、2FA、OIDC 发布、签名/溯源、名称抢注与恶意包治理。

文档与 CI：[docs.rs](https://docs.rs/) 为每个 crate 自动构建文档，[pkg.go.dev](https://pkg.go.dev/) 提供包文档；Rust 把平台分 Tier 1（保证构建 + 测试）、Tier 2（保证构建）、Tier 3（不保证），Tier 1 每次变更跑自动化测试（[rustc Book](https://doc.rust-lang.org/rustc/platform-support.html)）。coordination 应尽早定义平台分级与三平台 CI，否则“跨平台”只是口号。

注册表还包括命名空间与抢注、包所有权转移、恶意包下架、法律投诉、镜像与 CDN 成本；这些是长期人力，不是一次性开发。文档站要版本化（latest 与旧版本），示例可一键运行；CI 覆盖 Windows/Linux/macOS 与不同 CRT（MSVC/glibc/musl）。

资金：Rust Foundation 白金会员含 AWS、Google、Microsoft、Meta、Huawei、Arm，2026 年 NVIDIA、OpenAI 加入（[Rust Foundation](https://rustfoundation.org/members/)）；Kotlin 由 JetBrains 主导并设 [Kotlin Foundation](https://kotlinfoundation.org/)；Python 靠 [PSF](https://www.python.org/psf/)。商业化路径包括商业支持、培训、优先修复、托管 CI（[Rust Commercial Network](https://rustfoundation.org/rust-commercial-network)）；纯捐赠难养全职维护者，Rust 2025 调查已把“维护者支持”列为担忧。

## 六、对 coordination 的建议

从仓库现状看，coordination 以 C++20/CMake 做宿主实现，已有语法树与容器库基础。建议先定治理，再定版本，再谈生态。

**治理结构**：5 人核心组（语言、编译器、工具链、生态/发布、社区），公开 RFC 仓库与模板；语言设计变更需核心组 2/3 同意，僵局由语言负责人裁决并公开理由；设计评审与代码评审分离；设独立的行为规范与仲裁小组。

**发布节奏**：0.x 每 4 周 minor，nightly 每日、beta 每月；1.0 门槛为规范 + 一致性测试 + 全部 Tier 1 平台 + 文档/迁移工具；1.0 后 6 周列车，约三年一个 edition；只维护最新 stable，并为上一条 minor 线提供 12 个月安全修复。

**兼容性承诺**：1.0 发布《兼容性章程》——stable 源码兼容；破坏性变更只在 edition/大版本，附 `coord fix` 与迁移指南；弃用至少提前 2 个 minor 或 12 个月；安全例外需公告；ABI 只承诺 C 子集。

**社区启动**：以“C++/CMake 项目增量模块”为切口，提供 CMake 集成、C ABI 示例、从 C++ 迁移指南；找 3–5 个设计伙伴做真实工具；文档中英双语、进 CI；用 good-first-issue、月度 office hours、RFC 办公时间培养贡献者；早期用 GitHub Sponsors/Open Collective，1.0 后有多家赞助再考虑基金会。

用下载量、活跃项目数、CI 通过率、issue 首响时间、RFC 周期做季度复盘，不用 stars 当 KPI。主要风险是范围蔓延（同时做语言、后端、包管理、IDE）、过早承诺 ABI、治理被单一赞助方俘获、文档与实现脱节；对应的办法是阶段门禁、公开路线图与多赞助方。

## coordination 治理与发布建议（清单）

1. 建立公开 `coordination-rfcs` 仓库，提案模板固定含动机、语法/语义、兼容性、迁移、替代方案、未决问题。
2. 设 5 人核心组并明确授权：语言负责人对语义僵局有最终裁决权，发布负责人对版本窗口有最终决定权，二者都需公开书面理由。
3. 设计变更走 RFC + 7 天评论 + 10 天最终评论期（FCP）；代码与文档 PR 走普通评审，避免所有事都 RFC 化。
4. 0.x 阶段 4 周一个 minor；1.0 前冻结语法，1.0 后改 6 周列车，nightly/beta/stable 三通道。
5. 1.0 发布《兼容性章程》：stable 源码兼容；破坏只发生在 edition/大版本；弃用至少 2 个 minor 或 12 个月，并提供自动迁移。
6. ABI 只承诺 C 子集（`extern "C"`、`repr(C)`、无跨边界 unwind）；不承诺原生 ABI；C++ 互操作先经 C shim。
7. 平台分三级：Tier 1 为 Windows x64 MSVC、Linux x64 glibc、macOS arm64，每提交跑三平台 CI；Tier 2 交叉编译；Tier 3 社区维护。
8. 工具链先做“一个 `coord` 命令 + LSP + 锁文件 + 可复现构建 + 错误码”，编译时间与冷启动纳入 CI 预算。
9. 注册表 1.0 再上线（之前用 GitHub Releases + vendoring）；先定不可变版本、SHA-256、yank、名称政策、OIDC 发布、Sigstore 签名、2FA 与安全公告流程。
10. 社区以 3–5 个 C++/CMake 设计伙伴和 10 个真实示例启动；文档中英双语并进 CI；资金先众筹/商业支持，多赞助方后再设基金会。
