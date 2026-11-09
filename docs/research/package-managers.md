# Coordination 包管理器调研：九个生态的取舍

> 结论先行：包管理器真正的分水岭不是"有没有 lockfile"，而是**谁有权在构建期执行代码**。Cargo 的 `build.rs`、npm 的 `postinstall`、Conan 的 `conanfile.py`、Zig 的 `build.zig` 都把任意代码放进了依赖链；Go 与理想状态下的 Bazel 则试图把构建变成纯函数。Coordination 应站在后者一侧，解析上取 MVS 的确定性，再用强制 lockfile 弥补编译型语言的物理现实。

## 一、评价轴：清单、解析器、构建脚本

清单只该声明意图，解析器负责把意图变成一张确定的图，构建脚本则决定这张图能对宿主机做什么。三者混在一起的项目，最后都会用"可复现"换"灵活"。下面按"声明式阵营"和"编译型语言阵营"两组看，每组内部只讲差异和代价，不做功能清单。

## 二、声明式阵营

### Cargo：最大满足 + 特性合并 + build.rs

`Cargo.toml` 是 TOML，版本默认是 caret 语义：`1.2.3` 等价于 `>=1.2.3, <2.0.0`，而 `0.2.3` 等价于 `>=0.2.3, <0.3.0`——注意它按"左起第一个非零位"判兼容，和 semver.org 字面上"0.x 全不兼容"不同（[Cargo Book](https://doc.rust-lang.org/cargo/reference/specifying-dependencies.html)）。解析取范围内**最大**版本，`Cargo.lock` 记录精确版本与校验和；二进制提交 lock、库通常不提交。registry 是 crates.io 单一扁平命名空间，yank 只把版本移出新解析、不删内容；crates.io 不允许发布依赖 crates.io 之外的包（dev-dependencies 除外），这既是安全边界也是中心化。workspace 共享 lock 与 target，`workspace.dependencies` 统一版本。可复现性中上：lock + checksum + `cargo vendor`/source replacement 可离线。**最大的问题**是 `build.rs` 会被编译并执行，能读环境、调编译器、联网；OIDC 可信发布（[RFC 3691](https://rust-lang.github.io/rfcs/3691-trusted-publishing-cratesio.html)）与 2FA 管得住发布令牌，管不住构建脚本。判断：工程体验最好，信任模型最松。

### Node 三件套：同一个 postinstall，三种磁盘布局

`package.json` 配 `package-lock.json`/`pnpm-lock.yaml`/`yarn.lock`，范围用 `^`/`~`，解析取最大满足。npm 扁平提升、pnpm 用内容寻址 store 加符号链接（[pnpm](https://pnpm.io/motivation)）、yarn PnP 用 `.pnp.cjs` 取代 `node_modules`（[Yarn](https://yarnpkg.com/features/pnp)），三者解决的是布局与幻影依赖，不是信任。workspaces 字段支撑 monorepo。registry 用 `@org/name` 做命名空间，私有源靠 `.npmrc` 映射；名字抢注与 typosquatting 这里最严重。安全侧是行业最早落地的：npm provenance 用 Sigstore 与 CI 的 OIDC 签发（[npm 文档](https://docs.npmjs.com/generating-provenance-statements)、[GA 公告](https://blog.sigstore.dev/npm-provenance-ga/)），2FA 覆盖发布与 token；但 `preinstall/install/postinstall` 在安装期执行任意代码，`--ignore-scripts` 是唯一边界。离线靠 cache/store 与 `npm ci`。判断：生态最大，攻击面也最大。

### Go：用 MVS 换掉 lockfile

`go.mod`（require/replace/exclude/retract）加 `go.sum` 内容哈希，**没有 lockfile**：MVS 取所有约束下界的最大值，结果与"是否有新版本发布"无关，因此天然可复现（[Go Modules Reference](https://go.dev/ref/mod)、[MVS](https://research.swtch.com/vgo-mvs)）。模块路径就是 URL，命名空间等于域名所有权，`/v2` 后缀让不兼容大版本共存——抢注被 DNS 化解，代价是发现性差、域名过期即风险。GOPROXY 列表 + GOSUMDB 透明日志 + GOPRIVATE，`go mod vendor` 可完全离线；多模块用 `go.work`，但官方建议不提交，以免 CI 选错版本。构建期**不执行**依赖代码，`go:generate` 必须显式调用：这是它供应链口碑好的根本原因。代价是 MVS 可能选中带已知漏洞的旧版本，升级靠 `go get -u` 的纪律；`replace` 不被下游继承。判断：最值得抄的是"命名空间靠归属、构建不跑依赖代码"。

### JVM：nearest-wins 与 BOM

Maven 用 `pom.xml`，冲突按"最近定义"裁决、同深度先声明者胜（[Maven](https://maven.apache.org/guides/introduction/introduction-to-dependency-mechanism.html)）；范围语法存在但社区基本不用，靠 dependencyManagement/BOM 对齐版本。Maven 没有原生 lockfile，可复现靠 BOM 固定加插件。Gradle 用 Groovy/Kotlin DSL，**构建脚本本身就是程序**；依赖锁定是可选特性，依赖校验（checksum/PGP）也要手动开（[Gradle](https://docs.gradle.org/current/userguide/dependency_locking.html)）。Maven Central 要求 GPG 签名、版本不可变、不允许删除，groupId 是反向域名，同样是归属式命名空间；私有源用 Nexus/Artifactory，多模块靠 Maven reactor 的 `<modules>` 与 Gradle 的 `include`，离线用 `-o`/`--offline`。判断：企业治理成熟，但"锁定与校验都要手动开"是历史包袱。

### NuGet：被低估的中间路线

`csproj` 的 PackageReference 配 `Directory.Packages.props` 集中版本；范围用区间语法，默认取**满足下界的最低版本**，浮点 `6.*` 才取最高（[NuGet](https://learn.microsoft.com/en-us/nuget/concepts/package-versioning)）——这其实是 MVS 的商业化版本。`packages.lock.json` 加 locked mode 补上确定性；nuget.org 用保留前缀对抗抢注，私有 feed 是一等公民；包签名分作者签名与仓库副签，`nuget verify` 可验（[签名](https://learn.microsoft.com/en-us/nuget/create-packages/sign-a-package)），账户侧有 2FA。构建期风险来自包内 `build/*.targets`，MSBuild 会执行；离线靠全局包目录与 `--no-restore`。判断：解析与签名都做对了，弱在 MSBuild 的隐式执行与跨平台历史。

## 三、编译型语言阵营

### C++：包管理即构建系统

Conan 的 `conanfile.py` 是 Python，**配方就是程序**；版本范围 `[>=1.0 <2.0]` 取最大满足，`conan.lock` 记录版本加 recipe/package revision（[Conan](https://docs.conan.io/2/tutorial/versioning/lockfiles.html)）。vcpkg 走另一条：`vcpkg.json` 加 `builtin-baseline`（git commit）加 `version>=`，**选满足全部约束的最低版本**（[vcpkg](https://learn.microsoft.com/en-us/vcpkg/users/versioning)），`portfile.cmake` 下载源码并编译。两者都必须"从源码构建 + 二进制缓存"，因为 C++ 没有稳定 ABI；triplet/settings 决定二进制键。私有侧 Conan 用 remote、vcpkg 用 git registry，离线靠本地缓存与二进制缓存（Conan cache、vcpkg binary caching）；签名与 provenance 基本空白，信任等于审配方。判断：C++ 的包管理器注定是构建系统，Coordination 若要吃 C 生态，必须接受这层配方代码。

### SwiftPM：URL 即身份，manifest 即程序

`Package.swift` 是可执行的 Swift 程序（tools-version 决定 API），依赖以 git URL 加语义化范围声明，`Package.resolved` 锁 revision；registry（[SE-0292](https://github.com/swiftlang/swift-evolution/blob/main/proposals/0292-package-registry-service.md)）是可选第二条路，identity 为 `scope.name` 并带 checksum。没有 workspace 概念，用本地 path 包与 `swift package edit` 代替；构建插件会执行代码；镜像靠 `config set-mirror`，离线依赖已解析的 `Package.resolved` 与本地缓存（[SwiftPM](https://www.swift.org/documentation/package-manager/)）。安全侧最弱：无签名与透明日志，2FA 取决于托管方。判断：把"包"还原为"仓库 + 锁"，简单，但把治理推给了 GitHub。

### Zig：内容哈希取代 registry

`build.zig.zon` 声明依赖：url 加内容哈希，可选 `.path` 与惰性获取，`zig fetch --save` 写入哈希，抓取由构建系统内置（[build.zig.zon](https://github.com/ziglang/zig/blob/master/doc/build.zig.zon.md)、[Zig Build System](https://ziglang.org/learn/build-system/)）。没有中央 registry，也没有语义化范围解析——**哈希即身份**，因此天然可复现、天然抗抢注，也天然没有发现性与撤销机制；`build.zig` 是编译并执行的 Zig 程序，构建期代码执行无法避免，`--fetch`/`--fork` 提供离线与替换。判断：最少治理、最强完整性，适合做身份模型，不适合做完整产品。

### Bazel：MVS 的纯粹形态

`MODULE.bazel` 只写 `bazel_dep(name, version)`，不支持范围与 latest；bzlmod 用 MVS 求出一个版本，只有 root 的 override 生效；`MODULE.bazel.lock` 记录 registry 文件哈希与扩展结果（[Bazel modules](https://bazel.build/external/module)、[lockfile](https://bazel.build/external/lockfile)）。BCR 提供 `source.json` 的完整性哈希与补丁；自定义 registry 是静态文件树，天然可镜像、可离线。但 module extension 与 repository rule 会在抓取期执行代码，"hermetic"取决于规则作者。判断：构建图与依赖图统一的极致，却不是给普通开发者用的包管理器。

## 四、八个横向判断

**1. 语义化版本与范围。** semver 约束的是"公共 API 的意图"，范围语法把意图变成求解问题；范围越花，冲突越多，Cargo 文档自己就建议不要给非自有依赖加上界。0.x 应采用"左起第一个非零位兼容"，而不是字面上的"全不兼容"。

**2. MVS 对最大满足。** 最大满足让 patch 自动进来，代价是构建随时间漂移，必须靠 lock 冻结；MVS 让构建与发布时间无关，甚至可以不要 lock（Go），代价是默认选旧版本、漏洞修复要靠显式升级。Coordination 应选 MVS 作解析核心，但**强制 lock**：编译型语言的产物还依赖 target、编译器与系统库，Go"纯源码所以无需 lock"的前提在这里不成立。

**3. 可选依赖与 feature。** Cargo 的 feature 可加不可减、跨图合并，会出现"别人替我开了 feature"；npm 用 `optionalDependencies` 处理平台二进制；Maven 的 optional 不传递；vcpkg/Conan 用 feature/option 参与二进制键；Zig 的惰性依赖是"没 import 就不下载"的干净答案。结论：feature 只能影响"包含哪些依赖"，不能影响"已选版本"，且解析结果必须写进 lock。

**4. 私有 registry。** npm 的 scope 加 `.npmrc`、Cargo 的 alternate registry/source replacement、Go 的 GOPRIVATE、NuGet 的 feed、Conan remote、Bazel/vcpkg 的自建 registry——共同点是"命名空间可委派"。私有源最忌讳变成"同版本不同内容"的后门，内容哈希必须进 lock。

**5. vendor。** `cargo vendor`、`go mod vendor`、Bazel 的 repository_cache 都能离线，但 vendor 目录只能是 lock 的镜像，不能成为第二真相，否则审计与升级会分叉。

**6. yanked 版本。** Cargo 的 yank 只是移出新解析、不删内容；npm 的 unpublish 有 72 小时窗口，之后要人工介入（[npm 政策](https://docs.npmjs.com/policies/unpublish)）；Go 用 retract；NuGet 用 unlist，精确版本仍可还原；Maven Central 干脆不可删。正确的模型是"可撤回、不可删除、老 lock 仍可构建"。

**7. 名字抢注。** npm/crates.io 的先到先得制造了长期治理成本；NuGet 用保留前缀，Go 用域名，Swift 用 URL。**归属验证**（DNS TXT 或 OIDC）优于人工仲裁，同时要允许组织内重命名与转移。

**8. 镜像与离线。** 企业环境的真实需求不是"能访问外网"，而是"同一份 lock 在断网机房能重建"：Go 的 proxy 协议、Bazel 的静态 registry、Cargo 的 source replacement 都做到了协议层可镜像；npm 的镜像常靠整站同步，容易与上游漂移。判断标准只有一条——镜像能否按内容哈希校验。

## 五、对 Coordination 的 10 条设计建议

1. **清单声明式、构建逻辑隔离**：`coordination.toml` 只允许数据；构建期代码放进独立的 `build.coordination`，默认不执行、需显式 `--allow-build`，MVP 阶段直接禁用。
2. **MVS + 强制 lock**：解析单调、可解释、可缓存；lock 记录版本、内容哈希、target triple、feature 集、编译器版本与 C 依赖 ABI。
3. **收窄版本语法**：只允许精确、caret、tilde；禁止 `*` 与无上界；0.x 用左起非零位规则；预发布必须显式写出。
4. **归属式命名空间**：`org/name`，org 用 DNS TXT 或 OIDC 验证；不做全局扁平名；包名与 import 名解耦，支持 alias。
5. **构建期沙箱**：构建脚本声明能力（网络/文件/环境变量），默认断网；能力集与生成物一起进缓存键，避免假命中。
6. **发布侧短期凭证**：OIDC Trusted Publishing 加 Sigstore/in-toto 证明（commit 到 tarball），registry 提供透明日志；`--allow-unsigned` 仅显式开启。
7. **撤回而非删除**：yank 只影响新解析，老 lock 可重现；撤回必须带 advisory 链接与理由。
8. **feature 只增不减**：按 target/profile 解析并写入 lock；可选 native 依赖用惰性获取，未 import 不下载。
9. **镜像与离线是一等公民**：registry 协议必须可静态镜像（内容寻址、增量同步）；`coordination vendor` 与 lock 一一对应；`--offline` 严格失败而非静默降级。
10. **先接 C 生态**：提供 pkg-config/CMake 桥与显式 `system` 依赖，把系统库写进 lock；宁可少而可审计，不要多而不可复现。

## 六、对比表

| 生态 | 清单 | 解析 | lockfile | 构建期可执行代码 | 完整性/签名 | 命名空间 |
|---|---|---|---|---|---|---|
| Cargo | Cargo.toml | 最大满足 | Cargo.lock | `build.rs` | checksum + OIDC 发布 | 扁平 |
| npm/pnpm/yarn | package.json | 最大满足 | 三种 lock | `postinstall` 等 | provenance + 2FA | `@scope` |
| Go | go.mod | MVS | 无（go.sum 哈希） | 否 | sumdb 透明日志 | 模块路径=域名 |
| Maven | pom.xml | nearest-wins | 无原生 | 插件/扩展 | GPG + 不可变 | 反向域名 |
| Gradle | build.gradle(.kts) | 动态/富版本 | 可选 locking | 脚本本身 | 可选校验 | 反向域名 |
| NuGet | csproj | 最低满足/浮点 | packages.lock.json | `build/*.targets` | 作者签名+仓库副签 | 保留前缀 |
| Conan | conanfile.py | 最大满足 | conan.lock（含 revision） | 配方即 Python | 弱 | user/channel |
| vcpkg | vcpkg.json | 最低满足（baseline） | baseline（git commit） | `portfile.cmake` | 弱 | port 名 + registry |
| Bazel | MODULE.bazel | MVS | MODULE.bazel.lock | 扩展/仓库规则 | BCR 哈希 | 模块名 |
| Zig | build.zig.zon | 无（哈希固定） | 清单即锁 | `build.zig` | 内容哈希 | 无中央命名空间 |

**一句话**：Coordination 应取 Go 的身份模型、Bazel 的解析确定性、Zig 的完整性、npm 的发布凭证，再用一个默认关闭的沙箱，把 Cargo/Conan 那类配方代码关进去。
