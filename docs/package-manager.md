# Coordination 包管理器设计 v0.1

> 输入：[package-managers.md](research/package-managers.md)（9 个生态对比）、[toolchain-architecture.md](research/toolchain-architecture.md)、[language-spec.md](language-spec.md) §5。
> 定位：`coordination` 内置的包管理与构建前端；无 UI、跨平台、离线可复现。

## 1. 目标与非目标

**目标**：声明式依赖、确定性解析、强制 lockfile、内容寻址缓存、归属式命名空间、供应链可验证、断网可重建。

**非目标（v1.0 前）**：构建期任意脚本、动态版本、二进制包仓库、多语言依赖求解、桌面/图形入口。

## 2. 清单：`coordination.toml`

只允许**数据**，不允许代码（与 Cargo `build.rs`、Conan `conanfile.py` 的路线相反）：

```toml
[package]
name = "org.example/http"
version = "0.3.1"
edition = "2027"
description = "HTTP client"
license = "Apache-2.0"

[dependencies]
"org.example/tls" = "^1.2"
"org.example/log" = { version = "~0.9.0", optional = true }

[features]
default = ["tls"]
tls = { deps = ["org.example/tls"] }

[targets.x86_64-pc-windows-msvc]
dependencies = { "org.example/win32" = "1.0" }

[workspace]
members = ["compiler", "stdlib", "tools/*"]
```

约束：
- 依赖键是 `org/name`，与 import 名解耦（支持 `as` 别名）。
- 版本只允许精确、`^`、`~` 三种；禁止 `*` 与无上界。预发布必须显式写出。
- `[features]` 只能影响"包含哪些依赖"，不能影响已选版本；解析结果写入 lock。
- `[targets.*]` 的依赖按目标三元组解析，并写入 lock（编译型语言不能忽略 target）。

## 3. 版本解析：MVS + 强制 lock

- 采用 **Minimal Version Selection**（Go/Bazel 路线）：选满足约束的最低版本，解析结果与发布时间无关、单调、可解释。
- **lockfile 强制提交**：编译型语言的产物还依赖 target triple、编译器版本、系统库 ABI，Go "纯源码无需 lock" 的前提不成立。
- 0.x 兼容规则：左起第一个非零位兼容（`0.3.x` 视为兼容区间），不做字面上的"全不兼容"。
- `yank` 只影响新解析，不影响已有 lock；老 lock 必须仍可构建。

## 4. 锁文件：`coordination.lock`

```toml
version = 1
compiler = "coordination 0.4.2"
lockfile-hash = "sha256:..."

[[package]]
name = "org.example/tls"
version = "1.2.7"
source = "registry+https://pkg.coordination.dev"
checksum = "sha256:..."
features = ["default", "tls"]
target = "x86_64-pc-windows-msvc"
dependencies = ["org.example/crypto@1.1.2"]

[[system]]
name = "zlib"
version = "1.3.1"
source = "pkg-config"
```

必须记录：版本、来源、内容哈希、feature 集合、target、直接/传递依赖边、编译器版本、C 系统依赖。lock 变更必须出现在 diff 中，禁止"隐式升级"。

## 5. 包身份与 registry

- 命名空间 `org/name`：`org` 通过 **DNS TXT** 或 **OIDC 组织**验证，不做全局扁平先到先得（npm/crates.io 的治理债）。
- 版本不可变：发布后内容不可修改，只能 yank；registry 提供透明日志。
- 发布凭证：OIDC Trusted Publishing 短期令牌 + Sigstore/in-toto 证明（commit → tarball）；2FA 强制。
- 镜像与离线是一等公民：registry 协议必须可按内容哈希静态镜像；`--offline` 严格失败，不静默降级。

## 6. 构建集成：默认不执行依赖代码

- 清单不承载构建逻辑；确需生成代码时使用独立 `build.coordination`，**默认不执行**，需 `--allow-build`。
- 构建脚本声明能力（网络、文件系统、环境变量），默认断网；能力集参与缓存键，避免假命中。
- 依赖惰性获取：未被 `import` 的模块不下载（Zig 路线），减少供应链面。
- 与编译器的契约：模块导出接口写入 `.cif`（内容哈希 + 泛型体），包管理器只负责定位与校验，不解析语义。

## 7. 缓存、vendor 与离线

- 全局内容寻址缓存（SHA-256），本地 `~/.coordination/cache`（Windows `%LOCALAPPDATA%\coordination\cache`）。
- `coordination vendor` 只镜像 lock，不成为第二真相；vendor 目录必须可被 `coordination verify` 逐项校验。
- 缓存键 = 源码哈希 + 编译器版本 + TargetSpec + feature 集 + 构建能力集。

## 8. workspace 与路径依赖

- `[workspace] members` 支持编译型语言常见的"编译器 + 标准库 + 工具"单仓多包。
- 路径依赖 `{ path = "../stdlib" }` 只能在 workspace 内使用；发布时自动转成 registry 依赖并写入 lock。
- workspace 内共享 lock，成员版本统一由 workspace 根锁定。

## 9. 依赖图与循环

- 解析阶段构建模块依赖图（`compiler/Dependency.h` 的 `ModuleNode` 复用），跨包循环直接报 **E5001**。
- 继承图（class/interface）独立检测，循环报 **E5002**；两者在 `coordination check` 中先于代码生成执行。
- 图与缓存：每个包的导出集合指纹进入 `DependencyCache`，用于判断下游是否需要重编。

## 10. CLI 接口

```text
coordination new <org/name>            创建包骨架
coordination add <org/name>[@range]    添加依赖并解析
coordination remove <org/name>
coordination update [--precise X.Y.Z]  仅按 lock 允许的范围更新
coordination lock                      重新生成 lock（CI 用 --locked 校验）
coordination build|run|test|check
coordination verify                    校验 lock / vendor / 缓存一致性
coordination vendor                    按 lock 导出依赖
coordination publish [--dry-run]       签名并上传
```

CI 默认 `coordination build --locked --offline`：lock 不一致或缓存缺失直接失败。

## 11. 阶段路线

| 阶段 | 交付物 | 验收 |
|---|---|---|
| MVP | 清单解析、MVS 求解、lock 生成/校验、本地路径依赖、`build --locked` | 两个本地包互相依赖可构建；lock 可复现 |
| 1.0 前 | registry 客户端、内容缓存、`verify`/`vendor`、OIDC 发布（只读）、镜像协议 | 断网 + vendor 重建成功；篡改包被 checksum 拒绝 |
| 1.0 | 归属验证、Sigstore 证明、yank/撤回、workspace 完整支持、C 系统依赖 | 安全审计通过；两个 Tier 1 平台 CI 全绿 |

## 12. 取舍摘要

| 决策 | 采用 | 放弃 | 理由 |
|---|---|---|---|
| 解析 | MVS | 最大满足（Cargo/npm） | 构建与发布时间解耦；lock 冻结即可复现 |
| 清单 | 纯数据 TOML | 构建脚本/配方代码 | 供应链攻击面小，缓存键简单 |
| 身份 | `org/name` + 归属验证 | 扁平先到先得 | 治理成本低，防抢注 |
| 完整性 | 内容哈希 + 透明日志 | 仅版本号 | 镜像可校验，防同版本不同内容 |
| 构建脚本 | 默认禁用 + 能力沙箱 | 默认执行 | 与 toolchain 调研结论一致 |
