# Coordination 历史文档索引

> **历史归档**：以下内容不再维护，语言、框架与工具链以 [framework.md](../../framework.md) 为准；归档只用于追溯设计演进。

按顺序阅读：

1. [01-architecture.md](01-architecture.md) — 定位、14 个根类型、Scope、编译生命周期、关键决策
2. [02-spec.md](02-spec.md) — 语言规范：词法、Scope、声明、枚举、继承、RAII、错误、注解、模块
3. [03-reflection.md](03-reflection.md) — description / descriptor / reflection / reflector / proxy、元注解、Retention 阶段
4. [04-compiler.md](04-compiler.md) — 编译器实现：流水线、Scope 栈、C++ 后端、保留表
5. [05-roadmap.md](05-roadmap.md) — M0–M4 里程碑、验收、风险、待决问题
6. [06-implementation-plan.md](06-implementation-plan.md) — 当前实施顺序、每项 Definition of Done
7. [07-modules.md](07-modules.md) — 模块划分、依赖方向、Unicode/BOM 契约
8. [grammar.ebnf](../grammar.ebnf) — 形式化文法（实现 + 规划）

## 状态图例

| 图标 | 含义 |
| --- | --- |
| ✅ | 已实现并有测试 |
| 🚧 | 部分实现（解析/部分语义） |
| ⏳ | 仅规范，未实现 |

## 功能矩阵

| 特性 | 规范 | 实现 | 里程碑 |
| --- | --- | --- | --- |
| Scope（每个 `{}`）/ 遮蔽 / 同域重复声明 | 02 §2 | ✅ | M0 |
| Unicode 源码 UTF-8/16/32 + BOM；Unicode 标识符 | 02 §1 | ✅ | M0 |
| 类、构造/析构、`this`、单继承、`implements` | 02 §3.2–3.3 | ✅ | M0 |
| RAII 合成析构、`~this.member;`、`W0001–W0003` | 02 §6 | ✅ | M0 |
| `const` 编译期求值、`build` 直出可执行文件 | 02 §9 | ✅ | M0 |
| 泛型 `<N extends integer>`（单态化到模板） | 02 §3.5 | ✅ | M0 |
| 指针 `T*` / 引用 `T&` | 02 §4.2 | ✅ 类型解析 | M0 |
| `enumerate` / `Enumeration` | 02 §3.2.1 | ✅ P0 核心（方法/implements/match 待 P1） | M1 |
| `@Retention(enum)` 变体表 | 03 §3.4 | ⏳ P2 | M2 |
| `final` / `override` / 访问段 / 字段隐藏 | 02 §5 | 🚧 P1（`super`、构造初始化列表待补） | M1 |
| `Result` / `Expect` / 隐藏冒泡 | 02 §8 | ⏳ P3 | M1 |
| 元注解 `@Inherit/@Repeatable/@Target/@Meta/@Retention` | 02 §3.4.1 | 🚧 语法，P2 语义 | M2 |
| 注解处理器 / `line` / proxy / 反射 | 03 | ⏳ P4 | M2 |
| `.coox` / 多模块 / 包管理 / 标准库 | 02 §10 | ⏳ P5 | M3 |

> 文档与实现冲突时，以本表“实现”列和 `06-implementation-plan.md` 为准；规范描述的是目标语义。
