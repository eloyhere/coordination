# 06 · 实施计划（逐项落地）

## 工作方式

每个特性都走同一条垂直切片：

```
规范条目 → 词法/语法 → AST → Sema（含负例） → C++ 后端 → 示例 → 单元/e2e 测试 → 文档状态
```

没有测试的条目不算完成；文档里标 ✅ 必须能在 `ctest` 中找到对应测试。

## 当前队列

| ID | 特性 | 状态 | 验收 |
| --- | --- | --- | --- |
| P0 | `enumerate` / `Enumeration` 前端 + 后端 | ✅ 核心 | 声明、变体值、`Color.Red`、`print`、e2e |
| P1 | 继承完整化：`final`/`override`/`super`/访问段/字段隐藏/协变 | 🚧 | 正负例 + 可观察虚分派 |
| P2 | 元注解与 Retention 阶段校验 | ⏳ | `@Inherit/@Repeatable/@Target/@Meta/@Retention` 正负例 |
| P3 | `Result`/`Expect` 隐藏返回与冒泡 | ⏳ | 错误链、`@Result/@Expect` 消费检查 |
| P4 | 注解处理器、`line`、proxy、反射 | ⏳ | `@SourceMap`、`@Proxy`、`effective_annotations` |
| P5 | `.coox`、多模块、包、标准库 | ⏳ | 两包依赖、增量、`extern "C++"` |

## P0 `enumerate`（核心已完成）

- 词法：`enumerate` 关键字。
- 语法：`enumerate Name { A, B = 3, C, }`；`implements` 与方法的语法已可解析。
- 语义：具体枚举类型是 `Enumeration` 子类型；变体名唯一、值唯一；默认
  0/前值+1；显式值必须是整数常量表达式；`Color.Red` 是枚举值。
- 后端：纯枚举降级为 `enum class Color : std::int64_t { ... };`；变体访问
  `Color::Red`；`print` 输出底层整数。
- 测试：`coord_e2e_enum` 输出 `3`；单元测试覆盖变体值与重复值负例。
- 未完成：枚举方法、`implements`、`match` 穷尽、`@Retention(enum)` 表；
  在 P1/P2 继续。

## P1 继承完整化（进行中）

已完成：

1. 访问段 `public:/private:/protected:` 的解析、默认 private、字段/方法/构造
   访问检查（`E2030`）。
2. `final` 类/方法/字段；`virtual` + `override` 强制（`E2033`/`E2035`）；
   协变返回（`E2034`）；有 virtual 方法时自动 virtual 析构。
3. 同名字段隐藏（两份都在），`final` 字段禁止隐藏（`E2032`）。
4. C++ 后端：访问标签、`class X final`、`virtual/override/final`、虚析构。
5. 测试：`testAccessControl`、`testInheritanceRules`、`testAccessCodegen`。

下一轮：

1. `super.method()` / `super.field` / `: super(args)`。
2. 构造函数初始化列表 `: field(expr)`、初始化顺序检查。
3. 构造/析构访问检查覆盖默认构造与默认析构。
4. 枚举方法与 `implements` 接口。
5. 基类引用/指针的虚分派端到端测试。

## P2 元注解与 Retention

1. Sema 建 `AnnotationInfo`：目标、可重复、可继承、Meta、Retention 阶段。
2. `@Target` 校验（含 enum/enum_value）；`@Repeatable` 重复校验；`@Inherit`
   计算 `effective_annotations()`（直接优先、继承补缺）。
3. `@Retention(runtime)`：旁路只读表 + 访问器；`@Retention(enum)` 变体表。
4. `@Retention(invocation)`：调用/字段/构造 hook，只改值；类型可赋值检查；
   handler 状态在旁路存储。
5. 测试：正负例、枚举表查询、invocation 改值不改结构。

## P3 Result / Expect

- 有效返回类型 `Result<T, Error>` 的隐藏与自动冒泡。
- `@Result` 暴露 Result；`@Expect` 生成必须消费的 `Expect<T, Error>`。
- `Error` 接口（code/message/source/backtrace）；构造不可失败。
- 编译器：调用点插入错误检查、Scope 退出析构、C++ 后端返回槽。
- 测试：错误链、漏消费、`main` 错误退出码。

## P4 注解处理器 / proxy / 反射

- `annotation` 处理器执行轮次（目标 Sema 后、Lower 前）。
- 只增声明；`line(file,line)` 生成 `#line`；禁止改结构/其他注解。
- `description` / `descriptor` / `reflection` / `reflector<T>`。
- proxy 三模式（编译期 / runtime / invocation）；trap 展开。
- 测试：`@SourceMap` 错误映射、`@Serializable` 生成独立函数、proxy 输出顺序。

## P5 模块与工具链

- `.coox` 序列化、import、循环检测、拓扑初始化、增量。
- `coordination.toml`、包依赖、锁文件。
- `extern "C++"` 类型映射与 ABI 包装。
- 标准库 v0 与测试框架。

## 每项的完成定义（DoD）

- 规范段落已更新，并与实现一致；
- 正例、负例至少各一个测试；
- `ctest` 全绿；
- 示例可 `coorc run`（涉及运行期时）；
- [docs/archive/index.md](index.md) 功能矩阵与 [05-roadmap.md](05-roadmap.md) 状态同步。
