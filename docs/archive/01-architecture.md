# 01 · 总体架构

## 1. 定位

Coordination 是**通用系统级语言**，把“编译期计算”作为一等能力，而不是外挂
宏系统。它同时满足四个约束：

1. **没有宏**：不存在 token 替换、字符串拼代码或文本级展开。
2. **相位由上下文决定**：没有 `comptime` / `constexpr` / `consteval` 关键字；
   注解处理器、注解参数、`const` 初始化器、泛型实参天然在编译期执行。
3. **反射零运行期开销**：`description` / `descriptor` / `reflection` /
   `reflector` / `proxy` 只存在于编译期；代码生成后不保留元数据。
4. **RAII 复用 C++ 语义**：构造/析构顺序、逆序销毁、移动后仍析构、无 GC。

首个后端把检查后的程序降级为 C++20 单翻译单元，再交给宿主 C++ 编译器链接。
这样可以在 M0 就用真实编译器验证语义，同时把 ABI、调试信息、优化交给成熟
工具链。

## 2. 设计原则

| # | 原则 | 含义 / 代价 |
| --- | --- | --- |
| P1 | 一套语义，无宏 | 编译期代码就是普通声明与表达式；没有第二套语法。 |
| P2 | 显式相位，静态可判定 | 编译期上下文由语法位置决定，禁止运行期值泄漏进编译期。 |
| P3 | 反射是数据不是字符串 | 查询得到 `description`/`reflector`，不靠名字拼字符串。 |
| P4 | 生成只增不改 | 处理器只能追加新声明，不能改写已有声明，保证可增量、可预测。 |
| P5 | RAII 即 C++ RAII | 构造：基类 → 成员声明序 → 体；析构：体 → 成员逆序 → 基类。 |
| P6 | 错误是值 | 默认隐藏 `Result<T, Error>` 并沿调用栈冒泡；无异常。 |
| P7 | 默认安全，逃逸显式 | 指针运算/解引用需 `unsafe`；`new/delete` 仅 `unsafe`。 |
| P8 | 单态化优先 | 泛型默认单态化，与 C++ 模板后端自然对应。 |
| P9 | 可诊断 | 所有诊断带源码区间与稳定错误码；编译期执行有 fuel/内存上限。 |
| P10 | 渐进后端 | C++20 → `.coox` 模块 → LLVM/自研后端 → 自举。 |
| P11 | 每个 `{}` 是 Scope | Scope 同时决定名字可见性、遮蔽与对象生命周期边界。 |

## 3. 十四个基础类型

“基础类型”是语言的**类型宇宙**：任何值/声明的类型最终落在其中之一。它们不是
十四个关键字别名，而是十四个根类型（root type）；具体类型是它们的子类型。

| 根类型 | 本质 | 存在相位 | 典型值 / 用途 | 运行期表示 |
| --- | --- | --- | --- | --- |
| `integer` | 整数根 | 运行期 | `int`(64 位)、`i8..u64`、`isize/usize` | 定宽整数 |
| `decimal` | 十进制浮点根 | 运行期 | `f32`、`f64` | IEEE-754 |
| `character` | Unicode 标量根 | 运行期 | `char`（32 位标量） | UTF-32 码点 |
| `boolean` | 布尔根 | 运行期 | `bool` | 1 字节 |
| `Enumeration` | 枚举根 | 运行期 | 具体 `enumerate` 类型 | 整数 + 可选旁路名字表 |
| `class` | 名义对象类型根 | 运行期 | 用户 `class`、`interface` | 结构体 + vtable |
| `annotation` | 注解类型根 | 编译期 | 用户 `annotation`，可含处理器方法 | 不存在 |
| `function` | 可调用类型根 | 两相 | 函数、方法、算子、闭包 | 函数指针/`std::function` |
| `description` | 声明属性描述符 | 编译期 | class/function/method/variable 的属性快照 | 不存在 |
| `descriptor` | 描述符处理器 | 编译期 | 接收 `description`，产出新声明 | 不存在 |
| `reflection` | 擦除式反射游标 | 编译期 | 动态查询 kind/name/member | 不存在 |
| `reflector` | 类型化反射视图 | 编译期 | `reflector<T>`，静态检查成员访问 | 不存在 |
| `proxy` | 行为代理根 | 编译期展开或运行期保留 | `@Proxy(Handler)`；默认零开销，`@Retention` 时运行期分派 | 默认无对象；保留模式为包装类型 |
| `string` | UTF-8 文本 | 运行期 | 独立于 `description` 的字符串类型 | `std::string` |

> `description` 是**属性描述符**：描述 class/interface/enum/annotation/function/
> method/variable 的属性；`descriptor` 是**处理属性描述符的处理器**。二者不是
> 同义词。

辅助类型：`void`、`interface`、`Result<T, E>`、`Expect<T, E>`。具体 `enumerate`
类型是 `Enumeration` 的子类型；`enumerate` 是声明关键字。

## 4. 相位模型

没有相位关键字，相位由**语法上下文**决定：

```
编译期上下文
  ├─ annotation 声明体中的处理器方法
  ├─ 注解实参
  ├─ const 初始化器
  ├─ 泛型实参 / 约束
  └─ .coox 元数据构造

运行期上下文
  └─ 普通函数/方法/构造/析构体
```

规则：

- 编译期上下文可以调用普通纯函数；解释器按值执行，遇到 IO/时间/随机/可变全局
  即报错。
- 运行期上下文不能持有 `annotation`/`description`/`descriptor`/`reflection`/
  `reflector`/`proxy` 类型的值（`E2009`）。
- 编译期执行受 fuel 与内存上限约束，结果按内容寻址缓存；同一输入必须得到同一
  输出。
- 错误模型与 RAII 在编译期同样成立：编译期求值失败就是编译错误；编译期控制流
  退出时同样按逆序销毁（解释器层面是作用域栈）。

### 4.1 Scope

每个 `{ ... }` 是一个 Scope；它是语言的一等概念，而不是实现细节：

| Scope | 引入 | 生命周期终点 |
| --- | --- | --- |
| Module | 文件顶层 | 模块检查结束 |
| Type | class/interface/annotation 的 `{}` | 类型检查结束 |
| Function | 参数列表 | 函数返回 |
| Block | 语句块 `{}` | 块退出（`}`/`return`/`break`/`continue`/错误冒泡） |

- 名字从内向外查找；内层可遮蔽外层；**同域重复声明是错误**（`E2002`）。
- 局部对象在 Scope 退出时按构造逆序析构。
- Sema、Interpreter、Codegen 各自维护 Scope 栈；`{}` 与 C++ 花括号一一对应，
  因此析构顺序由语言与后端共同保证。
- 参数在 Function Scope，函数体最外层局部变量在 Block Scope，二者同名合法。

## 5. 编译生命周期

`coorc` 的流水线是显式的，每一阶段都可以单独停止：

```
① Load        读入 .coo，UTF-8 校验，SourceManager 持有缓冲
② Lex         Token 流（关键字、字面量、注释、运算符）
③ Parse       递归下降 → AST（带 Span）
④ Sema        根类型注册、名字/类型解析、继承/泛型约束、RAII 检查、可见性
⑤ ConstEval   解释 const 初始化器与注解处理器；产出常量表与生成声明
⑥ Lower       检查后的 AST → C++20 翻译单元（单态化、模板、RAII 映射）
⑦ Host CC     cl / c++ / clang++ 编译 + 链接（可注入 --cxx）
⑧ Run         （仅 `coorc run`）执行产物
```

CLI 映射：

| 命令 | 停止阶段 | 产物 |
| --- | --- | --- |
| `coorc check file.coo` | ⑤ | 诊断 / 退出码 |
| `coorc build file.coo` | ⑦ | 可执行文件（默认同名 `.exe`） |
| `coorc build --emit-cpp out.cpp file.coo` | ⑥ | C++20 源文件 |
| `coorc run file.coo` | ⑧ | 运行并返回其退出码 |

## 6. 编译器工程结构

```
libs/coord/
  include/coord/
    support.h      Span / SourceManager / DiagnosticEngine
    syntax.h       TokKind / Token / Lexer
    ast.h          AST 节点 + dumpAst
    parser.h       Parser
    sema.h         Type / FieldInfo / MethodInfo / Checker
    interp.h       Value / Interpreter（编译期求值）
    codegen.h      CppEmitter
    driver.h       CompileOptions / runCompiler
  src/             对应 .cpp
tools/coorc/      命令行
tests/             单元测试 + e2e.cmake
```

依赖方向严格单向：

```
support → syntax → ast → parser → sema → interp → codegen → driver → coorc
```

`codegen` 只依赖 `sema` 的类型与 `interp` 的常量表；不反向依赖 driver。
后续可把每个目录提升为独立静态库，接口不变。

## 7. 关键决策

| ID | 决策 | 理由 | 代价 |
| --- | --- | --- | --- |
| D1 | 无宏，注解 + 反射 + 处理器生成 | 无文本替换，错误定位与卫生性可控 | 需要设计处理器/生成 API |
| D2 | 相位由上下文决定，无 `comptime` 关键字 | 语法更小；注解即元编程入口 | 相位错误需靠类型系统诊断 |
| D3 | 错误默认 `Result` 隐藏并冒泡 | 调用点干净，无异常 | 签名与 ABI 需要隐藏参数/返回槽 |
| D4 | `@Result` / `@Expect` 不改声明返回类型 | 源码保持直观；由注解改变调用侧约束 | 类型检查器要维护“有效返回类型” |
| D5 | 单继承 + 多 interface | 布局简单，vtable 可控 | 多接口冲突需消歧 |
| D6 | `virtual` + `override` 强制，`final` 可修饰类/方法/字段 | 覆写显式，避免意外 | 代码略啰嗦 |
| D7 | `private` 可覆写、不可直接调用 | 支持 NVI / 私有虚方法 | 可见性与可覆写性解耦，教学成本 |
| D8 | 同名字段隐藏（两份都在）；重载跨继承自动合并 | 与 C++ 字段语义一致；调用更自然 | 名字解析要区分字段/方法 |
| D9 | 同 C++ 的 RAII；显式 `~this.member;` 仅作完整性声明 | 后端零成本，避免二次析构 | 显式调用被擦除，易误解 |
| D10 | C++20 后端优先 | 最快端到端；复用 ABI/调试/优化 | 受 C++ 语义与构建环境约束 |
| D11 | 单文件 = 模块，`.coox` 为接口单元 | 增量编译粒度清晰 | 需要序列化与版本化 |
| D12 | `int` 为 64 位有符号 | 索引/大小统一 | 与 C++ `int` 互操作需显式映射 |
| D13 | 注解只读、只增 | 不覆盖/修改其他注解，不改目标结构；只生成新声明 | 生成代码只能以独立声明存在 |
| D14 | 编译器指令只有 `line` | 用户注解通过处理器调用 `line(file,line)` 设置 `#line`；固定原语、绑定目标、不改结构 | 不能实现 `#warning`/`#pragma` 等文本指令 |
| D15 | 元注解仅内建 | `@Inherit`/`@Repeatable`/`@Target`/`@Meta`/`@Retention` 定义注解行为；`@Inherit` 直接优先、继承补缺 | 用户不能定义新元注解或改语义 |
| D16 | `@Retention` 是保留阶段开关 | 裸写 = runtime（旁路只读表 + 逻辑）；`@Retention(invocation)` = runtime + 值 hook；proxy 据此选择运行期模式 | 默认零运行期足迹；运行期仍无通用反射 |
| D17 | Invocation 只改值 | 调用前后、字段 get/set、构造后可改参数/返回值/字段值；类型必须可赋值，签名/成员/布局/vtable 不变 | handler 可有状态，状态在旁路存储 |
| D18 | 枚举是第 14 个根类型 | `enumerate` 声明关键字，`Enumeration` 根类型；C 风格整数后端、可标注变体、可有方法、可 implements 接口 | 不参与 class 继承；`match` 强制穷尽 |
| D19 | `@Retention(enum)` 保留枚举元数据 | 把变体名字/序号表放旁路只读表；默认整数后端无名字表 | 不改变枚举值布局/ABI；不开启 invocation |

## 8. 风险与缓解

| 风险 | 表现 | 缓解 |
| --- | --- | --- |
| 编译期膨胀 | 解释器慢、内存高 | fuel + 内存上限 + 内容缓存；惰性实例化 |
| 隐藏 Result 的 ABI | 跨 C++ 边界不稳定 | 先定义 Coo ABI，`extern "C++"` 边界显式包装 |
| 注解/代理语义过强 | 变成事实上的宏 | 只允许生成新声明；禁止改写/文本拼接 |
| 反射 API 不稳定 | 用户代码易碎 | 五层模型分层演进；`.coox` 版本化 |
| C++ 后端泄漏 | 布局/异常语义不一致 | 生成代码只使用受限子集；E2E 可观察测试 |
| 诊断质量 | 生成代码错误难定位 | `#line` 映射 + 生成节点保留源 Span |

## 9. M0 状态

M0 实现的是“可验证的垂直切片”，不是完整语言：

- 已实现：词法/语法、类/接口/函数/泛型约束/注解语法、单继承、
  `implements`、`this`、Scope 栈与遮蔽、`enumerate` 纯枚举
  （变体值/`Color.Red`/`print`）、`const` 求值、C++ 降级、
  `build` 直出可执行文件、RAII 合成析构与 `W0001` 告警、
  `~this.member;` 检查、诊断、测试。
- 规划中：Result/Expect 降级、注解处理器、proxy trap 展开、反射 API、
  `.coox`、访问段检查、`final/override/super` 语义、模式匹配、标准库。

未实现项不影响本文件的架构成立；它们按 [05-roadmap.md](05-roadmap.md) 推进。
