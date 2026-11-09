# Coordination 项目架构设计 v0.1

> 本文描述**仓库与工程结构**（目录、模块边界、构建、测试、CI、迁移）；编译器内部分层见 [architecture.md](architecture.md)，包管理见 [package-manager.md](package-manager.md)。
> 输入：[language-project-architectures.md](research/language-project-architectures.md)（rustc/Go/Swift/Kotlin/Zig/Roslyn/tsc 对比）。

## 1. 现状盘点

| 目录 | 内容 | 问题 |
|---|---|---|
| `compiler/` | `Span`/`Diagnostic`/`Source`/`Token`/`Lexer`/`Ast`/`Parser`/`Sema`/`Dependency`/`Driver`，全部平铺 | 9 个模块同层，边界靠约定；再增长会失控 |
| `charset/` | UTF-8/16 编解码、分类、BOM | 无依赖，位置合理 |
| `structure/` | 树、LRU、对象池、节点 | 编译器与运行时共用的基础设施 |
| `syntax/` | 早期 C++ 语法树原型 | 已被 `compiler/Ast.h` 取代，保留作设计参考 |
| `tests/` | 15 个 CTest 可执行 | 单层平铺，缺少分层与模糊/一致性测试 |
| `cmake/`、`CMakeLists.txt` | 头文件库 + demo + CTest + install | 只有单一 `coordination` INTERFACE 目标 |
| `docs/` | 规范、架构、流程、生命周期、包管理、调研 | 缺 ADR 目录 |

## 2. 目标目录树

```text
coordination/
├─ compiler/                 # 编译器（宿主 C++，按阶段拆子目录）
│  ├─ driver/                # coordination 子命令、参数、action graph
│  ├─ frontend/              # Source/Span/Diagnostic/Token/Lexer/Ast/Parser/Sema/Dependency
│  ├─ ir/                    # HIR/MIR、单态化、comptime 解释器
│  ├─ backend/               # LLVM 绑定、TargetSpec、ABI、调试信息
│  ├─ cif/                   # 模块接口文件 .cif 的读写与哈希
│  └─ support/               # 公共设施：文件、诊断渲染、哈希、缓存
├─ charset/                  # Unicode 基础设施（零依赖）
├─ stdlib/                   # 标准库 C++ 基础层：file/text/process/thread（后续用 coordination 自举）
├─ runtime/                  # panic/alloc/线程/时间等最小运行时
├─ tools/                    # fmt/lint/lsp/doc（后续）
├─ structure/                # 树/LRU/池等通用容器（被 compiler/runtime 复用）
├─ tests/
│  ├─ unit/                  # 现有 CTest 单测（按模块命名 test_<module>.cpp）
│  ├─ conformance/           # 按规范条款编号的语言一致性测试
│  ├─ ui/                    # 诊断快照（输入 + 期望输出）
│  └─ fuzz/                  # lexer/parser/缓存 模糊测试
├─ cmake/                    # CMake 模块（warnings/install）
├─ docs/
│  ├─ language-spec.md  architecture.md  project-architecture.md
│  ├─ package-manager.md  process.md  lifecycle.md
│  ├─ adr/                   # 架构决策记录（ADR-0001 起，一页一条）
│  └─ research/              # 调研原文（只增不改）
└─ CMakeLists.txt  CMakePresets.json
```

**保持单仓**：编译器、标准库、工具链同版本发布，接口以 `.cif` 发布，版本号单一来源（根 `VERSION` 文件 + CMake `project(VERSION)`）。参考 rustc/cargo 与 Go 的单仓模型；多仓只在生态规模真正需要时再拆。

## 3. 模块边界与依赖方向

```text
charset ────────────────┐（零依赖）
structure ──────────────┤
compiler/support ───────┤
compiler/frontend ──────┤ 依赖: support, charset, structure
compiler/ir ────────────┤ 依赖: frontend, support
compiler/backend ───────┘ 依赖: ir, support
compiler/driver ────────── 依赖: 以上全部（只做编排）
tools/fmt|lint|lsp ─────── 依赖: frontend, support（不依赖 backend）
```

硬规则：
1. **禁止反向依赖**（frontend 不得 include backend，charset 不得 include 任何东西）。
2. 每个子目录一个 CMake 目标（`coordination_frontend` 等），依赖通过 `target_link_libraries` 显式声明。
3. `compiler/driver` 不实现语义，只做参数、TargetSpec、action graph 与调用。
4. 公共设施只能来自 `support`/`charset`/`structure`，禁止各自造轮子。

## 4. 构建系统：两套分离

| 构建对象 | 系统 | 说明 |
|---|---|---|
| 宿主实现（C++） | CMake + Presets + Ninja | 现状保留；按子目录拆 target，`coordination` INTERFACE 保留为伞目标 |
| Coordination 自身构建 | `coordination build` | 清单 `coordination.toml` + lock，不使用 CMake；编译型语言的构建逻辑归语言工具链 |

两者不互相调用：CMake 构建编译器，编译器构建 Coastination 代码。测试与 CI 分别驱动两套。

## 5. 增量与缓存

- 查询键 = 源文件哈希 + 导出接口哈希（`.cif`）+ 编译器版本 + TargetSpec + 编译选项 + 构建能力集。
- `compiler/Dependency.h` 的 `DependencyCache` 负责导出集合指纹与依赖/继承边；接口变化才触发下游重编。
- 缓存可丢弃、可校验，不作为正确性唯一来源；`--locked --offline` 下缓存缺失应失败而非静默重建。
- 环检测（E5001/E5002）在 `coordination check` 的图构建阶段执行，先于代码生成。

## 6. 测试分层与 CI

| 层 | 位置 | 触发 |
|---|---|---|
| 单元测试 | `tests/unit` | 每次提交 |
| 一致性测试 | `tests/conformance` | 每次提交（按规范条款编号） |
| 诊断快照 | `tests/ui` | 每次提交（文本 diff） |
| 模糊测试 | `tests/fuzz` | nightly |
| 性能基准 | `tests/bench`（后续） | nightly + 发布 |

CI 矩阵：Tier 1 = Windows x64 MSVC / Linux x64 glibc / macOS arm64（每次提交）；Tier 2 = aarch64/wasm32（每周）。发布门禁另加：`coordination build --locked --offline` + 一致性测试 + 迁移工具 `coordination fix` 示例验证。

## 7. 迁移计划（从平铺到子目录）

1. **阶段 A（低风险）**：新建 `tests/unit`、`tests/conformance`、`docs/adr` 目录；把现有 `tests/test_*.cpp` 移入 `tests/unit`，CMake glob 改为递归。
2. **阶段 B**：`compiler/` 按 frontend/support 拆两目录（driver 暂留一层）；用转发头（`compiler/Span.h` → `#include "frontend/Span.h"`）保持旧路径可用一个版本周期。
3. **阶段 C**：引入 ir/backend 目录与对应 CMake target；`syntax/` 标记 deprecated，只在文档中保留。
4. **阶段 D**：stdlib/runtime/tools 建目录并接入 `coordination build`；CMake 只保留宿主目标。

每个阶段独立 PR，迁移期间不合并功能改动，保证 `ctest` 始终全绿。

## 8. 待落 ADR

- ADR-0001 单仓 + 六区划分（compiler/stdlib/runtime/tools/tests/charset）
- ADR-0002 宿主 CMake 与语言 `coordination build` 分离
- ADR-0003 查询式增量与缓存键
- ADR-0004 `.cif` 携带泛型体
- ADR-0005 测试四分层与 Tier 1/2 CI
- ADR-0006 自举判据（stage2/stage3 一致）
