# Coordination 框架规范

> Coordination 是面向系统编程的静态类型语言：没有宏，编译期元编程由注解、描述符、反射与 proxy 驱动；对象生命周期复用 C++ RAII，当前后端输出 C++20。本文是框架、语言与运行时契约的唯一权威叙述。

- 语言名：**Coordination**；源文件 `.coo`；模块接口 `.coox`。
- 编译器：`coorc`；包清单 `coordination.toml` / `coord.pkg`；锁文件 `coord.lock`。
- 配套文档：[快速上手（中文）](readme.zh-CN.md)｜[README (English)](readme.md)｜[Framework (English)](framework.md)。
- 形式文法：[docs/grammar.ebnf](docs/grammar.ebnf)。
- 历史文档：[docs/archive/](docs/archive/) 仅为历史归档，**以本文为准**。
- 状态图例：✅ 已实现并有测试｜🚧 部分实现（进行中）｜⏳ 仅有规范/设计，未实现。

## 1 定位与原则

### 1.1 定位

Coordination 把“编译期计算”当作一等能力，而不是外挂一个宏系统。以下四条是语言约束，不是建议：

- **没有宏**：不存在 token 替换、字符串拼代码或文本级展开；编译期元编程的入口只有注解。
- **相位由上下文决定**：没有 `comptime` / `constexpr` / `consteval` 关键字；注解处理器、注解实参、`const` 初始化器、泛型实参天然在编译期执行。
- **默认零运行期反射**：`description` / `descriptor` / `reflection` / `reflector` / `proxy` 只存在于编译期；只有显式 `@Retention` 才物化旁路只读表，且不改变对象布局。
- **RAII 复用 C++ 语义**：构造/析构顺序、逆序销毁、无 GC；对象生命周期是语言硬语义，不是库设施。

首个后端把检查后的程序降级为 C++20 单翻译单元，再交给宿主 C++ 编译器编译链接。这让 M0 起就能用真实编译器验证语义（RAII 顺序、ABI、调试信息、优化由成熟工具链负责）；代价是语言语义必须落在 C++ 可精确表达的子集内。

### 1.2 设计原则

| # | 原则 | 含义 / 代价 |
| --- | --- | --- |
| P1 | 一套语义，无宏 | 编译期代码就是普通声明与表达式，没有第二套语法。 |
| P2 | 相位由上下文决定，静态可判定 | 编译期上下文由语法位置决定，运行期值不得泄漏进编译期。 |
| P3 | 反射是数据不是字符串 | 查询得到 `description` / `Reflector<T>`，不靠名字拼字符串。 |
| P4 | 生成只增不改 | 处理器只能追加新声明，保证可增量、可预测、可缓存。 |
| P5 | RAII 即 C++ RAII | 构造：基类 → 成员声明序 → 体；析构：体 → 成员逆序 → 基类。 |
| P6 | 错误是值 | 默认隐藏 `Result<T, Error>` 并沿调用栈冒泡；无异常。 |
| P7 | 默认安全，逃逸显式 | 指针运算/解引用、`new`/`delete` 需 `unsafe`。⏳ |
| P8 | 单态化优先 | 泛型默认单态化，与 C++ 模板后端自然对应。 |
| P9 | 可诊断 | 诊断带源码区间与稳定错误码；编译期执行有 fuel/内存上限。 |
| P10 | 渐进后端 | C++20 → `.coox` 模块 → LLVM/自研后端 → 自举。 |
| P11 | 每个 `{}` 是 Scope | Scope 同时决定名字可见性、遮蔽与对象生命周期边界。 |

### 1.3 关键决策

| # | 决策 | 理由 / 代价 |
| --- | --- | --- |
| D1 | 类型宇宙共 15 项：14 个根类型，加上独立运行期文本类型 `string` | 任何值/声明的类型最终落在根类型上；`string` 不参与元数据五层。 |
| D2 | 反射五层：`description`、`descriptor`、`reflection`、`Reflector<T>`、`proxy` | 数据与行为分开，动态与静态分开。 |
| D3 | 无宏；注解只读、只增；用户可自定义注解 | 错误定位、卫生性、可缓存；代价是生成物只能以独立声明存在。 |
| D4 | 相位由上下文决定，无 `comptime` 关键字 | 语法更小；相位错误靠类型系统诊断。 |
| D5 | 元注解只有内建 `@Inherit`、`@Repeatable`、`@Target`、`@Meta`、`@Retention(stage)`、`@Documented` | 用户不能定义新元注解或改语义；`@Inherit` 直接注解优先、继承补缺。 |
| D6 | 默认隐藏 `Result<T, Error>` 自动冒泡；`@Result` 暴露、`@Expect` 必须消费；构造函数不可失败；无异常 | 调用点干净；ABI 需要隐藏返回槽。 |
| D7 | 每个 `{}` 一个 Scope；同域重复声明报错；局部对象逆序析构 | 名字可见性、遮蔽、生命周期共用同一个边界。 |
| D8 | RAII 顺序固定；无用户析构则合成；用户析构必须为每个 class 类型属性写 `~this.member;`（缺失 `W0001`、基本类型 `W0002`、重复 `W0003` 且最后一次生效）；C++ 后端擦除这些调用 | 后端零成本、避免二次析构。 |
| D9 | 单继承 + 多 interface；class 方法默认静态、`virtual` 显式；`override` 强制；`final` 可修饰类/方法/字段；`private` 可覆写但不可直接调用；`super` 访问父实现；同名字段隐藏；重载自动合并；协变返回；基类在前布局；含 virtual 方法自动 virtual 析构；C++ 风格访问段，默认 `private` | 布局简单、覆写显式；代价是声明略啰嗦。 |
| D10 | 指针后缀 `T*`；引用 `T&`；`const T*` 是“指向 const T 的指针” | 声明从内向外读，与 C++ 对齐。 |
| D11 | `enumerate` 声明、`Enumeration` 根类型；C 风格整数后端；默认 0/前值+1；变体名与值唯一；变体可标注；可定义方法、可 `implements`；`match` 强制穷尽；默认无名字表，`@Retention(enum)` 才生成旁路 `(name,value,ordinal)` 表 | 不参与 class 继承，ABI 稳定。 |
| D12 | 源文件 UTF-8/UTF-16LE/BE/UTF-32LE/BE，BOM 可选并剥离，内部 UTF-8；标识符允许 Unicode 字母/数字/下划线；`char` 是 32 位标量 | 与 Unicode 语义一致；后端映射到 `char32_t`。 |
| D13 | 一个 `.coo` = 一个模块；`export` 控制导出；`.coox` 是接口/元数据；`coordination.toml`/`coord.pkg` 工程清单 + `coord.lock` | 增量编译粒度清晰；清单与锁文件已实现。 |
| D14 | 生命周期 Load → Lex → Parse → @Processor → Sema → ConstEval → C++ Lowering → Host CC/Link → Run | 每阶段可单独停止，便于测试与定位。 |
| D15 | 编译器由 9 个核心阶段头 + 38 个 `modules/*` 模块头组成，单向依赖 | 可逐阶段迁移为独立库；IO/平台相关模块用 `.cpp` 实现。 |
| D16 | Stream 契约：内部 LF、读时 CRLF/CR 归一、写回 LF、行号 0 基、字节偏移是规范化 UTF-8 偏移 | 跨平台行号/偏移一致。 |
| D17 | 跨平台边界集中到 `modules/platform`：UTF-8 路径、目录约定、可执行/对象/库命名、`argv` 直接 spawn（无 shell）、Windows UTF-16 命令行 | 编译器/包管理器不写 `#ifdef _WIN32`。 |
| D18 | 包管理：双清单格式；semver 约束 + 路径优先 + 注册表回溯；`coord.lock` 固定版本/SHA-256；vendor 到 `coord_modules/`；注册表为目录树，镜像按优先级与健康状态故障转移 | 可复现、可离线、可镜像。 |
| D19 | 信任边界：归档 SHA-256 + HTTPS（系统 curl）+ 发布 token；不实现包签名/信任链 | 工具链无外部密码学依赖。 |
| D20 | 所有代理对象共享四种 `ProxyPolicy` 策略：`Error`（默认）、`Skip`、`Abort`、`Default(expr)`；`@Optional` 默认 `Error`，`@Proxy(handler)` 默认 `Skip`；`Error` 下未处理的直接代理使用是编译错误 `E2048` | 用一套策略模型覆盖 Optional、`@Proxy`、构造与解构；不引入第二套空值系统。 |
| D21 | lowering 生成内部代理层；目标本身是 `@Proxy` 时 Optional 在外层、用户 handler 在内层 | 策略检查先于用户 trap，缺失路径不调用用户 handler。 |
| D22 | 生成程序跨平台：C++20 + 条件 `<windows.h>`；Windows/MSVC 用 `wmain` + UTF-16→UTF-8 argv + UTF-8 控制台；POSIX 用 `main` | 源码/路径/控制台在三大平台行为一致；平台差异只进入生成前导。 |
| D23 | 交叉编译：`--target <triple>`（clang `--target`）、`--sysroot <path>`、GCC 交叉工具链用 `--cxx`；MSVC 不支持交叉编译；交叉目标不能 `run` | 复用宿主 C++ 工具链，不内建代码生成器。 |
| D24 | `@Constructure`（`@Target(constructor)`）支持类型化/命名字面量构造；`@Destructure` 既是 `ProxyHandler` trap，也是带引用输出的目标类方法 | 构造与解构保持用户定义，不做隐式结构转换。 |
| D25 | 类型注解（`@Optional`、`@Unique`、`@Share`、`@Signed`、`@Unsigned`、`@Length`、`@Radix`、`@Weak`）的目标是 `type`；`@Constructure` 的目标是 `constructor`；`@Target` 接受这两个名字 | 注解附着在类型上而不是声明上，别名与组合类型共享同一语义。 |

### 1.4 用什么替代宏

四条通道缺一不可：**注解**把意图声明在目标上；**description** 把目标属性变成结构化数据；**descriptor / 注解处理器**在编译期读取 description 并生成新声明；**proxy** 在编译期把行为包在调用点外面。生成物只能是**新声明**：不能改写既有声明，不能拼接 token，不能输出任意 `#...` 文本，也不能修改目标结构。

### 1.5 文档地图

| 文档 | 内容 |
| --- | --- |
| `framework.md` / `framework.zh-CN.md` | 框架、语言、反射、编译器、标准库摘要、包协议与后端映射（本文）。 |
| `readme.md` / `readme.zh-CN.md` | QuickStart、构建/测试、CLI、包与镜像简明用法、跨平台说明。 |
| [docs/grammar.ebnf](docs/grammar.ebnf) | 形式文法附录；解析器实现与本文冲突时以代码为准并回写本文。 |
| [docs/archive/](docs/archive/) | 旧架构/规范/路线图，仅作历史参考，不再维护。 |

## 2 类型宇宙

### 2.1 根类型（15 项）

根类型是语言的类型宇宙：任何值或声明的类型最终落在其中之一，具体类型是它们的子类型。

| 根类型 | 本质 | 存在相位 | 典型值 / 用途 | 运行期表示 |
| --- | --- | --- | --- | --- |
| `integer` | 整数根/基本类型 | 运行期 | 默认 64 位有符号；`@Unsigned` 无符号，`@Signed` 显式有符号，`@Length(N)` 指定位宽 | 定宽整数 |
| `float` | 二进制浮点根 | 运行期 | `float`（C++ `double`），规划中的 `f32`/`f64` | IEEE-754 |
| `decimal` | 十进制定点根 | 运行期 | `decimal`（value + scale），`@Length` 收窄存储 | 定点 |
| `character` | Unicode 标量根 | 运行期 | `char`（32 位标量） | UTF-32 码点 |
| `boolean` | 布尔根 | 运行期 | `bool` | 1 字节 |
| `Enumeration` | 枚举根 | 运行期 | 具体 `enumerate` 类型 | 整数 + 可选旁路名字表 |
| `class` | 名义对象类型根 | 运行期 | 用户 `class`、`interface` | 结构体 + vtable |
| `annotation` | 注解类型根 | 编译期 | 用户 `annotation`，可含处理器方法 | 不存在 |
| `function` | 可调用类型根 | 两相 | 函数、方法、算子、闭包 | 函数指针 / 闭包对象 |
| `description` | 声明属性描述符 | 编译期 | class/function/method/variable 的属性快照 | 不存在 |
| `descriptor` | 属性描述符处理器 | 编译期 | 接收 `description`，产出新声明 | 不存在 |
| `reflection` | 擦除式反射游标 | 编译期 | 动态查询 kind/name/member | 不存在 |
| `reflector` | 类型化反射视图 | 编译期 | `Reflector<T>`，静态检查成员访问 | 不存在 |
| `proxy` | 行为代理根 | 编译期展开或运行期保留 | `@Proxy(Handler)`；默认零开销 | 默认无对象；保留模式为包装 |
| `string` | UTF-8 文本 | 运行期 | 独立文本类型，不属于元数据五层 | `std::string` |

计数口径：根类型 14 个（`integer` … `proxy`），加上独立运行期文本类型 `string`，合计 15 项。`float` 是二进制浮点数（C++ `double`）；`decimal` 是 `value + scale` 十进制定点数。`string` 是**独立**的：它不是 `description` 的别称，不参与反射与保留表，不能承载编译期元数据。

辅助类型：`void`（单元类型）、`interface`（由 `class` 根类型覆盖的名义接口）、`Result<T, E>` 与 `Expect<T, E>`。

易混概念：`description` 是**数据**（只读快照），`descriptor` 是**行为**（处理器）；`reflection` 是擦除层，`Reflector<T>` 是类型化层；`proxy` 不是新类型，而是调用点重写。

### 2.2 内建具体类型

- 整数：`integer`（默认 64 位有符号）；`@Unsigned` 映射无符号，`@Signed` 显式有符号；`@Length(8/16/32/64/128)` 指定位宽，默认 64。没有 `int`、`i8`、`usize` 等别名。
- 浮点：`float` 是二进制浮点数（C++ `double`）；`decimal` 是十进制定点数（value + scale）；`f32`/`f64` 为规划中的显式宽度写法。
- 文本：`char`（32 位 Unicode 标量，不是字节）、`string`（不可变 UTF-8 字节序列，运行期）。
- 其他：`bool`、`void`、`record`/`Record`；规划：`Result<T, Error>`、`Expect<T, Error>`、`Error`、`@Unique`/`@Share` 所有权注解、`T[N]`、`Array<T>` 及其视图。
- 标准库风格的大写类型名（`Integer`、`String`、`Character`、`Boolean`、`Float`、`Decimal`、`Record`）与对应小写关键字解析到同一个 `Type*`，诊断与反射使用规范名。

`decimal` 的定点规则：后端表示为 `coord_rt::Decimal { value; scale; }`；`+`/`-`/`%` 先对齐 scale；`*` 的 scale 相加；`/` 保留左操作数 scale；位运算先对齐 scale；`<<`/`>>` 只移动 value、scale 不变；`print` 去掉多余尾零。`@Length(8/16/32/64)` 会把 value/scale 字段收窄到对应宽度，算术时提升到 64 位再写回；`@Length(128)` decimal 尚不支持。

### 2.3 子类型与可赋值

- 名义子类型：`S <: T` 当且仅当 `S == T`、`S` 的基类链包含 `T`、`S` 实现了接口 `T`，或 `S` 是 `T` 的联合/交集成员。
- 泛型默认不变；`N extends integer` 表示 `N` 的每个实例都必须是 `integer` 的子类型；`class` 根类型是名义类型的上界。
- 联合 `A | B`、交集 `A & B` 用于类型与约束；`&` 在参数类型位置是引用，在约束位置是交集（上下文消歧）。
- 可赋值：子类型关系成立即合法；此外 `integer` 可隐式赋给 `decimal`，数值类型之间允许加宽；指针按被指类型协变（M0 行为）。
- 规划：`where` 结构约束、const 泛型（数组长度）、泛型接口、union/intersection 的完整检查与 `match` 收窄（⏳）。

### 2.4 可空性与 ProxyPolicy

`@Optional` 不是根类型，也不是 `Nullable<T>` 包装类型，而是由 `@Target(type)` 注解承载的**类型状态**：

- 只用于类型 `T*`、`@Unique T`、`@Share T`；普通 `@Optional Class` 仅用于描述符场景，**不承诺运行期可空**。
- 非 optional 的 `T*`/`@Unique`/`@Share` 默认非空；赋 `null` 报 `E2046`；`@Optional` 用于其它类型报 `E2047`。
- 可空值规则：`null` 只能赋给 optional。默认 `Error` 策略下，optional 到非 optional 必须收窄或 `orElse`/`get()`；`Skip`/`Abort`/`Default(expr)` 下按策略物化为非 optional 值。非 optional 到 optional 是安全加宽；部分隐式转换边界仍属 🚧。
- 容器元素的可空性暂不能靠类型注解表达；v0 在字段/变量/参数/返回值的类型位置支持 `@Optional`，后续用类型级 `Nullable<T>`。
- **ProxyPolicy** 由所有代理对象共享，不只属于 Optional；Optional 只是代理的一种，`@Proxy` 对象、构造与解构使用同一套四策略。见 §4.5.2。

详细访问策略见 §4.5.2，Optional 代理与解构见 §4.5.3。

## 3 Scope 与相位

### 3.1 Scope 种类与 Scope Graph

语言可见的 Scope 边界有四类：

| Scope | 引入方式 | 内容 | 生命周期终点 |
| --- | --- | --- | --- |
| Module | `.coo` 文件顶层 | import、类型、函数、常量 | 模块检查结束 |
| Type | `class` / `interface` / `annotation` / `enumerate` 的 `{}` | 字段、方法、构造/析构、嵌套类型 | 类型检查结束 |
| Function | 函数/方法/构造/析构的参数列表 | 参数 | 函数返回 |
| Block | 语句块 `{ ... }` | 局部变量、局部常量 | 块退出（`}`、`return`、`break`、`continue`、错误冒泡） |

实现层把名字解析建模为 **Scope Graph**，节点可细分为 Module / Namespace（后续）/ Class / Function / Method / Block / TypeParam。每个节点持有 `id`、`kind`、`parent`、`owner`、声明集合、import 边、继承边与可见性规则。

边标签与解析关系：

| 边 | 含义 | 是否参与名字解析 |
| --- | --- | --- |
| `LEX` | 词法父作用域 | 是 |
| `MEM` | 类成员 / 方法所属类 | 是 |
| `EXT` | 基类 / 实现接口 | 是 |
| `IMP` | 模块 import | 是 |
| `EXP` | re-export | 是 |
| `TP` | 类型参数 | 是 |
| `DEP` | 调用 / 类型依赖 | **否** |

可见性公式：

```text
Visible(S) = Declarations(S, before use) ∪ Visible(parent(S))   // 子遮蔽父
Visible(b) ∩ Visible(c) = 公共祖先声明（经遮蔽后）
```

依赖不进入公式：**依赖不授予可见性**。`b` 能调用 `a()`，是因为 `a` 已通过词法父作用域、import 或显式限定可见，而不是因为“调用了 a”。

规则要点：

- 名字从最内层向外查找，找到即停；内层可遮蔽外层；同一 Scope 内重复声明是错误（`E2002`）。
- 同级 Scope 完全隔离；不因为文本相邻或存在调用关系而互相可见；依赖图允许环，初始化顺序必须检测环。
- 局部变量只能看到声明点之后；类型、函数、类成员允许前向引用（两遍收集）。
- 函数体是 Block scope，嵌套在 Function scope 内；参数与函数体最外层局部变量同名合法。
- 类体是 Type scope：字段不参与局部变量查找，必须用 `this.field` 或成员函数内的裸字段名；裸名解析顺序为局部 Scope → 当前类字段 → 模块常量。
- `this` 只在 Type scope 内可见；脱离类/接口/注解上下文使用 `this` 是 `E2004`。

解析顺序（高到低）：

1. 当前块中声明点之前的局部变量/参数；
2. 当前函数/方法的类型参数；
3. 当前类成员；
4. 基类成员（按继承顺序，歧义报错）；
5. 模块级声明；
6. 显式 import / alias；
7. 通配 import（最低）。

同层多个候选必须报歧义，不能“先找到先用”；私有/受保护成员解析后还要过访问检查。规划中的 Scope 专用诊断码为 `E2080`–`E2086`（不可见、同域冲突、歧义、声明点前使用、import 冲突、私有访问、初始化环）。

### 3.2 Scope 归属与初始化顺序

- 每个对象有唯一的 **owner Scope**：局部变量属于 Block/Function，字段属于其对象，静态/全局属于 Module。
- owner Scope 退出时按声明逆序析构其拥有的对象；不 detach、不延长、不计数。
- 类初始化：基类先于子类；成员按声明顺序；静态/模块初始化按依赖图拓扑排序；初始化依赖环在编译期可判定时报错。
- 名字解析与初始化顺序是两件事：依赖边只用于初始化顺序、生命周期、诊断和 outline。

### 3.3 生命周期

- 进入 Block 时局部对象按声明顺序构造；离开时按构造逆序逐个析构。提前离开（`return`、`break`、`continue`、错误冒泡）同样触发逆序析构。
- 编译器合成的析构函数按成员声明逆序析构成员，最后析构基类。
- Scope 边界与 C++ 的 `{}` 一一对应；后端直接保留花括号，析构顺序由语言与后端共同保证。
- 临时对象在完整表达式结束时析构；移动后的源对象仍然会被析构，析构函数必须接受 moved-from 状态。

### 3.4 相位模型

没有相位关键字，相位由**语法上下文**决定：

```text
编译期上下文：annotation 处理器方法 / 注解实参 / const 初始化器 / 泛型实参·约束 / .coox 元数据构造(⏳)
运行期上下文：普通函数 / 方法 / 构造 / 析构体
```

- 编译期上下文可以调用普通纯函数：解释器按值执行；遇到 IO、时间、随机、可变全局即报错。
- 运行期上下文不能持有 `annotation`/`description`/`descriptor`/`reflection`/`reflector`/`proxy` 类型的值（`E2009`）。
- 编译期执行受 fuel 与递归深度上限约束：M0 为 2,000,000 次求值 / 512 层调用；超限是编译错误（`E3001`），不是挂起。
- 同一输入必须得到同一输出；结果按内容寻址缓存是目标（⏳，M0 无缓存）。编译期错误就是编译错误；编译期控制流退出时同样按逆序销毁（解释器维护 Scope 栈，回卷时必须弹栈）。
- 无法在编译期求值的 `const` 初始化器保留给运行期（M0 行为）；能求值的进入常量表并参与后续求值。

## 4 语言规范

本节规定语言的**目标语义**：状态以 §11 为准；未标 ⏳ 的条目要么已生效，要么只缺负例检查。逐项实现状态见 §11。

### 4.1 词法

- 源文件：`.coo`；模块接口 `.coox`。支持 UTF-8、UTF-16LE/BE、UTF-32LE/BE；BOM 可选，读取时剥离；内部统一 UTF-8。`Span` 是规范化 UTF-8 字节偏移（半开区间）；非法码点替换为 U+FFFD，不中断词法分析。
- 注释：`//` 行注释；`/* ... */` 块注释，可嵌套；`///` 与 `/** */` 文档注释挂到下一个声明，Sema 校验 `@parameter`/`@param`、`@return`、`@template`，未知或重复标签报 `E2076`。
- 标识符：Unicode 字母/`_` 开头，后续 Unicode 字母、数字、`_`；关键字为 ASCII，保留字不可作标识符。M0 实现更宽松：非 ASCII 码点只要不是 Unicode 空白/控制字符即可；收紧到“字母/数字”属后续工作。
- 关键字：`module import export as class interface implements extends annotation function void const static virtual override final operator where if else switch while for in return break continue true false null this is match extern unsafe new delete integer decimal float record character boolean enumerate public private protected`。`descriptor`/`description`/`reflection`/`reflector`/`proxy` 不是关键字。“词法保留”不等于“已实现”：`is`/`extern`/`unsafe`/`new`/`delete` 仍是保留字（使用报 `E1101` 或语义未实现）；`match` 已对枚举生效；`operator`/`where` 可解析，完整语义 ⏳。
- 字面量：整数（十进制、`0x`/`0b`/`0o`，允许 `_`）；十进制小数与指数（`1e10`）；字符 `'a'`、`'\u{...}'`；字符串 `"..."`；`true`/`false`。数字后缀不支持（`E1001`）。
- 运算符：`+ - * / % = += -= *= /= %= == != < <= > >= && || ! & | ^ ~ &= |= ^= << >> ++ -- -> => . .. ..= :: ? @ #`。`**` 未进入 token 表；`<<`/`>>` 在表达式层合并相邻 `<`/`>`，不破坏嵌套泛型；表达式语义以 C++ 同优先级子集为准。
- `this` 指当前实例；**不使用 `self`**。

### 4.2 声明

#### 4.2.1 类

```coo
final class File {
public:
    string path;
    File(string p) { this.path = p; }
    ~File() {}
    integer size() const { return 0; }
}
```

- 访问段 `public:` / `private:` / `protected:`，C++ 风格，**默认 `private`**；接口体默认 `public`。单继承 `extends Base`，多接口 `implements I1, I2`。
- 构造：`: super(args)` 调基类构造、`: field(expr)` 初始化成员；顺序固定为基类 → 成员声明序 → 构造函数体。
- 析构：`~Name()`，无参数、不可 `static`/`const`、不可失败；类含 virtual 方法（或实现接口）时析构自动 virtual。
- 字段按声明顺序布局，`private` 字段同样参与；`final` 类不可继承（`E2031`）、`final` 方法不可覆写（`E2032`）、`final` 字段不可被隐藏（`E2032`）。
- 构造与析构不能失败；需要失败语义时用返回 `Result` 的工厂函数（⏳）。
- 初始化形式：`A a{}` 是逐属性聚合初始化（要求聚合类，否则 `E2049`）；`A a()` 走构造函数；`A a = b` 走可访问的单参数转换构造；`A a = { ... }` 用类型化/命名条目选择 `@Constructure` 构造（§4.3.1、§4.8.2）。
- `@Constructure` 是 `@Target(constructor)` 注解，只能标在目标类的构造函数上；被标构造参与类型化/命名字面量构造，构造函数体仍由用户定义并负责实际转换。普通位置 `Type{1, 2}` 构造与它并存。
- `@Radix(2|8|10|16)` 只改变声明初始化、字段默认值或 `const` 值在生成 C++ 中的整数字面量写法：`@Radix(16) integer mask = 255;` 生成 `static_cast<std::int64_t>(0XFF)`，2 进制生成 `0B...`，8 进制生成 C++ 八进制字面量（如 `0100`）。它不改变源码字面量解析，也不改变运行期 `print`；非法进制或非整数目标报 `E2066`。

#### 4.2.2 接口

```coo
interface Drawable {
    void draw() const;
    integer z() const { return 0; }   // 默认实现
}

class Point extends Shape implements Drawable {
public:
    Point() {}
    override void draw() const { print("point"); }
}
```

- 接口方法默认 `virtual`；可提供默认实现；接口不能声明字段（`E2013`）。
- 接口可以继承接口：`interface DestructureProxyHandler extends ProxyHandler {}`。`interface X extends ProxyHandler` 是 proxy handler 子接口支持的形式；实现子接口也满足父接口要求。
- class 方法默认静态分派，`virtual` 显式开启；覆写必须写 `override`（`E2035`），父方法必须 `virtual`（`E2033`），`final` 方法禁止覆写（`E2032`），返回类型允许协变（违反报 `E2034`）。
- private 方法可以被覆写并参与动态分派，但子类不能按名字直接调用父实现（`E2030`）；`super.method()` / `super.field` 访问父实现。多接口同名方法冲突由实现类显式覆写消歧。

#### 4.2.3 枚举

```coo
@Retention(enum)
enumerate Color {
    Red,
    Green = 3,
    Blue,      // 4
}
```

- `enumerate` 是声明关键字；具体枚举类型是根类型 `Enumeration` 的子类型，与 `class` 平行，不参与 class 继承。
- C 风格整数后端：底层类型为 64 位整数；首个变体为 0，后续默认前值 +1；可用整数常量表达式显式赋值（`E2020`）。
- 变体名在同一枚举内唯一（`E2022`）；显式值必须唯一（`E2023`）；变体访问 `Color.Red`，类型是 `Color`；不存在的变体是 `E2024`；支持相等/比较、`switch`、`print`。
- 转换：`Color.Red as integer` 取底层值；`n as Color` 做范围检查，越界 panic（⏳）。
- 枚举可定义方法、可 `implements` 接口；不能声明实例字段/构造函数。枚举类型与单个变体都可被注解（目标 `enumerate` / `enum_value`）。
- `match` 对枚举强制穷尽：未用通配/绑定分支覆盖时必须列出全部变体，缺一变体报 `E2077`。
- 运行期表示是底层整数；**默认不保留名字表**。`@Retention(enum)` 标在 `enumerate` 上时才额外生成旁路只读表 `(name, value, ordinal)`，不改变枚举值布局或 ABI（⏳ P2）；其它 `@Retention` 阶段出现在枚举上是 `E2021`。

#### 4.2.4 注解

```coo
@Retention(invocation)
@Target(method)
annotation Audit(string reason);

class Service {
    @Audit("billing")
    integer charge(integer amount) { return amount; }
}
```

- 注解声明可带参数与处理器方法；处理器方法在编译期执行，接收目标的 `description`。
- 只读、只增（完整规则见 §4.7）：应用不会覆盖/替换/修改先前注解，不能修改目标结构，处理器只能生成新的独立声明；默认同一目标上同类型注解只能出现一次，`@Repeatable` 才可重复。
- 注解默认只存在于编译期，不能作为运行期字段/参数类型（`E2009`）；被 `@Retention` 标注的注解才物化为运行期只读元数据表与生成逻辑。处理器可调用唯一指令原语 `line(file, line)` 设置生成代码的 `#line` 映射。
- 内建注解名（编译器识别，用户不能重定义同名内建）：`@Result`、`@Expect`、`@Invocation`、`@Proxy`、`@Get`、`@Set`、`@Destructure`、`@Construct`、`@Constructure`、`@Has`、`@Delete`、`@Keys`、`@Describe`、`@Call`、`@Copy`、`@Wrap`、`@Native`、`@FunctionalInterface`、`@Unique`、`@Share`、`@Optional`、`@Signed`、`@Unsigned`、`@Length`。
- **目标修正**：`@Optional`、`@Unique`、`@Share`、`@Signed`、`@Unsigned`、`@Length` 是 `@Target(type)` 注解，附着在声明位置的类型表达式上，而不是声明本身；`@Constructure` 是 `@Target(constructor)`。其它注解保持原有的声明目标。
- **分层原则**：元注解只有 6 个，只在 C++ 端实现；其余注解全部是标准库里的普通 Coo 声明（`lib/standard/annotations.coo`）。编译器只保留按名字识别的语义 hook（如 `@Result` 的返回包装、`@Unsigned` 的存储映射），不维护第二套内建注解表。`Descriptor`/`Reflection` 是该文件中的普通类，`Description`/`Reflector<T>` 由编译器特殊处理。
- `@Native(header: string, library: string, symbol: string = "", abi: "c" | "c++" = "c")`：标注无 body 的 class/function/method/variable；编译器在生成的 C++ 中 `#include` header，并在链接阶段把 library 传给宿主链接器。没有运行期 FFI/dlopen；声明不引入 Scope，也不参与运行期反射。

#### 4.2.5 函数、算子与常量

```coo
integer add<N extends integer>(const N& a, const N& b);
Vec operator+(const Vec& lhs, const Vec& rhs);   // 语法 ✅ / 语义 ⏳
const integer FIB10 = fib(10);
const answer = 42;                               // 类型可推断
```

- 函数支持重载（按名字 + 参数个数；按参数类型的重载解析 ⏳）；默认参数可解析（语义 ⏳）；命名参数、闭包（⏳）。
- 泛型参数可带 `extends` 上界；`where` 子句表达结构约束（语法 ✅ / 语义 ⏳）；const 泛型（⏳）。
- `operator` 重载：成员或全局函数，可重载集合由词法运算符表限定（语义 ⏳）。
- `const` 初始化器在编译期求值；没有 `comptime` / `constexpr` 关键字。`const` 可省略类型由初始化器推断；局部 `const` 与顶层 `const` 走同一套编译期规则。
- `print(x)` 是当前后端提供的内建输出（每次一行），标准库的 `Println`/`Format` 将在 P5 接管。

### 4.3 类型语法、语句与表达式

类型形式（与 C++ 从内向外读）：

| 写法 | 含义 | 状态 |
| --- | --- | --- |
| `T*` | 可为零地址的指针（后缀） | ✅ |
| `T&` | 不可为空的引用 | ✅ |
| `const T` / `const T&` | 只读值 / 只读引用 | ✅ |
| `const T*` | 指向 const `T` 的指针（指针本身可改） | ✅ |
| `A \| B` / `A & B` | 联合 / 交集（约束位置） | ✅ 解析 / ⏳ 完整检查 |
| `(A, B) -> R` | 函数类型 | ✅ 解析 / ⏳ 语义 |
| `T[N]` | 定长数组，C++ `std::array<T,N>` | ✅ |
| `T?` | 显式可空标注 | ⏳（未解析） |

- `if` / `match` 目标是**表达式**；块的值是最后一个表达式（`if` 表达式 ⏳，`match` 表达式 ⏳）。
- 语句：块、变量声明、`if`/`else`、`while`、`for`、`for ... in`、`switch`、`match`、`return`、`break`、`continue`、`~this.member;`、表达式语句。
- `++value`、`value++`、`--value`、`value--` 仅接受 integer/decimal lvalue；前缀/后缀的值语义按 C++。`<<`/`>>` 作为位移运算符在表达式层合并相邻 `<`/`>` token。
- 本地 `T&` / `const T&` 已实现；绑定临时量时后端生成隐藏临时对象并把引用绑定到它，生命周期覆盖该引用变量。
- 运算符优先级与 C++ 一致；`<` 在类型上下文解析为泛型实参，在表达式上下文解析为小于。字面量与纯函数调用在 ConstEval 阶段折叠。

#### 4.3.1 对象字面量、@Constructure 与解构

字段初始化对象字面量：

```coo
class Point {
public:
    integer x;
    string y;
}

integer main() {
    Point point = {x: 1, y: "a"};
    {integer x, string y} = point;

    print(x); // 1
    print(y); // a
    return 0;
}
```

- `{x: expr}` 显式字段；`{x, y}` 使用同名变量简写；字段必须存在且可访问。
- 解构 `{integer x, string y} = point;` 在当前作用域声明新变量；绑定按字段名匹配。
- Proxy 构造按字段触发 `@Set`，解构按字段触发 `@Get`；目标类也可以定义 `@Destructure` 方法（引用输出），Proxy handler 可以定义 `@Destructure` trap（§4.8.1）。
- Optional 代理的解构按 `ProxyPolicy` 处理（§4.5.2/§4.5.3）：默认 `Error` 未收窄报 `E2048`；`Skip` 绑定默认值且不读目标字段；`Abort` 运行期终止；`Default(expr)` 解构回退值。
- `...rest` 需要后续 `Record` 视图实现，当前未开放；不支持捕获列表 `[...]`。

**用 `@Constructure` 进行类型化/命名构造**：

```coo
class Point {
public:
    integer x;
    string y;

    @Constructure
    Point(integer x, string y) {
        this.x = x;
        this.y = y;
    }
}

integer main() {
    integer a = 3;
    string b = "hi";

    Point p = {a, b};                  // a、b 已声明
    Point q = {5, "ok"};               // 位置值
    Point r = {y = "named", x = 9};    // 命名参数
    Point s = {integer a, string b};   // 仅在需要消歧时写类型标签

    print(p.x + q.x + r.x + s.x);
    return 0;
}
```

- 条目形式为 `[A] value`、`[A] name`、`[A] name = expr`；类型标签 `A` 可省略。无 `=` 的裸 `name` 既是值表达式也是匹配名；有 `=` 的条目按参数名匹配。条目用逗号分隔，可连续列出多个。
- 变量/字面量已有类型时直接写表达式（`{a, b}`、`{5, "ok"}`）；只有需要消歧时才写显式类型标签（`{integer a, string b}`、`{integer 5, string "ok"}`）。
- 匹配只考虑标了 `@Constructure` 的构造函数：条目数量必须等于参数数量；命名条目按参数名匹配；无名条目按位置和值类型匹配；显式类型标签也参与可赋值检查。无匹配或多个可行时报 `E2076`。
- 构造体由用户定义，负责实际转换。类型化/命名构造与普通位置 `Type{1, 2}` 构造、聚合对象字面量（`{x: 1}`）并存。
- 目标类是 `@Proxy` 时，handler 的 `@Construct` 钩子在构造前执行；见 §4.8.2。

#### 4.3.2 lambda

lambda 的**目标类型**沿用 Java（函数式接口/函数指针/方法引用），**捕获行为**与 C++ 一致：显式捕获列表按 C++ 规则；省略捕获列表时，被引用的外部变量按值复制，lambda 内修改的是副本。参数与返回类型显式书写。

```coo
integer n = 1;

// 省略捕获列表：n 被按值复制，修改的是副本；外部 n 不变
Function<integer, integer> copy =
    (const integer& x): integer -> { n = n + 1; return x + n; };

// 显式按引用捕获：修改原变量
Function<integer, integer> original =
    [&n](const integer& x): integer -> { n = n + 1; return x + n; };
```

完整写法：

```text
[ 捕获列表 ]? ( 参数列表 ): 返回类型 -> (表达式 | { 语句块 })
```

| 规则 | 语义 |
| --- | --- |
| 语法 | `[captures](const integer& x): integer -> { ... }`；捕获列表可省略；参数列表必须有括号；返回类型写在 `:` 后、`->` 前 |
| 省略捕获列表 | 被引用的外部局部变量按**值复制**；副本在 lambda 内可修改，原变量不受影响。`this` 按值复制（指针），通过它修改字段仍作用于原对象 |
| 显式捕获 | 与 C++ 相同：`[]`、`[=]`、`[&]`、`[x]`/`[&x]`、`[this]`、`[=, &x]`/`[&, x]` |
| 显式按值捕获 | `[=]`/`[x]` 捕获的副本在 lambda 内只读（赋值/`++`/`--` 报 `E2088`）；源码没有 `mutable` 关键字（v0） |
| 显式按引用捕获 | 修改原变量；引用捕获的 lambda 不得逃逸声明它的 Scope：返回、存字段/容器字面量、赋给外层作用域变量均报 `E2039` |
| 未列出变量 | 写捕获列表时，未列出的外层变量不可见，与 C++ 一致（`E2087`） |
| 捕获校验 | 未知名字、重复捕获、默认捕获位置/重复、混用 `[=, x]`/`[&, &x]` 报 `E2087` |
| 目标类型 | 函数式接口（恰好一个抽象方法）、函数指针 `(A,B) -> R`、方法引用，或等价的后续可调用类型 |
| 函数式接口目标 | `@FunctionalInterface` 且恰好一个抽象方法的接口降为等价函数类型：`Function<A,R>` 等价 `(A) -> R`，`Consumer<T>` 等价 `(T) -> void`，`Predicate<T>` 等价 `(T) -> boolean`，`Supplier<T>` 等价 `() -> T`，`BiFunction<A,B,R>` 等价 `(A,B) -> R`。lambda 与方法引用按抽象方法签名检查，`.apply`/`.accept`/`.test`/`.get` 调用该值。`lib/standard/utility.coo` 声明这些接口。持有类实现的具名接口值 ⏳：v0 中类不能 `implements` 函数式接口（`E2004`），类型实参数量错误报 `E2089` |
| 函数指针目标 | 必须无捕获：只能 `[]`，或省略捕获列表且不引用任何外部变量 |
| 类型检查 | 参数类型、返回类型必须与目标类型兼容；返回类型可协变；具体规则按泛型可赋值检查 |
| `this` | 省略捕获列表时按值复制 `this`；显式捕获时必须写 `[this]` 才能访问成员；捕获 `this` 不能转函数指针 |
| 遮蔽 | lambda 参数**可以**遮蔽外层局部变量/参数；编译器发 warning `W0004`（检测 ⏳）；遮蔽后外层变量在该 lambda 内不可见 |
| 返回 | 块体用 `return`；表达式体自动返回；必须与声明的返回类型兼容 |
| 泛型 | lambda 自身不声明类型参数；目标类型实例化决定参数/返回类型 |
| 方法引用 | `Type::staticMethod` 生成函数值；`object::method` 生成绑定接收者的函数值，接收者按引用借用并遵循与按引用闭包相同的逃逸规则（`E2039`）。未绑定 `Type::instanceMethod` ⏳。 |

lambda 的“值”只能通过目标类型使用：赋值、传参、返回、存容器前必须先确定目标类型。目标为函数式接口时生成闭包对象；目标为函数指针时生成无捕获的函数 thunk；方法引用同理。函数式接口由 `utility` 提供：`Function<T,R>`、`Consumer<T>`、`Supplier<T>`、`Predicate<T>`、`BiFunction<A,B,R>`。`@FunctionalInterface` 可选标注，编译器检查“恰好一个抽象方法”。lambda 编译为生成类/闭包对象，捕获值存在字段中；与普通接口实现一样参与单态化，不引入运行期元数据。实现状态：🚧。已实现：函数类型目标 `(A) -> R`、表达式/块体、隐式与显式捕获列表（`[]`、`[=]`、`[&]`、`[x]`、`[&x]`、`[this]`）、省略捕获列表的可变按值副本、捕获列表校验（`E2087`）、显式按值捕获只读（`E2088`）、按引用闭包逃逸检查（返回/字段/容器字面量/外层变量，`E2039`）、变量初始化/赋值、调用、lambda 实参与返回、静态（`Type::method`）与绑定（`object::method`）方法引用、函数类型字段调用、返回函数值的链式调用，以及 `@FunctionalInterface` 目标降为函数值（泛型实参替换、接口方法调用形式；`lib/standard/utility.coo` 的 `Function`/`Consumer`/`Predicate`/`Supplier`/`BiFunction`）。未实现：具名函数式接口值/类实现、未绑定实例方法引用、函数指针 thunk 转换、跨线程闭包逃逸、`this` 生命周期检查。

#### 4.3.3 控制流与模式匹配

```coo
integer main() {
    integer total = 0;
    for (integer i = 0; i < 5; i = i + 1) {
        total = total + i;
    }

    Array<integer> queue = [1, 2, 3];
    for value in queue {
        total = total + value;
    }

    switch (total) {
        case 15: { print("fifteen"); }
        default: { print("other"); }
    }
    return 0;
}
```

- `switch` 支持 integer/string/character/boolean/enumerate；每个分支独立作用域，无 fallthrough；`default` 必须最后；条件只求值一次，后端生成 if/else 链。
- `if` 表达式产生值：`if (condition) a else b`，分支为表达式并支持 `else if` 链；条件必须是 `boolean`，两个分支必须可赋给共同类型。块分支 ⏳；有副作用时使用 `if` 语句。
- `match` 支持枚举变体（`Color.Red`）、整数/字符串/字符/布尔常量、`_` 通配、绑定（`x`）、闭/开区间（`1..10`、`1..=10`）、或模式（`1 | 2 | 3`）与守卫（`x if x > 0`）。分支写作 `pattern => body`（块或表达式），同时兼容旧的 `case pattern:` 形式。
- 穷尽性检查：枚举必须覆盖全部变体（除非有通配/绑定）；`boolean` 必须覆盖 `true` 与 `false`（除非有通配）；其它标量类型必须写 `_`。缺失覆盖报 `E2077`，未知枚举变体报 `E2024`，非标量值上的绑定报 `E2077`。绑定是副本，在守卫与分支体内可见。类型模式与类解构模式 ⏳。
- `for` / `for ... in` / `break` / `continue` / `return` 已实现；`for ... in` 展开为 `iterator()` + `next()` 循环。
- `if let` 已移除；`Result`/`Expect` 用 `isOk()` + `unwrap()` 或 `match` 显式处理。

#### 4.3.4 Thread

`Thread` 是内建 move-only 类型，降为 `std::thread`：

```coo
Thread worker = Thread([](): void -> { print("worker"); });
print(worker.joinable());
worker.join();
print(hardwareConcurrency() > 0);
```

- 构造：`Thread(entry)`，`entry` 的类型必须是 `() -> void`；可以是 lambda、无捕获函数值或方法引用。
- 跨线程捕获：入口闭包必须**拥有**它使用的一切。允许 `[]`、`[=]`、`[x]` 以及 `@Share` 值的拷贝；拒绝 `[&]`、`[&x]`、`[this]` 与隐式 `this`，也拒绝调用期借用的函数值（函数类型参数或按引用闭包），统一报 `E2090`。
- 方法：`join()`、`detach()`、`joinable()`；`hardwareConcurrency()` 返回 `integer`（宿主硬件线程数）。
- 生命周期：`Thread` move-only，不能拷贝或赋值（`E2090`）。可 join 的线程必须在析构前 `join()` 或 `detach()`；否则生成的 `std::thread` 按 C++ 规则终止程序。v0 中 `Thread` 只支持局部变量，字段/参数/返回值暂不支持。
- 安全边界（v0）：编译器只检查捕获所有权，不检查共享内存。通过指针、`@Unique` 句柄或按引用捕获共享可变状态不保证无数据竞争；只承诺不可变拷贝与 `@Share` 引用计数的安全。同步原语以最小 `Mutex` 形式提供（见 §4.3.5）；atomic 与 scoped thread API ⏳。

#### 4.3.5 Mutex

`Mutex` 是内建不可拷贝类型，降为 `std::mutex`。v0 使用**手动** `lock()`/`unlock()`，没有 RAII guard：

```coo
class Counter {
public:
    Mutex mutex;
    integer value;

    void add(integer amount) {
        mutex.lock();
        value = value + amount;
        mutex.unlock();
    }
}
```

- 方法：`lock()`、`unlock()`、`tryLock()`（返回 `boolean`，降为 `std::mutex::try_lock()`）。
- `Mutex` 不能拷贝或赋值（`E2091`）；必须就地声明局部变量或字段。含 `Mutex` 字段的类不可拷贝，应通过 `@Share` 共享对象而不是按值复制。
- 边界（v0）：手动加锁；没有 `Lock` guard/RAII、没有 scoped lock 辅助，也**没有静态数据竞争分析**。忘记 `unlock()` 会阻塞其它线程；同一线程对同一 mutex 重复加锁是未定义行为。`Mutex` 支持局部变量与类字段；参数/返回位置 ⏳。
- 线程可按值捕获 `@Share` 值；mutex 保护被加锁方法访问的字段，只承诺这一手动纪律。

#### 4.3.6 ScopedLock、Atomic 与 `@Lock`

三者共享同一 v0 边界：无数据竞争静态分析、固定顺序一致性、无超时/try-lock 策略。

- **`ScopedLock`**：内建 RAII 类型，降为 `std::lock_guard<std::mutex>`，由具名 `Mutex` 左值构造：`ScopedLock guard = ScopedLock(mutex);`。进入声明时加锁，离开作用域（含 `return`/`break`/`continue`）自动解锁。不可拷贝/赋值（`E2092`），必须就地初始化，且仅支持局部变量（字段/参数/返回位置拒绝）。
- **`Atomic`**：内建 `std::atomic<std::int64_t>`，由 `integer` 初值构造：`Atomic count = Atomic(0);`。支持 `load()` 返回 `integer`、`store(integer)`、`fetchAdd(integer)`/`fetchSub(integer)`（返回操作前的值）、`exchange(integer)`（返回旧值）与 `compareExchange(expected, desired)` 返回 `boolean`（值等于 `expected` 并被替换为 `desired` 时为 true；`expected` 按值传入且不会被更新）。所有操作固定 **seq_cst**，没有内存序参数。`Atomic` 不可拷贝（`E2092`），可作局部变量或类字段；参数/返回位置 ⏳。没有泛型 `Atomic<T>`、没有浮点/指针原子、没有 `condition_variable`。
- **`@Lock(Exclusive, mutex, [captures]) { ... }`**：带捕获能力列表的块注解。第一个参数是字面量模式 `Exclusive`（其它模式 ⏳），第二个是具名 `Mutex` 左值（`Name`/`Member`），可选的第三个是捕获列表。捕获是**能力声明**而非引用：
  - `[x]` 复制值，只允许 primitive/string/enumerate；class/container/pointer 的值捕获报 `E2094`（浅拷贝会共享 `@Share`/指针字段，且暂无 `@Copy`）。对 `[x]` 的写入只改块内副本。
  - `[&x]` 可变借用：允许字段/下标写入与修改方法调用。
  - `[const&x]` 只读借用：写报 `E2094`；只能调用标了 `@ReadOnly` 的方法，未标注/修改方法在 `const&` 下报 `E2094`。只分析直接调用（无别名分析），经由其它表达式到达的方法不检查。
  - 未捕获的外层对象被写报 `E2094`；块内新声明的局部变量可正常写。对**未捕获** class 对象的方法调用，除 `@ReadOnly` 外保守拒绝。`[&x]` 调用的方法所产生的全局副作用在 v0 不做效果分析（只检查直接写/调用）。
  - 捕获受作用域限制：返回 `[&x]`/`[const&x]` 借用、存入字段/容器、被嵌套闭包捕获均报 `E2039`；`[x]` 值捕获是 owned copy，可以返回。嵌套 `@Lock` 在锁模式定义嵌套语义前直接报 `E2094`。
  - 旧 `@Lock(mutex)` 语法报 `E2094`，需迁移到 `@Lock(Exclusive, mutex, [captures])`。
  - 降为 `{ std::lock_guard<std::mutex> __lock_N(mutex); <值捕获副本>; ... }`，任意退出路径都会解锁。`@Lock` 只允许语句块（`@Target(block)`）。没有超时、try-lock、重入或嵌套锁检测：同一 mutex 嵌套加锁会死锁。写集检查刻意保守，不证明无数据竞争。

```coo
void add(integer amount) {
    @Lock(Exclusive, mutex, [&value]) {
        value = value + amount;
    }
}
```

#### 4.3.7 ScopedThread

`ScopedThread` 是内建 move-only 类型，降为 `std::jthread`；沿用 `Thread` 的跨线程捕获规则与诊断：

```coo
{
    ScopedThread worker = ScopedThread([counter](): void -> { counter.add(1); });
    print(worker.joinable());
}   // 析构自动 join
```

- 构造：`ScopedThread(entry)`，`entry : () -> void`；接受 lambda、无捕获函数值与方法引用。
- 捕获：只允许 `[]`、`[=]`、`[x]` 与 `@Share` 拷贝；`[&]`、`[&x]`、`[this]` 与隐式 `this` 报 `E2090`。
- 方法：`join()`、`detach()`、`joinable()`。析构时若仍可 join 会**自动 join**，因此离开声明块会等待线程结束。
- `ScopedThread` 不可拷贝/赋值（`E2090`），仅支持局部变量（字段/参数/返回位置暂不支持）。
- 边界（v0）：没有 `stop_token`/`requestStop`、没有取消、没有跨线程异常传播；线程入口逃逸的异常会终止程序。数据竞争注意事项与 `Thread` 相同。

#### 4.3.8 `@Copy` 与值拷贝

含 `@Unique`、`@Share` 或原始指针字段的类**默认不可复制**；复制需要类级 `@Copy(Deep)` 或 `@Copy(Shallow)`，否则在复制点报 `E2095`。纯值类为兼容保留隐式复制。

- `@Copy(Deep)`：只允许值字段与可深拷贝的 `@Unique` 链（pointee 自身是值可复制或 `@Copy(Deep)`）；拒绝 `@Share`、原始指针/引用、`@Copy(Shallow)` 字段、线程/mutex/atomic 字段与环。后端生成拷贝构造与拷贝赋值，对每个 `@Unique` pointee 执行 `std::make_unique<T>(*other.field)`。
- `@Copy(Shallow)`：逐成员拷贝；拒绝 `@Unique`，允许 `@Share`（增加引用计数）与原始指针（仅复制指针，解引用仍 unsafe）。
- 锁的 `[x]` 类值捕获要求 `@Copy(Deep)`；`@Copy(Shallow)` 与隐式类被拒绝，因为快照会共享状态。
- `@Copy` 只影响复制语义，不使类可跨线程，也不提供 unsafe 能力。

### 4.4 继承与访问

| 规则 | 语义 | 状态 |
| --- | --- | --- |
| 基类数量 | 单继承；接口可多个 | ✅ |
| 访问段 | `public:` / `private:` / `protected:`，默认 `private`（接口默认 `public`） | ✅ |
| 访问检查 | 字段/方法/构造越界报 `E2030`；`protected` 允许基类链内部访问 | ✅ |
| 字段同名 | 隐藏：父子各一份，按声明者 Scope 解析；`final` 字段不可隐藏（`E2032`） | ✅ |
| private 字段 | 参与布局；子类不可见，但可声明同名私有字段 | ✅ |
| 方法覆写 | 父方法 `virtual`、子类 `override`，缺一报 `E2033`/`E2035`；private 方法可覆写但不可直接调用 | ✅ |
| `final` | 类不可继承（`E2031`）；方法不可覆写（`E2032`） | ✅ |
| 重载集 | 跨继承的重载自动合并（按名字 + 参数个数解析） | ✅ |
| 返回类型 | 协变（返回子类型），否则 `E2034` | ✅ |
| 布局 | 基类子对象在前，随后字段按声明顺序；private 字段同样参与 | ✅ |
| 析构 | 含 virtual 方法（或实现接口）时自动 virtual | ✅ |
| 显式父访问 | `super.method()`、`super.field`、构造用 `: super(args)` | ✅ |
| 构造初始化列表 | `: field(expr)` 与初始化顺序检查 | ✅ |
| 基类引用的虚分派 | `Base&`/`Base*` 调用 virtual 的端到端测试 | ✅ |
| 默认构造/析构访问检查 | 默认构造 `E2030`；默认析构检查 | ✅ |

`this` 的静态类型是当前类；通过 `Base&`/`Base*` 调用 virtual 方法时按动态类型分派。接口实现检查：接口方法必须被实现，除非有默认实现；缺实现报 `E2010`，实现未标 `override` 报 `E2035`。

### 4.5 对象生命周期与 RAII

这是语言的硬语义，直接复用 C++ RAII。

- **构造顺序**（固定）：基类子对象 → 成员按**声明顺序**（有初始化器则用其值）→ 构造函数体。构造函数**不能失败**；资源获取失败请用返回 `Result` 的工厂函数（⏳）。
- **代码生成**：聚合初始化输出 `A a{...}`；`A a()` 输出 `A a = A()` 以避开 C++ most vexing parse；`A a = b` 交给 C++ 转换构造。
- **析构顺序**（固定）：用户析构函数体（若有）→ 成员按**声明逆序** → 基类子对象。每个类恰好有一个析构函数：用户声明，或编译器合成。
- **合成析构**：没有声明析构函数时，编译器在编译期合成，语义为“按成员声明逆序逐个 `destroy`，最后 `destroy(base)`”。后端输出 `~T() = default;` 并附逆序成员注释；C++ 保证同样顺序。

**用户析构与 `~this.member;`**：

```coo
class Holder {
    A a;
    B b;
    ~Holder() {
        ~this.b;
        ~this.a;
    }
}
```

- 成员类型是用户定义的 `class` 时，必须写 `~this.member;`；漏写产生 warning `W0001`（`-Werror` 升级为错误）。
- 基本数据类型（`integer`/`decimal`/`character`/`boolean`/`string`/指针等）不需要也不应显式析构；写了产生 `W0002`。
- 同一成员重复写析构声明时**只有最后一次生效**（保证恰好一次），重复给 `W0003`。
- `~this.member;` 是**析构声明**，不是第二次析构：C++ 后端把它擦除（只留注释），成员仍由 C++ 在析构体之后逆序析构，从而避免二次析构。
- 基类析构由编译器负责，不在用户析构体中写；`~this.member;` 只能出现在析构函数体内，且必须命名当前类的字段（否则 `E2008`）。

#### 4.5.1 所有权、Scope 归属与 `@Unique` / `@Share`

悬垂的本质是**引用的作用域超过了被引用对象的 owner Scope**。因此不用引用栈，而是用静态 Scope 包含 + 两个内建所有权注解：

- 每个对象有唯一 **owner Scope**；每条 `&` 有 **borrow Scope**；合法条件是 `borrowScope` 必须嵌套在 `ownerScope` 内。
- owner Scope 退出时按声明逆序析构其拥有的对象；不 detach、不延长、不计数。

1. **`@Unique` / `@Share` 是内建类型注解**（`@Target(type)`），写在局部变量、字段、参数、返回值的类型位置。静态类型仍是 `T`，注解决定存储/所有权/ABI；用户注解仍只读只增，这两个内建注解允许影响 lowering。
2. **`@Unique T`**：唯一所有权，堆存储，move-only。作用域/字段析构时释放；默认非空，可空写 `@Optional @Unique T`。成员访问自动解引用；`@Unique T y = x` 是移动（x 失效，`E2045`），不能复制。提供 `isPresent()` / `isEmpty()` / `get()`；`get()` 空时 panic；移动后 `isPresent()` 为 false。
3. **`@Share T`**：共享所有权，堆控制块，原子计数，copyable。复制 +1、销毁 -1，最后一个引用释放时析构；默认非空，可空写 `@Optional @Share T`；可跨线程。同样提供 `isPresent()` / `isEmpty()` / `get()`。
4. **普通 `T`**：值/栈语义；字段随对象析构；没有额外控制块。
5. **`&` 借用**：不拥有、不计数、不延长；只在当前函数/Block 内有效。`&` 不得返回、存入字段/容器、跨线程传递，也不得被可能逃逸的闭包捕获；违反报 `E2039`。`&` 字段直接编译错误。当前已实现 `return &x`、`T&` 字段、引用容器元素与按引用闭包逃逸（返回/字段/容器字面量/外层变量）检查，跨线程检查仍待补。
6. **借用来源**：`&` 可借用普通值、`@Unique`、`@Share`；借用本身不改变 owner；`@Unique` 移动后不能再借用（`E2045`）。
7. **参数/返回**：`@Unique` 参数按值转移所有权；`@Share` 参数复制计数；`&` 参数只借用且 callee 不得存储。返回 `@Unique`/`@Share` 转移/共享所有权；禁止返回 `&`。
8. **`@Unique` 转 `@Share`**：`@Share T r = box;` 把 `@Unique` 移入共享控制块，box 失效；反向不允许。
9. **`const T&` 临时量**：绑定临时量时，临时量生命周期延长到该引用变量的 Scope 结束；不改变 owner 规则。
10. **环**：只有 `@Share` 能成环；引用计数无法回收。类型图会对类级 `@Share` 环（含自递归类型）发 `W0005`，直接自赋值也发 `W0005`；运行期会记录 `@Share` 字段赋值形成的小对象图，退出时 DFS 检测环并发 `W0005`；若仍有存活对象但未检测到环，则报告可能泄漏。构造函数体和初始化列表会记录边；类内字段初始化和任意表达式别名产生的边仍可能缺失，因此运行期遍历仍是部分实现。`&` 不可存储，天然无环。
11. **静态/全局**：生命周期到程序结束；可被 `&` 借用，但借用仍不得逃逸。
12. **保守检查（v0）**：`&` 只在当前函数内使用，不返回、不存储、不跨线程；不做完整生命周期参数化。
13. **`@Weak T`**：非拥有、`std::weak_ptr<T>` 形式的 `@Share T` 观察者；默认空，可由 `@Share` 或另一个 `@Weak` 初始化/赋值。`isPresent()`/`isEmpty()` 判断是否过期；`lock()`/`upgrade()` 返回可空 `@Share T` 句柄，支持 `isPresent()/get()/orElse()`；过期后 `get()` 终止。

C++ 后端映射：

| Coordination | C++ 后端 |
| --- | --- |
| `@Unique T` | `std::unique_ptr<T>`（move-only、独占） |
| `@Share T` | `std::shared_ptr<T>`（copyable、原子计数） |
| `Weak<T>` | `std::weak_ptr<T>`（`isPresent()` = `!expired()`，`upgrade()` 返回可空 `@Share`） |
| `@Optional @Unique T` | 可为空的 `std::unique_ptr<T>` + Optional 代理/空检查 |
| `@Optional @Share T` | 可为空的 `std::shared_ptr<T>` + Optional 代理/空检查 |
| `T` | `T` 值 |
| `T*` | 原生指针（`unsafe`） |

JPA cascade 映射：

| JPA cascade | Coordination |
| --- | --- |
| `persist` / `merge` | 值复制/移动进 `@Share`，owner 变为共享控制块 |
| `detach` | `&` 借用随 Scope 结束（不影响目标生命周期） |
| `remove` / `orphanRemoval` | `@Unique` 随 owner Scope 析构；最后一个 `@Share` 释放析构共享对象 |
| cascade 到子对象 | 字段按声明逆序析构；`@Unique`/`@Share` 字段递归 release |

安全代码不会悬垂（`&` 不能逃逸、owner 覆盖 borrow、`@Share` 计数正确）；`T*`、FFI、指针运算仍属 unsafe，由用户负责。待决：`@Optional @Unique` / `@Optional @Share` 的组合细节、`@Unique` 移动规则、`@Share`/`Shared<T>` API 合并、环检测算法与析构重入诊断码。

#### 4.5.2 ProxyPolicy

`ProxyPolicy` 由**所有代理对象**共享；Optional 只是代理的一种。它用于代理值未经收窄或其它处理就被直接使用：解引用、成员访问、隐式转换为非代理值、构造或解构。策略名大小写不敏感。

| 策略 | 直接/缺失使用 | 诊断 |
| --- | --- | --- |
| `Error` | 未收窄/未处理时不允许；不生成回退路径 | 编译期 `E2048` |
| `Skip` | 以类型默认值/默认绑定继续；跳过实际读取或 handler | 无 |
| `Abort` | 运行期终止并打印源码位置 | 运行期 panic |
| `Default(expr)` | 用 `expr` 作为回退值；必须可赋给目标类型/pointee | 类型不符 `E2005` |

- `@Optional` 是 `@Target(type)` 注解，应用在类型 `T*`、`@Unique T`、`@Share T` 上（如 `@Optional integer*`、`@Optional @Unique Box`）。v0 中普通 `@Optional Class` 仅描述符场景，**不承诺运行期可空**；默认策略是 `Error`。
- **v0 覆盖范围**：已验证的 policy 执行覆盖 **局部变量** `T*`/`@Unique`/`@Share`（直接解引用、成员访问、隐式转非空、局部 Optional 解构）、**参数与字段**、收窄、`@Proxy(handler, policy)` 的 `Skip`/`Abort`/`Error` 解构路径，以及静态/runtime `@Construct`/`@Destructure` 钩子。字段/参数/返回值上的 `@Optional` 已解析和校验，但完整 policy 执行仍不完整 🚧；`@Proxy` 的 `Default(expr)` 构造/解构、runtime handler 构造前钩子、任意非默认构造类的 `Skip` 默认值也仍属 🚧。
- `@Proxy(handler, policy)` 为代理对象配置同一套四策略；不写 policy 时默认 `Skip`。`@Proxy` 策略同样作用于构造与解构路径（§4.8.1、§4.8.2）。
- **直接访问**（Optional）指：`*p`、`p.field`、`p.method()`，以及把 optional 值赋给非 optional 变量/参数（隐式转非空）。
- **收窄**：`if (p != null)`、`if (p.isPresent())` 的对应分支内视为已收窄；`if (p == null)` / `if (p.isEmpty())` 的 **else 分支**同理。收窄后访问不受 `Error` 策略限制。
- `Skip` 的默认值按目标类型构造：数值 0、`boolean` false、`string` 空串、`char` U+0000、用户 class 默认构造；默认构造不可用时是编译错误。解构时 `Skip` 绑定默认值且不读目标字段；构造时跳过缺失的 hook。
- `Default(expr)` 的 `expr` 只在缺失/空值路径求值；present 路径不读取 `expr`。
- `.get()` 是 Optional 的显式 panic 出口，不受策略影响：空值时终止并打印 `optional value is null` 与源码位置。
- `.orElse(x)` 始终安全：空值返回 `x`，非空返回目标值；`x` 必须可赋给目标/pointee 类型。
- 非 optional 到 optional 是安全加宽。`Error` 下 optional 到非 optional 必须收窄、`orElse` 或显式 `get()`；`Skip`/`Abort`/`Default(expr)` 下按策略隐式物化为非 optional 值（`Skip` 用默认值、`Abort` 运行期检查、`Default` 用回退值）。部分隐式转换边界仍属 🚧（`coord_e2e_optional_implicit` 覆盖基本 Skip/Default 情形）。
- `@Optional` 不解决悬垂：`@Optional T*` 的 lifetime 仍属 unsafe；它只表达空值状态。

#### 4.5.3 Optional 代理、`@Destructure` 与解构

`@Optional` 在 lowering 时生成内部**代理层**：成员访问、调用、构造与解构先经过它，再到达目标对象。Optional 代理不是用户可见的新类型，不改变目标的布局与 ABI。

- v0 主要覆盖**局部变量**。字段/参数/返回值的类型携带 `@Optional` 时已解析和校验，但完整 policy/代理执行仍不完整 🚧；返回 Optional 的表达式在已实现的路径上按代理处理。若目标本身带 `@Proxy`，**Optional 在外层，用户 handler 在内层**；缺失路径不调用用户 handler，只有 present 路径才展开 `@Call`/`@Get`/`@Set`/`@Construct`/`@Destructure`。
- `ProxyPolicy`（§4.5.2）在代理层先行生效，**先于**用户 `@Destructure` 方法或 trap。
- 解构（`{T a, U b} = value;`）支持 Optional 代理，空值时按策略处理：
  - `Error`：未收窄则编译期 `E2048`；
  - `Skip`：绑定默认值，不读取目标字段，不调用用户 trap；
  - `Abort`：运行期终止并打印源码位置；
  - `Default(expr)`：对 `expr` 做解构。
- 目标类定义了 `@Destructure` 方法时，解构先声明默认构造的绑定，再把绑定的引用传给该方法，由用户方法写入输出；否则逐字段读取（存在代理 trap 时先经过 `@Destructure` trap）。见 §4.8.1。
- 非空时 Optional 代理透传到目标，字段读取/方法调用按正常规则进行；收窄只影响静态检查，不引入运行期分支。

### 4.6 错误模型

`@Result<T>` 的 C++ `Result<T>` 包装、success 包装、语句级自动冒泡、`error("...")`、`@Result main` 与错误传播已实现；`@Expect` 有独立 `Expect<T>` 类型身份、`isOk/isErr/unwrap/expect`、赋值/打印/丢弃检查，以及 `isOk()` + `unwrap()` 显式消费；`if let` 已移除。`match` 作为后续控制流增强（⏳），不再阻塞 P3。

- 每个函数/方法声明的返回类型 `T` 是**成功类型**；内部有效返回类型是 `Result<T, Error>`，该包装对开发者隐藏。调用返回 `Result` 的函数时，若调用者不处理错误，错误**沿调用栈自动冒泡**；调用者的有效返回类型也带上 `Error`。
- 没有 `?`、`try`、`throw`、`catch` 关键字，也没有运行期异常；错误冒泡是提前返回，Scope 退出时局部对象逆序析构。
- `@Result<T>` 与 `@Expect<T>` 标注在函数/方法声明上，**必须携带成功类型 `T`**，且 `T` 必须等于声明的返回类型（否则 `E2073`）；注解不改变声明的返回类型。

| 调用点 | 看到 | 自动冒泡 | 消费要求 |
| --- | --- | --- | --- |
| 无注解的 fallible 函数 | 普通 `T`（隐藏 `Result`） | 是 | 否 |
| `@Result<T>` 函数 | `Result<T, Error>` | 否，调用者显式处理 | 否，可传递/丢弃 |
| `@Expect<T>` 函数 | `Expect<T, Error>` | 否 | 必须在本 Scope 内消费 |

- `@Result<T>` 关闭该调用点的自动冒泡：调用者必须 `match`/`isOk()` + `unwrap()`/`return` 或显式转成 `Result`。
- `@Expect<T>` 产生独立 must-use 类型 `Expect<T, Error>`（可转 `Result`）；不能存字段/容器、不能跨函数延迟消费，离开 Scope 未消费是编译错误 `E2055`。
- 消费方式：`match`、`isOk()` + `unwrap()`、`expect`、`return` 均可。
- `Error` 是全局接口：`code`、`message`、`source`、`backtrace`；错误可包装下层错误；默认错误类型即 `Error`。
- 入口：`integer main()`、`integer main(Array<string>)` 或 `integer main(integer argc, Array<string> args)`；不支持 `void main` 或其他参数形式（`E2052`）。未处理错误由运行时打印并以非零码退出。

### 4.7 注解、元注解与 Retention

注解应用的目标集合：`class`、`interface`、`annotation`、`function`、`method`、`field`、`variable`、`parameter`、`module`、`enumerate`、`enum_value`、`type`、`constructor`、`block`。应用顺序按源码出现顺序；同一目标的处理器按注解应用顺序执行。

- `type` 目标是类型表达式：`@Optional`、`@Unique`、`@Share`、`@Signed`、`@Unsigned`、`@Length`、`@Radix`、`@Weak` 是类型注解，附着在局部变量/字段/参数/返回值位置的类型上，而不是上述声明目标。
- `constructor` 目标是构造函数声明：`@Constructure` 标记目标类中参与类型化/命名字面量构造的构造函数。
- `block` 目标是语句级 `{ ... }` 代码块，用于 `@Retention(Compile)` / `@Retention(Runtime)` 的执行控制。
- 其它名字保持声明语义（`@Result`/`@Expect` 用于函数/方法，`@Destructure` 用于方法，`@Retention` 用于注解/枚举等）。`@Target` 校验接受 `type`、`constructor` 与 `block`。

**只读、只增**：

- 应用不会覆盖、替换或修改先前注解；多个注解共存。
- 处理器不能读取、修改、覆盖目标上的其他注解；它只能看到 `description` 快照并产出新声明。
- 处理器不能修改被注解目标的结构（字段、方法、布局、可见性）；要生成 `toJson` 之类的代码，只能生成独立的函数/类。
- 生成物进入正常 Sema 检查；名字由编译器分配（可预测、唯一），引用通过 description 身份而非字符串拼接解析。生成声明可被后续处理器再次看到（按轮次直到不动点，带轮次上限）。

**唯一指令原语**：

```coo
line(file: string, line: integer);
```

- 只在注解处理器（编译期上下文）中可用；在普通函数/方法体里调用是编译错误。
- 语义绑定到**被注解的声明/块**：处理器为该目标设置生成的 C++ `#line` 映射，离开后恢复上一层映射。
- 同一目标的处理器多次调用 `line` 时，最后一次生效；不同目标互不覆盖。
- 只允许这一个原语；不提供 `#warning`、`#error`、`#pragma`，不能定义新指令名，更不能输出任意 `#...` 文本。
- 坐标诊断仍使用原始 `.coo` 的 Span；`#line` 只影响生成 C++ 的调试/诊断位置。

**元注解**（只有内建，用户不能定义新的元注解或改写其语义）：

| 元注解 | 作用 | 违反时 |
| --- | --- | --- |
| `@Inherit` | 注解沿 class → 子类、interface → 实现类、virtual method → override 传播 | 应用于非注解声明：错误 |
| `@Repeatable` | 允许同一目标重复应用同一注解；查询返回列表 | 未开启却重复：错误 |
| `@Target(...)` | 限制可应用目标（上列集合） | 目标不匹配：错误 |
| `@Meta` | 标记“这是元注解”；只能用于内建元注解 | 用户应用：错误 |
| `@Retention(stage)` | 保留阶段：注解上用裸写/`runtime`/`invocation`；枚举上用 `enum` | 目标/阶段不匹配：错误 |
| `@Documented` | 标记注解进入文档/IDE hint（不影响编译语义） | 应用于非注解声明：错误 |

#### 4.7.1 元注解与处理器架构

- **description** 是语言级、用户可见的只读快照（名字、kind、注解值、成员）；**descriptor** 是编译器内部语义对象（targets/repeatable/inherit/retention/processor/intrinsic），定稿后只读，不序列化。
- 元注解在 C++ 启动时 bootstrap 为 `AnnotationDescriptor`；MetaProcessor 只修改 `DescriptorBuilder`，不修改 description。
- `AnnotationKind`：`Meta`（6 个）、`Processor`（可通过 `@Processor` 方法生成声明）、`Intrinsic`（`@Result`/`@Unsigned`/`@Native` 等 lowering hook）。
- 普通处理器在 annotation 声明内用 `@Processor` 方法挂载：

```coo
annotation Serializable {
    @Processor
    void generate(Description target) {
        addFunction("serialize" + target.name);
    }
}
```

- 固定五阶段：Meta 定稿 → 应用校验 → `@Inherit` 展开 → 普通处理器按应用顺序执行 → Retention 物化。普通处理器只拿 description，不能改目标结构；`@Processor` 与 description 内部 API 属 P4（解析与基础执行已实现）。
- 处理器在 Sema 前执行，绑定 description，支持局部变量/if/description 查询/`line`，通过 `addFunction`/`addFunctionReturning`/`addConstant`/`addAnnotatedFunction` 生成声明；生成声明参与下一轮，轮次上限默认 8，超限报 `E2068`。
- **Intrinsic 的可见性**：用户可以在自己的声明上**应用**标准库 Intrinsic 注解（`@Result`、`@Expect`、`@Unsigned`、`@Signed`、`@Length`、`@Native`、`@Unique`、`@Share`、`@Optional`、`@Proxy`、`@Invocation` 等），并接受其目标/重复性/Retention 校验；但用户**不能定义**新的 Intrinsic，也不能让自己的注解获得 lowering/ABI hook。Intrinsic 的名字到 C++ hook 的映射是封闭表。

`@Inherit` 的解析规则（与“注解不能覆盖”一致）：先收集目标上**直接声明**的注解（按源码顺序）；若注解类型带 `@Inherit`，再沿继承关系收集（class 走基类链 → 接口列表；interface 走父接口；method 走被覆写的 virtual 方法链）；**直接注解优先**，同类型直接注解已存在时不再加入继承的那一份；不同类型全部共存，非 `@Repeatable` 类型只保留最近的一份；`effective_annotations()` 返回“直接 + 继承”的只读视图并标注每项的 `is_inherited`，源码/`description` 上的直接注解列表不被修改。

`@Retention` 是**保留阶段**元注解：

| 写法 | 可标注 | 阶段 | 含义 |
| --- | --- | --- | --- |
| 不写 | — | Compile | 纯编译期，零运行期足迹 |
| `@Retention` / `@Retention(runtime)` | `annotation` | Runtime | 旁路只读元数据表 + 按需生成逻辑 |
| `@Retention(invocation)` | `annotation` | Invocation | Runtime 的全部内容 + 逐次调用的值拦截 |
| `@Retention(enum)` | `enumerate` | Enum | Runtime + 枚举变体 `(name, value, ordinal)` 表 |
| `@Retention(Compile)` | `block` | Compile | 代码块由解释器在编译期执行；C++ 后端省略该块 |
| `@Retention(Runtime)` | `block` | Runtime | 代码块不在编译期执行，正常生成并在运行时执行 |

**代码块保留**：语句层面的 `{ ... }` 可写成 `@Retention(Compile) { ... }` 或 `@Retention(Runtime) { ... }`。

- `@Retention(Compile)` 块在编译期求值中执行；其 `print` 输出进入编译期输出（可用 `coorc --print-consts` 查看），不进入生成程序；C++ 后端省略该块。
- `@Retention(Runtime)` 块不在编译期执行，正常生成并在运行时执行。
- 编译期块不能依赖运行期局部状态；只建议使用常量与纯函数。`@Retention(Invocation)` 或 `@Retention(enum)` 用在代码块上报 `E2044`。
- `@Target` 合法名新增 `block`。
- 阶段递进：Compile → Runtime → Invocation；Invocation 隐含 Runtime，阶段不能回退。Enum 是 Runtime 的枚举专用分支，只适用于 `enumerate`。
- 只有携带 `@Retention` 的声明、注解/枚举或代码块才获得对应处理；未标注仍是纯编译期、零运行期足迹。Runtime/Enum/Invocation 物化产物是**旁路只读数据 + 生成逻辑**，不改变对象布局、字段顺序或 ABI；Compile 块在编译期执行并由生成代码省略。
- 保留注解只能标注具有稳定身份的声明：class/interface/annotation/function/method/field/module/enum；局部变量/参数上的 `@Retention` 报错（语句级代码块是显式例外）。`@Inherit` 的 effective annotations 在编译期算好后一并物化。
- 运行期只提供“按声明 id 查询已保留注解/枚举变体”的窄接口，不提供任意成员枚举或按字符串调用的通用运行期反射。

### 4.8 proxy

`@Proxy(Handler)` 或 `@Proxy(Handler, Policy)` 把 Handler 的 trap 绑定到目标。Handler 是注解或类，且必须实现标准库标记接口 `ProxyHandler` 或其子接口（缺失报 `E2074`）；未实现的 trap 使用默认行为（直接转发）。

| trap | Coordination 注解 | 触发点 | 默认行为 |
| --- | --- | --- | --- |
| `get` | `@Get` | 读字段/属性 | 直接读 |
| `set` | `@Set` | 写字段/属性 | 直接写 |
| `has` | `@Has` | 成员存在性查询 | 静态成员检查 |
| `deleteProperty` | `@Delete` | 删除/失效 | 编译错误或 no-op |
| `ownKeys` | `@Keys` | 遍历成员 | 返回声明的成员 |
| `getOwnPropertyDescriptor` | `@Describe` | 取成员描述 | 返回 `description` |
| `apply` | `@Call` | 调用函数/方法 | 直接调用 |
| `construct` | `@Construct` | 构造对象前 | 直接构造 |
| `destructure` | `@Destructure` | 解构代理对象前 | 按字段普通读取 |

`@Constructure` 不是 trap：它是 `@Target(constructor)` 注解，用于开启类型化/命名字面量构造（§4.8.2）。

```coo
@Target(method)
annotation LogHandler implements ProxyHandler {
    @Get
    void get(string name) { print(name); }
}

@Proxy(LogHandler)
class Service { ... }
```

- 静态语言中属性名在编译期已知，因此 trap 参数是 `description` 或可省略的 `string` 名字，而不是运行期字符串查询。
- 代理不改变目标的类型、成员集合、布局与 ABI；`@Proxy` 是“调用点重写”，不是新类型，也不是结构修改。
- 展开目标：`class`（构造包 `@Construct`、成员访问包 `@Get`/`@Set`、方法调用包 `@Call`）；`function`（每个调用点包 `@Call`）；`method`（入口/出口包 trap，虚调用按动态类型展开）；`variable`（每次读写改写为 `get`/`set`）。
- 虚分派与接口调用同样适用；trap 在静态类型已知的调用点展开。代理解析失败（trap 签名不匹配、目标不可代理）是编译错误。代理可叠加，展开顺序按注解从内到外、显式 `@Proxy` 优先。
- 三种模式：handler 未标 `@Retention` → 编译期展开、零开销、无代理对象；标 `@Retention` → 运行期代理包装、trap 运行期分派；标 `@Retention(invocation)` → 运行期代理且 trap 可修改参数/返回值/字段值。
- `@Proxy(handler)` 默认使用 `ProxyPolicy.Skip`；`@Proxy(handler, Error|Skip|Abort|Default(expr))` 为缺失的构造/解构路径选择策略。四策略见 §4.5.2。
- `ProxyHandler` 是标记基接口；`MethodProxyHandler`、`DestructureProxyHandler`、`ConstructureProxyHandler`、`ClassProxyHandler`、`ObjectProxyHandler`、`FunctionProxyHandler` 继承它。支持 `interface X extends ProxyHandler`，实现任一子接口即满足 `@Proxy` 的 handler 要求。
- `@Construct` 与 `@Destructure` 是 **handler 钩子**：在默认构造/解构行为之前执行，默认行为仍然发生；钩子可按签名校验、记录或修改值。
- 当前进度：`@Proxy(Handler[, Policy])` 校验、class handler 静态 `@Call/@Get/@Set`、annotation/运行期 retention、`@Construct`（`@Constructure` 构造前）、逐绑定 `@Destructure`、策略默认 `Skip` 均已实现基础版。`@Has/@Delete/@Keys/@Describe` 仍只有声明；runtime handler 构造钩子与部分隐式 optional→非 optional 边界为 🚧。

#### 4.8.1 `@Destructure` 与解构

`@Destructure` 有两种形式：

1. **ProxyHandler trap**：用 `@Target(method)` 标注 handler 方法。解构代理对象时按绑定字段调用 handler，静态或 runtime 规则与 `@Get`/`@Set` 相同。
2. **目标类方法**：`@Destructure void unpack(T& outA, U& outB)` 写在被解构的类上；方法接收与绑定数量相同的引用参数，由用户写入输出。

```coo
class Point {
public:
    integer x;
    string y;

    Point(integer x, string y) {
        this.x = x;
        this.y = y;
    }

    @Destructure
    void unpack(integer& outX, string& outY) {
        outX = x + 1;
        outY = y + "!";
    }
}

integer main() {
    Point point(1, "a");
    {integer a, string b} = point;   // a == 2, b == "a!"
    return 0;
}
```

- 解构 `{T a, U b} = obj;` 时，若目标类存在匹配的 `@Destructure` 方法，编译器先声明默认构造的绑定；对象是代理时先对每个绑定调用 handler `@Destructure` 钩子；随后把绑定引用传给目标方法，由用户方法写入。否则逐字段读取（存在代理 trap 时先经过 trap）。
- handler trap 方法参数为字段名（`string`），可省略参数；runtime handler 可额外接收目标对象/上下文（按 `@Get` 的 runtime 规则）。
- 目标方法的参数数量必须等于绑定数量，参数类型必须接受绑定类型；使用非 static 方法。缺失/不兼容的方法回退到逐字段读取。
- `ProxyPolicy` **先于**用户 `@Destructure` 生效：缺失的 Optional 路径不调用 handler（`Error` 报 `E2048`、`Skip` 绑定默认值、`Abort` 终止、`Default` 解构回退值）。
- 解构只读取被绑定的字段，不读取未绑定字段；字段缺失/不可访问按普通解构错误处理。
- `@Destructure` 不改变对象的布局/ABI，也不引入运行期通用反射。

#### 4.8.2 `@Constructure`

`@Constructure` 是 `@Target(constructor)` 注解，只能标在目标类的构造函数上。它开启类型化/命名字面量构造；构造函数体仍由用户定义并负责实际转换。

```coo
class Point {
public:
    integer x;
    string y;

    @Constructure
    Point(integer x, string y) {
        this.x = x;
        this.y = y;
    }
}

integer main() {
    integer a = 3;
    string b = "hi";

    Point p = {a, b};                  // 已声明变量
    Point q = {5, "ok"};               // 位置字面量
    Point r = {y = "named", x = 9};    // 命名参数
    Point s = {integer a, string b};   // 显式类型标签消歧
    return 0;
}
```

- 条目语法为 `[A] value`、`[A] name` 或 `[A] name = expr`；类型标签 `A` 可省略。无 `=` 的裸 `name` 既是值表达式也是匹配名；有 `=` 的条目按构造函数参数名匹配。条目用逗号分隔。
- 类型标签只用于消歧。已有类型的变量和字面量直接写（`{a, b}`、`{5, "ok"}`）；显式标签（`{integer a, string b}`）用于在多个可行构造间选定。
- 匹配只考虑标了 `@Constructure` 的构造函数：条目数量必须等于参数数量；命名条目按参数名匹配；无名条目按位置和值类型匹配；显式类型标签参与可赋值检查。无匹配或多个可行时报 `E2076`。
- 类型化/命名构造与普通位置 `Type{1, 2}` 构造、聚合对象字面量（`{x: 1}`）并存。
- 目标类是 `@Proxy` 时，handler 的 `@Construct` 钩子在构造前执行，然后调用用户构造函数（§4.8）。
- `@Constructure` 只是声明语法，不引入运行期反射，也不改变布局/ABI。

#### 4.8.3 `@Invocation`

`@Invocation` 标注**函数、方法或其它可调用对象**，把每次调用包装成 before/after hook：

```coo
class Audit implements InvocationHandler {
public:
    void before() { print("before"); }
    void after() { print("after"); }
}

@Invocation(Audit)
integer work() {
    print("work");
    return 1;
}
```

- handler 必须实现标准库标记接口 `InvocationHandler`（缺失报 `E2075`）。
- handler 签名：`before(...)` 和/或 `after(...)`；参数以可写引用暴露，返回值通过 result 槽暴露。
- 默认编译期展开、零运行时对象；handler 或目标带 `@Retention(invocation)` 时生成运行期 hook，允许修改参数/返回值。
- `@Invocation` 不改变错误模型；可与 `@Result`/`@Expect` 组合，错误仍由 Result/Expect 通道处理。
- 多个 hook 按注解应用顺序执行；before 正序、after 逆序；hook 不得增删目标成员或改变签名。
- handler 签名不匹配、目标不可调用是编译错误；基类引用调用 override 时 hook 随虚分派执行。

### 4.9 模块与包

- 一个 `.coo` 文件就是一个模块；路径即模块名。缺省 `module` 声明时模块名由文件路径决定。默认私有；`export` 导出声明（类型、函数、常量）。
- `.coox` 是编译后的模块接口/元数据（公开接口 + 反射元数据），供 `import` 做增量编译；私有声明不导出。模块初始化按依赖拓扑排序；禁止循环依赖。
- `coordination.toml`/`coord.pkg` 描述包、目标与依赖，`coord.lock` 固定解析结果；完整协议见 §8。

```coo
module app.main;                        // 可选
import {User, Order} from app.model;    // 选择性导入
import app.model as m;                  // 别名导入
export { User };                        // 导出列表
export class File { ... }
```

- 支持 `import {a,b} from xx;`、`import xx as alias;` 与 `export {a,b};`；旧的 `import xx {a};` 已移除。单文件模式支持相对路径 import（按导入文件目录解析、每个文件只加载一次、缺失 `E2070`）。
- `coorc build <dir>` 进入工程/包模式：`coord.pkg` 的 `name/source/dependency` 递归收集依赖包源码，循环依赖报错；无清单时递归收集 `.coo`。`coorc deps <package-dir>` 输出依赖优先顺序。`.coox` 可读写；`--incremental` 基于 `.coo`/根 `coord.pkg`/`.coox` stamp 跳过未变更构建。
- 限制：别名局部变量声明 `l.Payload p = ...` 仍有语法歧义，当前完整覆盖参数/返回类型位置。

### 4.10 内存、并发与 FFI

- 值语义 + 引用 + RAII；无 GC。`@Unique` 唯一所有权、`@Share` 原子引用计数、`new`/`delete` 仅在 `unsafe` 块中（⏳）。
- 指针解引用/运算需 `unsafe`；引用不可为空（检查 ⏳）。标准库提供线程、通道、原子类型；语言不内建 async/await（⏳）。
- `extern "C++"` 块声明头文件、类型与符号，编译器生成 C++ 边界包装；C ABI 是更窄子集（⏳ P5）。
- 默认运行期零反射：没有 RTTI、没有注解表、没有 proxy 对象；显式 `@Retention` 只物化被保留的注解表与生成逻辑。

## 5 反射五层与 Class 模型

### 5.1 模型

```text
源码声明
   │  编译期快照
   ▼
description ──(descriptor 处理器)──► 新声明（追加到模块）
   │                    │
   │ 擦除式查询          │ 类型化查询
   ▼                    ▼
reflection          Reflector<T>
```

| 层 | 定义 | 何时存在 | 典型操作 |
| --- | --- | --- | --- |
| `description` | class/interface/enumerate/annotation/function/method/field/variable 的属性描述符：kind、name、type、members、annotations、visibility、span、flags | 编译期 | 读取字段、遍历成员、比较、作为处理器输入 |
| `descriptor` | 处理属性描述符的处理器：接收一个或多个 `description`，产出新声明 | 编译期 | `process(target)`、`generate()`、注解处理器 |
| `reflection` | 擦除式反射游标：动态 kind/name/member 查询 | 编译期 | `kind()`、`name()`、`members()`、`get("x")` |
| `Reflector<T>` | 类型化反射视图：对已知类型 `T` 的静态检查视图 | 编译期 | `Reflector<File>.fields`、`.methods`、`.annotations` |
| `proxy` | 行为代理：trap 集合 | 默认编译期；保留模式运行期 | `get`、`set`、`has`、`call`、`construct`、`destructure` |

### 5.2 `description` 与 `descriptor`

```coo
description d = description_of(User);
d.kind; d.name; d.qualifiedName; d.type;
d.members; d.annotations; d.visibility; d.flags; d.span;

annotation Json {
    descriptor generate(Description target) { return declaration { ... }; }
}
```

- `description` 是不可变值，可比较、可哈希、可序列化进 `.coox`；`description_of(x)` 接受类型、声明或值，对运行期值只能在编译期上下文中调用。
- 处理器是**编译期函数/方法**，输入 `description`，输出新声明，只能**追加**到当前模块末尾，不能替换/删除/改写已有声明，也不能修改被注解目标的结构。
- 处理器运行时机：目标 Sema 通过后、全模块 Lower 之前；同一目标的处理器按注解应用顺序执行；多轮直到不动点，带轮次上限。
- 处理器不得读取 IO/时间/随机；需要外部输入时通过显式 capability 注解声明，并把输入纳入缓存 key；有 fuel/内存上限。

### 5.3 `reflection` 与 `Reflector<T>`

```coo
reflection r = reflect(value);                 // 擦除式，编译期
Reflector<User> ru = Reflector<User>();        // 类型化，编译期
for (description field : ru.fields) { print(field.name); }
```

- 通用算法（序列化、ORM、测试发现、依赖注入）用 `reflection`；业务代码与需要 IDE 补全的场景用 `Reflector<T>`。
- `reflection` 的成员访问在编译期动态检查（不存在即编译错误）；`Reflector<T>` 对成员做静态检查，名字拼错是编译错误。
- `Reflector<T>` 的查询折叠为 C++ 常量，无运行期 RTTI；越界索引报 `E2069`；数据来自 descriptor，与 outline/hint/运行期表同源。

### 5.4 保留阶段物化

**Runtime**：编译器为保留注解生成旁路只读表（`declaration id -> annotation record`，位于只读数据段）与窄访问器；`@Inherit` 在编译期展开为 effective 列表后写入，运行期不再走继承链；对象布局、字段顺序、vtable 不变；跨模块时 `.coox` 只携带公开声明的保留元数据。

**Enum**：`@Retention(enum)` 生成 `enum type id -> [(name, value, ordinal)]` 旁路表与按名字/按值/列变体的窄访问器；枚举值仍是底层整数，布局/ABI 不变。

**Invocation**：在 Runtime 之上生成逐次调用的值 hook。

| hook | 触发点 | 可修改的值 |
| --- | --- | --- |
| 调用前 | function/method 进入 | 参数值 |
| 调用后 | function/method 返回 | 返回值 |
| 字段读 | `get` | 读出的值 |
| 字段写 | `set` | 即将写入的值 |
| 构造后 | 构造函数返回后 | 已构造对象的字段值 |
| proxy trap | `@Proxy` 的调用点 | 参数/返回值/字段值 |

- **只能改值**：新值必须可赋值给原类型（允许子类型）；存储的静态类型不变。**不能改类型与结构**：签名、泛型实参、成员集合、字段顺序、vtable、ABI 全部不变。
- handler 允许保存运行期状态（计数、缓存等），状态放在旁路存储，不进入目标对象布局；hook 不引入可恢复错误，失败只能 panic。
- hook 链按注解应用顺序执行，后者看到前者修改后的值；`@Repeatable` 的多个 handler 依次执行，`@Inherit` 时直接注解的 handler 先于继承 handler；未标 `@Retention(invocation)` 的注解不产生任何 hook。

### 5.5 代码生成规则与确定性

- 生成入口只有注解处理器；**没有 `emit` 关键字**。生成物只能是新声明，不能修改已有声明、目标结构，不能注入 token。
- 多个注解共存时互不覆盖：每个处理器都基于同一份 `description` 快照工作，产出各自的声明；不存在“后注解替换先注解”的语义。
- 生成的名字必须可预测（便于调试），但引用解析使用 description 身份，避免文本拼接；生成的声明参与 Sema、重载解析、可见性检查。
- 处理器执行禁止 IO/时间/随机；capability 注解是唯一例外且必须进入缓存 key。编译期结果按内容寻址缓存（⏳），确保增量构建可重复。
- 反射值不能逃逸到运行期；`.coox` 只序列化描述数据，不序列化编译期句柄。

### 5.6 Class / Member / Field / Function / Method / Constructor

反射基础类型：`Class`、`Member`、`Field`、`Function`、`Method`、`Constructor`、`Modifier`、`Description`、`Descriptor`。

```text
Member
  ├─ Field
  └─ Function
       ├─ Method
       └─ Constructor

Description                 // 只读声明视图
  └─ Descriptor             // 运行期布局 + 查询表

Class                       // 类型句柄，指向 Descriptor
Modifier                    // 位标志枚举
```

- `Member` 统一 `name/owner/modifiers`；`Field` 有 `type/index`；`Function` 统一 `returnType/parameterTypes/generics/invoke`；`Method` 增加 `owner`/`this` 语义；`Constructor` 是特殊 `Function`，无返回类型。
- `Description` 是基类/只读视图；`Descriptor` 继承并补充布局；`Class` 只持有 `const Descriptor*`。
- `Modifier` 是位标志枚举：`None`、`Public`、`Private`、`Protected`、`Static`、`Final`、`Virtual`、`Override`、`Abstract`、`Const`、`Optional`、`Unique`、`Share`；只保留 `modifiers.has(Modifier.X)`。

```coo
class Class {
public:
    string name;
    string qualifiedName;
    integer id;

    Class getSuperClass();
    Array<Class> getInterfaces();
    Description getDescription();
    Descriptor getDescriptor();

    boolean isPrimitive();
    boolean isEnum();
    boolean isRecord();
    boolean isInterface();
    boolean isProxy();
    boolean isAssignableFrom(Class other);
}
```

- 基本类型 `getSuperClass()` 返回 Object 的 Class；`Object.getSuperClass()` 返回 `null`；`Class` 相等按 id；每个类型一个 Class 实例；规范名小写，`Integer` 等别名共享同一 id。
- `Descriptor` 提供 `fields`/`functions`/`constructors`、`size`/`alignment`、`getFunctions`/`getField`/`getFunction`/`getConstructor`/`hasAnnotation`；成员查询按名字 + 参数类型。
- `Value` 支持 null、基本类型、string、对象引用；`Field.get/set`、`Function.invoke` 统一返回 `Result<Value>`；对象引用用 `Result<T> cast<T>()` 且先校验 Class。

### 5.7 运行期描述表与访问控制

```cpp
struct ClassDescriptor {
  const char* name;
  const char* qualifiedName;
  std::uint32_t id;
  const ClassDescriptor* superClass;
  const ClassDescriptor* const* interfaces;
  const FieldDescriptor* fields;
  const MethodDescriptor* methods;
  const ConstructorDescriptor* constructors;
  const AnnotationEntry* annotations;
  std::size_t size;
  std::size_t alignment;
  std::uint32_t modifiers;
};
```

- 每个类型生成一个 `inline constexpr` 表；`Class` 只存 `const ClassDescriptor*`；`Member` 句柄存表指针 + index；id 在包内确定性分配（按“包名 + 限定名”排序）并写入 `.coox`；跨模块合并描述表，避免 ODR 冲突；描述表位于只读段。
- 默认只暴露 public；`@Reflect` 才能查询/调用 private/protected；私有成员默认不进入公开 Descriptor；`getDeclared*` 在 `@Reflect` 下开放。
- Proxy 的 `getClass()` 返回目标类，`Descriptor.isProxy()` 标识代理。
- 任意值可调用 `getClass()` 获取 `Class`；基本类型的 `getSuperClass()` 返回 `Object` 的 `Class`，`Object.getSuperClass()` 返回 `null`。运行期保留表提供窄查询（如 `hasRuntimeAnnotation`/`runtimeAnnotationValue` 与 `runtimeFieldCount`/`runtimeFieldName`/`runtimeMethodCount`/`runtimeBase`），不提供通用运行期反射。
- `Module`/`Reflection` 是自由函数与跨包发现的入口；`Class.of<T>()` 只接受具体类型，泛型需要显式提供实参；`Module` 含包名、版本、导出类型/函数，`TypeParameter` 保存未擦除形参与上界。

### 5.8 Documentation

`lib/standard/documentation.coo` 提供普通标准库类型 `Document`（summary、结构化 tags、原始文本）与 `DocumentTag`（name、subject、text）。文档挂载到 `Description`/`Member`/`FunctionMember`/`Module`；反射模型类改名 `FunctionMember`，以便 `Function` 作为函数式接口名；运行期 Descriptor 保留只读 Document 引用；编译器只负责提取 `///` / `/** */` 并构造 Document，解析/渲染 API 由标准库实现；现有 `@parameter/@return/@template` 校验保留。

### 5.9 已确认决策与待决

已确认：`Description` 保留名字并改造成普通标准库基类，processor 签名保持兼容；`Value` 对象引用允许跨包传递但必须携带 `Class` 和包 id；`Field.set` 拒绝写 `@Unique`/`@Share` 字段；`Method.invoke` 外层 `Result<Value>` 只表示反射失败，被调函数的错误放入 `Value`；`Class.getDescription()` 运行期可用；泛型 `Class` 带 `Array<Class>` 实参与 `TypeParameter`；注解保留类型化值；静态取 Class 用 `Class.of<T>()`；Descriptor 默认只含 public。

待决：`Description` 迁移的兼容层；`Value` 跨包生命周期；反射调用的错误通道；`Class.getDescription()` 的可用范围；`@Reflect` 的粒度；描述表跨模块合并策略。

## 6 编译生命周期与工程

### 6.1 流水线

| 阶段 | 输入 | 输出 | 关键不变量 |
| --- | --- | --- | --- |
| ① Load | 文件路径 | `SourceFile`（拥有缓冲） | `string_view` 永远指向稳定内存 |
| ② Lex | 源文本 | `Token[]` | 关键字、字面量、注释、Span 完整 |
| ③ Parse | Token 流 | `ModuleAst` | 语法错误恢复；每个节点带 Span |
| ③.5 @Processor | AST + description 快照 | 生成声明 | 只增不改；轮次上限 8；生成物参与 Sema |
| ④ Sema | AST | 类型/符号/诊断 | 类型可解析；Scope 栈平衡；RAII 检查 |
| ⑤ ConstEval | 已检查 AST | 常量表、编译期输出 | 纯函数、fuel 上限、确定性 |
| ⑥ Lower | AST + 常量表 | C++20 文本 | 只使用受限 C++ 子集 |
| ⑦ Host CC | `.cpp` | 目标文件 + 可执行文件 | `--cxx`/`$CXX`/`cl`/`c++` 可替换 |
| ⑧ Run | 可执行文件 | 退出码 | 仅 `coorc run` |

`check` 停在 ⑤（含 ConstEval）；`--emit-cpp` 停在 ⑥；`build` 到 ⑦；`run` 到 ⑧。选项：`-o`、`--emit-cpp`、`--cxx`、`--keep-cpp`、`--dump-tokens`、`--dump-ast`、`--print-consts`、`-Werror`、`--version`。`build` 的 C++ 中间文件默认临时删除，`--keep-cpp` 保留；宿主编译器默认 `cl`（非 Windows 为 `c++`），可用 `--cxx` 或 `CXX` 覆盖。

### 6.2 工程结构

```text
libs/coord/
  include/coord/   support / syntax / ast / parser / sema / interp / codegen
                   driver / standard_library
                   modules/  identifier / access / modifier / operator / scope / symbols
                             unicode / character / charset / stream / file / thread / atomic
                             annotation_registry / descriptor / outline / hint / interface / processor
                             module / package / project / dependency / compiler
                             abstract_syntax_tree
                             platform / semver / toml / checksum / archive
                             manifest / lockfile / net / registry / mirror / resolver
                             package_manager
  src/             对应实现（C++ 后端发射器在 generate.cpp）
tools/coorc/       命令行入口
tests/             单元测试（unit/main.cpp） + e2e.cmake + package_manager.cmake
examples/          .coo 示例
lib/standard/      标准库源
```

CMake 目标：`coorc_core`（全部编译器阶段的静态库）、`coorc`（CLI）、`coord_tests`（单元测试）。仓库根目录的 `coordination.cpp/.h` 是 Visual Studio 模板遗留，不属于工具链（`COORD_BUILD_LEGACY_DEMO` 默认关闭）。

### 6.3 编译器模块划分

| 模块 | 文件 | 职责 | 状态 |
| --- | --- | --- | --- |
| `Identifier` | `modules/identifier.h` | 标识符驻留（id ↔ text） | ✅ |
| `Syntax` / `Lexer` | `syntax.h` / `src/lexer.cpp` | `TokKind`、`Token`；Unicode 源码 → Token | ✅ |
| `AbstractSyntaxTree` | `ast.h` / `modules/abstract_syntax_tree.h` | AST 节点、`dumpAst` | ✅ |
| `Parser` | `parser.h` | 递归下降 + Pratt 表达式 | ✅ |
| `AnnotationRegistry` | `modules/annotation_registry.h` | 注解 Descriptor、MetaProcessor、注册与查找 | ✅ |
| `Descriptor` | `modules/descriptor.h` | `Field`/`Method`/`Variant`/`Declaration` 快照与 Builder | ✅ |
| `Outline` / `Hint` | `modules/outline.h`、`hint.h` | 声明大纲树与补全/提示 | ✅ / ⏳ CLI |
| `Interface` | `modules/interface.h` | `.coox` 声明级 writer/reader | ✅ 基础 |
| `Processor` | `modules/processor.h` | 注解处理器：description 快照、8 轮上限、生成声明 | ✅ 基础 |
| `Sema` | `sema.h` / `modules/compiler.h` | 名字/类型/继承/Scope/RAII 检查 | ✅ |
| `Operator` / `Scope` / `Access` / `Modifier` | `modules/operator.h`、`scope.h`、`access.h`、`modifier.h` | 运算符表、Scope 栈、访问、修饰符 | ✅ |
| `Platform` | `modules/platform.h` / `src/platform.cpp` | OS/架构/目标三元组、UTF-8 路径、`argv` 子进程、控制台编码 | ✅ |
| `Semver` / `Toml` / `Checksum` / `Archive` | `modules/semver.h`、`toml.h`、`checksum.h`、`archive.h` | 版本、TOML、SHA-256/CRC、压缩归档 | ✅ |
| `Manifest` / `Lockfile` | `modules/manifest.h`、`lockfile.h` | 清单、锁文件读写与指纹 | ✅ |
| `Net` / `Registry` / `Mirror` / `Resolver` | `modules/net.h`、`registry.h`、`mirror.h`、`resolver.h` | 下载、索引、镜像健康、回溯解析 | ✅ |
| `PackageManager` | `modules/package_manager.h` | install/update/add/remove/list/tree/clean/pack/publish/init/search | ✅ |
| `Module` / `Project` / `Package` / `Dependency` | `modules/module.h`、`project.h`、`package.h`、`dependency.h` | 发现、工程根、依赖图 | ✅ / ⏳ CLI |
| `Unicode` / `Character` / `Charset` | 同名头文件 | 码点校验、UTF-8 编解码、BOM | ✅ |
| `Stream` / `File` | `modules/stream.h`、`file.h` | 跨平台文本流与 UTF-8 文件 | ✅ |
| `Thread` / `Atomic` | `modules/thread.h`、`atomic.h` | RAII 线程边界、`Atomic<T>` | ✅ |
| `Compiler` / `Evaluator` / `Linker` | `modules/compiler.h` | 驱动门面、解释器包装、宿主链接边界 | ✅ 门面 |
| `Driver` | `driver.h` / `src/driver.cpp` | 生命周期、产物、`--emit-cpp`/`build`/`run` | ✅ |

依赖方向严格单向：

```text
Identifier / Access / Modifier / Operator / Scope / Unicode
        ↓
Syntax → Lexer → AST → Parser → AnnotationRegistry / Processor
        ↓
Descriptor → Outline → Hint ; Sema → Evaluator → Compiler → Linker
        ↓
Module / Package / Project / Dependency（独立工具链层，使用 File/Scope）
        ↓
coorc CLI
```

前端不依赖包管理；扫描器不依赖类型检查器；`codegen` 只依赖 `sema` 的类型与 `interp` 的常量表，不反向依赖 driver。`modules/*` 的纯数据/算法模块默认头文件实现，平台/IO 模块（platform、toml、archive、manifest、lockfile、net、registry、mirror、resolver、package_manager）用 `.cpp`，避免为工具链层引入过多链接单位。

### 6.4 支撑层与契约

- **SourceManager / Span**：每个文件一个 `std::unique_ptr<SourceFile>`，地址稳定，`string_view` 安全；预计算 `lineStarts`，`lineCol(Span)` 用 `upper_bound` 定位。`Span { file, begin, end }` 贯穿 lexer/parser/sema/diagnostics，偏移是规范化 UTF-8 字节偏移。
- **DiagnosticEngine**：诊断 = `{severity, code, message, span}`；渲染格式为 `path:line:col: error[E2030]: ...` + 源码行 + `^~~~`（行号/列号 1 基）。`-Werror` 把 warning 提升为 error；warning 仍保留代码（`W0001` 等）便于抑制。
- **Interpreter**：值域 `Void / Bool / Int / Decimal / Char / String`；Scope 栈与 Sema 同构（函数调用压 Function Scope，函数体再压 Block）；`return`/`break`/`continue` 用内部信号实现，回卷时必须弹出 Scope。
- **Unicode/编码**：源文件 UTF-8、UTF-16LE/BE、UTF-32LE/BE；BOM 可选，检测后剥离（UTF-32LE BOM 先于 UTF-16LE 检测）；内部统一 UTF-8；非法码点替换为 U+FFFD。`char` 是 32 位标量（后端 `char32_t`）。`Character` 提供 `isAscii`、`isDigit`、`isLetter`、`isWhitespace`、`isControl`、`toUtf8`、`decode`。
- **Stream / File**：内部 LF；读时 CRLF/CR 归一；写回一律 LF，因此行号/字节偏移跨平台一致。**Stream 的行索引 0 基**（诊断显示的行号则是 1 基），`readByLine` 返回不含换行的行内容；`lineCount` 按 LF 计数。`readByStream`/`writeByStream` 读写整段文本；`readByByteOffset(offset)` 返回到末尾的文本；`writeByByteOffset(offset, content)` 从 `offset` 覆盖写入，越界追加；`writeByLine(index, content)` 只替换已存在的行且 `content` 不得含换行。`File` 提供 `readAll`、`writeAll`、`exists` 与 Stream 方法。
- **Concurrency**：`Thread` 是 RAII 线程边界；`Atomic<T>` 提供无锁原子操作；工具链不使用全局可变状态，保证并行测试/构建可重复。
- **Platform**：所有跨平台差异集中于此：路径 UTF-8、目录约定、可执行/对象/库命名、`argv` 直接 spawn（无 shell）、超时/输出捕获、Windows UTF-16 命令行、控制台编码。

### 6.5 跨平台工具链

| 宿主 | 目标三元组 | 默认宿主 C++ 编译器 |
| --- | --- | --- |
| Windows x64/arm64 | `x86_64-pc-windows-msvc` / `aarch64-pc-windows-msvc` | `cl` |
| Linux x64/arm64 | `x86_64-unknown-linux-gnu` / `aarch64-unknown-linux-gnu` | `c++`（g++/clang++） |
| macOS x64/arm64 | `x86_64-apple-darwin` / `aarch64-apple-darwin` | `c++`（Apple clang） |
| FreeBSD | `*-unknown-freebsd` | `c++` |

- 宿主编译器探测顺序：`--cxx` → `COORD_CXX` → `CXX` → 平台候选（Windows `cl`/`clang++`/`g++`；POSIX `c++`/`clang++`/`g++`）；`--cxx-flavor msvc|gnu` 可强制参数风格。
- 工具链用 `platform::run(argv)` 直接启动子进程，**不经过 shell**；Windows 使用 `CreateProcessW`（UTF-16 命令行与环境块），Unix 使用 `fork`/`execvp` + 进程组；支持超时、输出捕获、stdin 管道与取消。
- 所有跨模块路径以 UTF-8 传递；Windows 转换到 UTF-16 文件 API；源文件内部 LF，读写归一；配置目录 `%APPDATA%\coord`、`~/Library/Application Support/coord`、`${XDG_CONFIG_HOME:-~/.config}/coord`，缓存目录对应 `%LOCALAPPDATA%\coord\cache`、`~/Library/Caches/coord`、`${XDG_CACHE_HOME:-~/.cache}/coord`，可用 `COORD_CONFIG`/`COORD_CACHE_DIR` 覆盖。
- CMake 使用 `Threads::Threads` 与平台 preset；Windows 用 Ninja + cl，Linux/macOS 使用 `linux-release`/`macos-release`/`linux-debug` preset。Linux/macOS 已用 g++ 本地验证；Windows 为主开发平台。
- 交叉编译：`coorc --target <triple>` 把 `--target` 传给 clang；GCC 交叉工具链通过 `--cxx` 指定；`--sysroot <path>` 传递 sysroot；MSVC 不支持交叉编译；交叉编译目标不能执行 `coorc run`。

### 6.6 测试现状

`ctest` 当前注册 **183 项**：`coord_unit` 加 182 个端到端测试。`coord_unit` 覆盖词法、解析、泛型、Scope、RAII、编译期求值、代码生成、接口、枚举、指针、Unicode/BOM、模块扫描、访问/继承、`super`、元注解、Optional/null、转换构造、Result/Expect、for-in、`@Signed/@Unsigned/@Length`、固定数组、数组字面量、ArrayView、OrderedMap、OrderedSet、UnorderedMap、UnorderedSet、128 位、Invocation/多 handler/虚分派、Processor 生成声明、Proxy class/annotation/runtime call/field trap/state、reflector、runtime retention/value/member/type、descriptor pipeline、ownership/optional/fields/params/returns/initializer、main 签名、`.coox` reader、package graph、outline/hint、`@Test`。e2e 覆盖示例与 fixtures 的编译、运行、负例、确定性、增量、循环依赖、两包构建、包管理器/镜像/注册表全流程；本轮新增覆盖为 `coord_e2e_optional_policies`、`coord_e2e_optional_error`、`coord_e2e_optional_implicit`、`coord_e2e_constructure`、`coord_e2e_constructure_error`、`coord_e2e_destructure_annotation`、`coord_e2e_proxy_construct_destructure`、`coord_e2e_platform` 与 `coord_e2e_retention_block`, `coord_e2e_radix`, `coord_e2e_radix_error`，以及 lambda 闭包切片 `coord_e2e_lambda_capture`/`_escape`/`_escape_scope`/`_unlisted`/`_readonly`/`_this`，以及控制流切片 `coord_e2e_match_patterns`、`coord_e2e_if_expression`、`coord_e2e_match_missing`、`coord_e2e_match_no_wildcard`、`coord_e2e_match_bad_pattern`、`coord_e2e_if_expression_type_error`，以及函数值切片 `coord_e2e_method_ref`、`coord_e2e_method_ref_escape`、`coord_e2e_method_ref_unbound`，以及函数式接口切片 `coord_e2e_functional_interface`、`coord_e2e_functional_interface_error`、`coord_e2e_functional_interface_arity_error`、`coord_e2e_functional_interface_methods_error`，以及线程切片 `coord_e2e_thread`、`coord_e2e_thread_capture_error`、`coord_e2e_thread_this_error`、`coord_e2e_thread_copy_error`，以及 Mutex 切片 `coord_e2e_mutex`、`coord_e2e_mutex_copy_error`、`coord_e2e_mutex_assign_error`，以及 ScopedLock/Atomic/@Lock 切片 `coord_e2e_atomic`、`coord_e2e_scopedlock_copy_error`、`coord_e2e_atomic_copy_error`、`coord_e2e_atomic_assign_error`、`coord_e2e_lock_annotation`、`coord_e2e_lock_annotation_error`，以及 ScopedThread/Atomic-ops 切片 `coord_e2e_scoped_thread`、`coord_e2e_atomic_ops`、`coord_e2e_scopedthread_copy_error`、`coord_e2e_scopedthread_assign_error`、`coord_e2e_scopedthread_capture_error`、`coord_e2e_scopedthread_this_error`，以及 @Lock 能力切片 `coord_e2e_lock_capture`、`coord_e2e_lock_capture_uncaptured_write`、`coord_e2e_lock_capture_const_write`、`coord_e2e_lock_capture_readonly_method`、`coord_e2e_lock_capture_value_class`、`coord_e2e_lock_capture_escape`、`coord_e2e_lock_legacy_error`、`coord_e2e_lock_nested_error`、`coord_e2e_lock_capture_uncaptured_call`、`coord_e2e_lock_capture_value_return`，以及 @Copy 切片 `coord_e2e_copy_deep`、`coord_e2e_copy_deep_unique`、`coord_e2e_copy_shallow`、`coord_e2e_copy_deep_share_error`、`coord_e2e_copy_shallow_unique_error`、`coord_e2e_copy_explicit_required_error`、`coord_e2e_copy_bad_mode_error`、`coord_e2e_lock_capture_deep`、`coord_e2e_lock_capture_shallow_error`。完整列表见 [`tests/CMakeLists.txt`](tests/CMakeLists.txt)。`COORD_SANITIZE=ON` 以 ASan/UBSan 构建编译器（含 `linux-sanitize` 预设）；`COORD_FUZZ=ON` 构建 Clang/libFuzzer 解析器与编译器 harness，见 [`tests/fuzz`](tests/fuzz)；编译器 harness 需让 `Checker` 存活到 emitter 结束，因为 `TypeExpr::resolved` 指向 checker 拥有的类型。诊断最多存储 256 条，解析器在成员错误恢复后强制前进，避免诊断无限增长。`coord_e2e_packages` 不依赖宿主编译器与网络；`coord_e2e_packages_build` 需要可用宿主 C++ 工具链。

## 7 标准库设计

> 标准库与编译器同版本发布；公开 API 用 Coordination 编写，底层通过 `@Native` 接 C/C++ 并在编译期链接。本文与实现冲突时，语言语义以本规范为准，标准库 API 以 §7 为准。

### 7.1 目标与约束

- **无宏**：格式化、测试发现、集合字面量都靠语言语法与注解，不引入文本宏。
- **公开 API 纯 Coo**：C/C++ 只出现在私有 `@Native` 边界，用户看到的类型、函数、方法都是 Coordination 声明。
- **跨平台一致**：路径、换行、字符编码、时间/随机行为由标准库统一，不把平台差异泄漏给用户。
- **可预测的所有权**：默认移动；拷贝需 `@Copy`；无 GC。
- **版本随编译器**：不承诺独立 ABI；`.coox` 带版本，1.0 前允许破坏性修改。
- **同步优先**：提供 Thread/Mutex/Channel/Atomic，不内建 async/await。

### 7.2 模块地图

标准库顶层模块互相并列，不带 `std.` 前缀：

| 模块 | 内容 |
| --- | --- |
| `utility` | `Iterable`、`Iterator`、`Hash`、`Eq`、`Ord`、`Move`、`Forward`、`Swap`、`Exchange`、`Min`、`Max`、`Clamp`、`Abs`、`Defer`、`ScopeGuard`、`BitCast`、字节序、`Format`、`Println` |
| `memory` | `Shared<T>`、`Weak<T>`、`Allocator`、`GlobalAllocator`；`@Unique`/`@Share` 是内建所有权注解 |
| `collection` | `Array<T>`、`ArrayView<T>`、`OrderedMap<K,V>`、`UnorderedMap<K,V>`、`OrderedSet<T>`、`UnorderedSet<T>` |
| `string` | `string` 扩展、`StringBuilder`、`StringView`、Unicode 迭代器、编码转换 |
| `io` | `Reader`、`Writer`、`Stream`、`Console`、`BufferedReader`、`BufferedWriter`、`EndOfStream` |
| `file` | `File`、`Path`、`Directory`、`FileStream` |
| `atomic` | `Atomic<T>`、`MemoryOrder` |
| `thread` | `Thread`、`Mutex<T>`、`MutexGuard<T>`、`Channel<T>`、`ChannelClosed` |
| `reflection` | `Reflection`、`Reflector<T>`、`Proxy<T,H>`、`ProxyHandler` |
| `time` | `Instant`、`Duration`、`Clock` |
| `mathematics` | `Vector2/3/4`、`Matrix`、标量数学函数 |
| `random` | `Random`、`SplitMix64`、`Xoshiro256`、`UniformInt`、`UniformFloat` |
| `test` | `@Test`、断言、`coorc test` 的运行期支持 |

导入规则：

```coo
import collection;          // 非限定名：Array<T>、OrderedMap<K,V>
import collection as c;     // 限定名：c.Array<T>
import string;              // string 扩展与 StringBuilder
```

模块内部可以拆文件，但 `import collection;` 看到的是该模块的公开导出集合。标准库 v0 每个顶层模块先实现为单个 `.coo`（或一个导出门面），后续可拆子模块。

### 7.3 Prelude 与命名

自动进入每个模块的 core 名（其余显式 import）：`Result`、`Expect`、`Error`、`Shared`、`Array`、`Iterator`、`Iterable`。内建基础类型仍为小写：`integer`、`string`、`bool`、`char`、`void`、`pointer` 等。标准库与用户 class 一律 PascalCase：

| 类别 | 规则 | 例子 |
| --- | --- | --- |
| 模块 | 小写 | `collection`、`atomic`、`thread` |
| 类型/协议 | PascalCase | `Array`、`OrderedMap`、`MutexGuard` |
| 函数/方法 | lowerCamel | `push`、`readLine`、`trySend` |
| 注解 | PascalCase | `@Test`、`@Native` |
| 枚举变体 | PascalCase | `Red`、`Green` |
| 内建类型 | 小写 | `integer`、`string`、`bool` |

### 7.4 `@Native` FFI（v1 子集）

```coo
@Native("getpid")
integer nativePid();

integer main() {
    print(nativePid());
    return 0;
}
```

- 当前实现只接受 `@Native("c_symbol")` 标注的**无函数体函数**；符号必须是 C 标识符。
- 支持的签名：`integer`、`boolean`、`float`、`string`、`void` 参数与返回值。`string` 参数以 `.c_str()` 传递；string 返回值从可空 `char*` 转换，`nullptr` 映射为空字符串。暂不支持 class、指针、回调或不透明句柄。
- 代码生成一个 Coo 可见的包装函数，内部调用 `::c_symbol(...)`；C 符号必须已由生成翻译单元的平台头文件声明（Windows 的 `windows.h`、POSIX 的 `unistd.h`）。
- `@Native` 不能与 `@Result`、`@Expect`、`@Test`、`@Unique`、`@Share`、`@Weak`、`@Optional` 或泛型组合（`E2078`）。
- 这是**不安全边界**：程序员必须保证 C ABI 与签名匹配；没有运行期 `dlopen`、动态查找或 FFI 元数据。
- 完整设计——`header`/`library` 参数、不透明 `@Native class`、native method、C++ ABI 选择、自动链接库——仍为 ⏳。

### 7.5 核心协议（utility）

```coo
interface Iterator<T> {
    T? next();
}

interface Iterable<T> {
    Iterator<T> iterator();
}

interface Hash {
    integer hash() const;
}

interface Eq<T> {
    bool equals(const T& other) const;
}

interface Ord<T> {
    int compare(const T& other) const;   // <0, 0, >0
}

interface Function<T, R> { R apply(T value); }
interface Consumer<T> { void accept(T value); }
interface Supplier<T> { T get(); }
interface Predicate<T> { bool test(T value); }
interface BiFunction<A, B, R> { R apply(A first, B second); }
```

- `Function`/`Consumer`/`Supplier`/`Predicate`/`BiFunction` 是函数式接口，lambda 的目标类型；`@FunctionalInterface` 可选标注，编译器检查恰好一个抽象方法。
- `T?` 是 `T | void`；`next()` 返回 `void` 表示结束。
- `for x in xs` 展开为 `iterator()` + `next()` 循环，不要求容器暴露下标。
- `Hash`/`Eq` 用于 `UnorderedMap`/`UnorderedSet`；`Ord` 用于 `OrderedMap`/`OrderedSet` 与排序。用户 class 可显式实现这些接口；v0 先手写，后续可用注解派生。

### 7.6 容器（collection）

```coo
Array<integer> xs = [1, 2, 3];
xs.pushBack(4);
xs.pushFront(0);
integer first = xs.popFront();
for x in xs { Println(x); }

OrderedMap<string, integer> ages;
ages.set("alice", 30);
integer? alice = ages.get("alice");
```

- `Array<T>` → `std::deque<T>`；队首/队尾操作 O(1)。已实现：类型、默认构造、字面量 `[1, 2, 3]`、`pushBack`/`pushFront`/`popFront`/`popBack`、`length`/`isEmpty`/`clear`、下标读写、`for ... in`、`view()` → `ArrayView<T>`；`ArrayView<T>` 支持 `length`/`isEmpty`/`view[i]`；`get`/`at` 返回 `@Optional T*`。
- 固定数组 `T[N]` → `std::array<T,N>`，长度是类型的一部分。
- `OrderedMap<K,V>` → `std::map<K,V>`；`UnorderedMap<K,V>` → `std::unordered_map<K,V>`；方法 `set(k,v)`、`get(k)`（缺失为 null）、`contains(k)`、`remove(k)`、`length`、`isEmpty`。`OrderedSet<T>` → `std::set<T>`；`UnorderedSet<T>` → `std::unordered_set<T>`；方法 `add/contains/remove/length/isEmpty`。
- 通用 API：`insert`、`remove`、`get`、`contains`、`length`、`clear`、`keys()`、`values()`、`entries()`、`iterator()`；`get` 返回 `V?`，`operator[]` 只用于已存在 key（插入用 `insert`）。
- 约束：`OrderedMap`/`OrderedSet` 的 key 需实现 `Ord`，按 key 升序；`UnorderedMap`/`UnorderedSet` 需实现 `Hash` + `Eq`，迭代顺序未定义。四种容器的字面量、迭代与自定义约束仍待做。
- 默认移动；`@Copy` 时拷贝为深拷贝（元素也必须 `@Copy`）；空字面量需显式类型注解。

### 7.7 内存与所有权（memory）

```coo
@Unique File file = File.open("a.txt");
@Share Config config = Config();
Shared<Config> shared = Shared.make(Config());
Weak<Config> weak = shared.weak();
if (weak.isPresent()) {
    @Optional @Share Config stillAlive = weak.upgrade();
}

@Optional @Unique File maybe = null;
if (maybe != null) {
    maybe.get().readAll();
}
File fallback = maybe.orElse(File.open("default.txt"));
```

- `&` 是非逃逸借用，不能存字段/容器/返回；逃逸用内建注解 `@Unique`/`@Share`。
- `@Unique`：唯一所有权，move-only；`@Target(type)` 注解；`isPresent()`、`isEmpty()`、`get()`（空时 panic）、`orElse(default)`。
- `@Share`：共享所有权，原子计数，copyable；`@Target(type)` 注解，提供同样的查询 API。
- `@Optional`：可空**类型**，用于 `T*`、`@Unique`、`@Share`；`@Target(type)` 注解；`ProxyPolicy` 见 §4.5.2；非 optional 默认非空，赋 `null` 报 `E2046`。
- `Weak<T>`：对应 `std::weak_ptr<T>`；`isPresent()` = 未过期，`isEmpty()` = 已过期，`upgrade() -> @Optional @Share T`。
- `Shared<T>`：显式堆分配的所有权指针；`Shared.make(args...)` 在堆构造；`Weak<T>` 不增加计数。
- `Allocator` 接口 `allocate/deallocate/reallocate`（均 `unsafe`）；`GlobalAllocator` 是默认实现；v0 的 `@Unique`/`@Share`/`Shared`/`Array` 固定使用它，自定义分配器后续开放。
- `new`/`delete` 仅允许在 `unsafe` 块内；普通代码只用 `@Unique`/`@Share`/`Shared`。
- C++ 后端：`@Unique T` → `std::unique_ptr<T>`，`@Share T` → `std::shared_ptr<T>`，`Weak<T>` → `std::weak_ptr<T>`。

### 7.8 字符串（string）

- 内建 `string` 是**不可变 UTF-8 字节序列**；`length` 返回字节数；`s[i]` 返回字节 `u8`。
- Unicode 通过迭代器：

```coo
for c in s.chars() { Println(c); }        // Unicode 标量
for g in s.graphemes() { ... }            // 字素簇（后续）
string sub = s[1..3];                     // 字节切片，返回 StringView
```

- `StringBuilder` 用于增量拼接；`StringView` 是非拥有只读切片。
- `f"..."` 是编译期插值（见 §7.14）；`Format`/`Println` 在 `utility`。
- 编码转换：`Charset`/`Unicode` 模块（UTF-8/16/32 + BOM），与编译器共用。

### 7.9 IO 与文件（io / file）

```coo
interface Reader {
    Result<integer, Error> read(Span<u8> buffer);
    Result<string, Error> readLine();
}

interface Writer {
    Result<integer, Error> write(Span<const u8> buffer);
    Result<void, Error> flush();
}
```

- 所有可失败 IO 返回 `Result<T, Error>`；读完返回 `EndOfStream` 错误。
- `Span<T>` 是非拥有视图；`Span<const u8>` 表示只读字节区。
- `Stream` 提供整段/按行/按字节偏移读写；行号 0 基，内部 LF，读时归一 CRLF/CR，写回 LF；跨平台一致。
- `File` 提供 `open`、`readAll`、`writeAll`、`openStream` 与 Stream 模式；`Path`/`Directory` 处理路径；`Console` 提供 stdin/stdout/stderr；`BufferedReader`/`BufferedWriter` 为可选缓冲层。
- 语言内建的 `readFile`/`writeFile`/`fileExists`/`deleteFile`/`timeMillis`/`sleepMillis` 是编译器前导提供的快捷 API；标准库接管后仍保持同名兼容。

### 7.10 并发（atomic / thread）

```coo
Atomic<integer> counter = Atomic<integer>(0);
counter.fetchAdd(1);

Mutex<Array<integer>> queue = Mutex(Array<integer>());
{
    MutexGuard<Array<integer>> guard = queue.lock();
    guard.get().pushBack(1);
}   // guard 析构解锁

Channel<integer> channel = Channel<integer>(16);   // 有界；Channel<integer>() 无界
channel.send(1);
integer value = channel.receive()?;                // 失败为 ChannelClosed
channel.close();
```

- `Atomic<T>`：`load`/`store`/`exchange`/`fetchAdd`/`compareExchange`；`MemoryOrder` 显式指定顺序。
- `Thread`：`Thread.spawn(function)`、`join`、`detach`、`hardwareConcurrency`。
- `Mutex<T>`：`lock() -> MutexGuard<T>`（RAII），`tryLock() -> MutexGuard<T>?`。
- `Channel<T>`：`send`/`receive` 返回 `Result`；`trySend`/`tryReceive`；`close()` 后 receive 返回 `ChannelClosed`；有界/无界构造。
- 不提供 async/await；需要事件循环时由用户库实现。

### 7.11 时间、数学、随机（time / mathematics / random）

- `Instant`（单调时钟点）、`Duration`（纳秒）、`Clock`（系统/单调）。
- `mathematics`：`Vector2/3/4`、`Matrix`、`Sqrt`、`Sin`、`Cos`、`Pow` 等；类型 PascalCase。
- `random`：`Random` 接口 + `SplitMix64`/`Xoshiro256` 实现；`UniformInt`/`UniformFloat` 分布；显式 seed。默认不自动播种，测试可复现。
- 随机数与时间均不参与编译期求值（编译期禁止 IO/时间/随机）。

### 7.12 测试（test + coorc test）

```coo
import test;

@Test
void additionWorks() {
    assertEq(1 + 1, 2);
    assertTrue(2 > 1);
    assertFalse(1 > 2);
}
```

- `@Test` 标注零参、返回 `void` 的函数；断言：`assert`、`assertEq`、`assertTrue`、`assertFalse`。
- `coorc test <file>` 编译并运行当前文件中收集到的 `@Test` 函数，**串行**执行；`--filter <substring>` 只运行名称包含该子串的测试；失败非零退出并打印文件/行。
- 测试函数可使用 `defer`、RAII 与普通标准库 API；并行执行、fixture、benchmark 后续加入。

### 7.13 反射与代理（reflection）

| 标准库类型 | 包装/角色 |
| --- | --- |
| `Reflection` | 静态反射入口 |
| `Reflector<T>` | 类型化反射视图，包装内建 `reflector<T>` |
| `Proxy<T,H>` | 代理包装，包装内建 `proxy`；`H` 实现 `ProxyHandler` |
| `ProxyHandler` | trap 接口；运行期代理的行为定义 |

```coo
description d = Reflection.describe<User>();
Array<description> members = Reflection.membersOf<User>();
description? name = Reflection.annotationOf<Serializable, User>();
integer size = Reflection.sizeOf<User>();

Reflector<User> r = Reflection.reflect<User>();
for field in r.fields() { Println(field.name()); }
```

- `describe<T>()`、`membersOf<T>()`、`annotationsOf<T>()`、`annotationOf<A,T>()`、`sizeOf<T>()`、`alignOf<T>()`；仅编译期可用，`Reflection` 不能作为运行期字段/参数类型（`E2009`）。
- `Reflector<T>`：`name()`、`kind()`、`fields()`、`methods()`、`annotations()`、`size()`、`align()`、`construct(args...)`、`invoke(method, args...)`；成员访问静态检查，拼错名字是编译错误；`construct`/`invoke` 受访问控制约束。
- `Proxy<T,H>` 是**独立包装类型**，不改变 `T` 的布局/ABI；`Proxy.make(value, handler)` 构造；Java Optional 风格 `isPresent()`/`isEmpty()`/`get()`/`unwrap()`；不是 `T` 的子类型。
- `ProxyHandler` 声明 `get/set/has/deleteProperty/keys/call/construct/destructure`；未实现的方法使用默认实现（直接转发）。`@Proxy` + `@Get/@Set/...` 与接口是同一套 trap 的两种写法：注解适合编译期，接口适合运行期/`@Retention` 代理。handler 状态放在旁路存储，不进入目标对象布局。
- `ProxyHandler` 子接口：`MethodProxyHandler`、`DestructureProxyHandler`、`ConstructureProxyHandler`、`ClassProxyHandler`、`ObjectProxyHandler`、`FunctionProxyHandler`。它们都继承 `ProxyHandler`；支持 `interface X extends ProxyHandler`，实现任一子接口即满足 `@Proxy` 的 handler 要求。

### 7.14 语法糖

| 糖 | 去糖 / 语义 |
| --- | --- |
| `for x in xs { ... }` | `Iterator<T> it = xs.iterator(); while (it.next()) { ... }` |
| `[1, 2, 3]` | `Array<T>` 构造 + `pushBack`；元素类型由上下文推断 |
| `[k: v, ...]` | `OrderedMap`/`UnorderedMap` 构造 + `insert` |
| `a..b` / `a..=b` | 半开/闭合区间值；`xs[a..b]` 返回 `ArrayView<T>`，边界检查 |
| `{a, b}` / `{y = "named", x = 9}` | `@Constructure` 类型化/命名构造；类型标签可省略；匹配带注解的构造函数 |
| `{x: 1}` | 聚合对象字面量（字段初始化），与 `@Constructure` 条目不同 |
| `[captures](const integer& x): integer -> expr` | lambda（目标类型 Java；显式捕获按 C++；省略捕获时外部变量按值复制且副本可变） |
| `Type::method` / `object::method` | 函数类型目标下的方法引用；静态引用降为函数指针，绑定引用按引用捕获接收者并遵循闭包逃逸规则；未绑定实例引用 ⏳ |
| `if let` / `match` | 解构 `Result`/`Expect`/联合；`match` 对枚举强制穷尽（`if let` 已移除） |
| `defer expr;` | 当前 Scope 退出时按声明逆序执行；与局部析构同一栈 |
| `f"{name}: {count}"` | 编译期解析占位符并做类型检查；`{{`/`}}` 转义 |
| `f(name: value)` | 命名实参；位置实参必须在命名实参之前 |

- 不使用 `?` 传播错误：错误默认自动冒泡；显式处理用 `isOk`/`unwrap`/`match`。
- 集合字面量在无法推断类型时报错；空字面量必须写 `Array<T>()`。

### 7.15 错误类型

```coo
interface Error {
    integer code() const;
    string message() const;
    string source() const;
    Backtrace backtrace() const;
}
```

- 子类型：`IoError`、`EndOfStream`、`ParseError`、`AllocError`、`ChannelClosed`。
- `Result<T, Error>` 是 prelude 类型；`Expect<T, Error>` 必须显式消费。
- 标准库函数不抛异常、不 panic（除分配失败等不可恢复场景）；可恢复错误一律返回 `Result`。

### 7.16 实现与发布

- 源码：`lib/utility/`、`lib/memory/`、`lib/collection/`、`lib/string/`、`lib/io/`、`lib/file/`、`lib/atomic/`、`lib/thread/`、`lib/reflection/`、`lib/time/`、`lib/mathematics/`、`lib/random/`、`lib/test/`。
- 公开层纯 Coo；平台相关部分在 `lib/*/internal/*.coo` 中用 `@Native` 声明。
- 编译器按 `import` 从工具链 `lib/` 与项目依赖中解析模块，生成/缓存 `.coox`。
- 标准库与编译器同版本；`.coox` 记录版本与公开 API 摘要；标准库自身测试放在 `lib/*/tests/*.coo`，用 `@Test` 与 `coorc test`。
- 发布包含源码与预编译 `.coox`；不使用独立包管理器版本。

### 7.17 里程碑与待决

**P5 首批验收**：`Array`/`OrderedMap`/`UnorderedMap` + 迭代协议 + 字面量/切片/for-in；`@Unique`/`@Share`/`@Optional` + `Shared` + `Allocator`；`string` 字节索引 + `chars()` + `f"..."` + `StringBuilder`；Reader/Writer/Stream/File/Path，LF 归一；Thread/Mutex/Channel/Atomic；`Reflection`/`Reflector<T>`/`Proxy<T,H>`/`ProxyHandler`；`@Test` + `coorc test`；`Instant`/`Duration`、基础数学、`Random`。

**待决**：未绑定实例方法引用与重载/歧义解析策略；`OrderedMap` 底层（B-tree vs 有序数组）；`Span<T>`/`ArrayView<T>`/`StringView` 是否合并；`Format`/`f"..."` 占位符语法与本地化；测试并行/fixture/benchmark；自定义 Allocator 是否进入容器泛型；`@Native` C++ ABI 稳定范围与平台 shim 组织。

## 8 包、工作区、锁、注册表与镜像

> 包管理器已实现（`coorc 0.2`）。本节描述 `coordination.toml` / `coord.pkg`、`coord.lock`、注册表协议、镜像选择与离线行为。

### 8.1 设计目标

- **零外部依赖**：注册表可以是本地目录或 HTTP(S)；校验和、gzip/tar、TOML 解析都由工具链自己实现。HTTP 走系统 `curl`，不引入 TLS 库。
- **可复现**：`coord.lock` 固定版本、来源与 SHA-256；`--locked` / `--frozen` 在锁文件过期时直接失败。
- **可离线**：`--offline` 只读缓存与 `coord_modules/`；`file://` 注册表不需要网络。
- **可镜像**：注册表与镜像是两层概念；按优先级与健康状态自动故障转移。
- **跨平台**：Windows / macOS / Linux / FreeBSD 使用同一套命令与目录布局；路径、编码、子进程启动由 `modules/platform.h` 统一。

### 8.2 清单

包根目录下有清单文件即可成为包：

| 文件 | 说明 |
| --- | --- |
| `coordination.toml` | 完整格式，推荐 |
| `coord.pkg` | 行式 M0 格式，向后兼容 |

两者同时存在时优先 `coordination.toml`。扫描时忽略 `out/`、`coord_modules/`、`.git/`、`.vs/`、`build/`、`cmake-build-*/`。

`coord.pkg`：

```text
name math
version 1.2.0
edition 2026
source src
description math helpers

dependency ../lib              # 路径依赖
dependency core ^0.3           # 注册表依赖
dependency util@~1.4           # @ 形式等价
dev-dependency testkit 0.1
build-dependency codegen 0.2
registry official              # 该包依赖的默认注册表
mirror cn                      # 默认镜像
```

字段：`name`（必填）、`version`、`edition`、`source`、`description`、`license`、`repository`、`homepage`、`registry`、`mirror`、`dependency`、`dev-dependency`、`build-dependency`；未知字段报错。路径依赖判定：以 `.`、`/`、`\` 开头，或包含 `/`、`\`，或前缀 `path:`；其余视为 `name [requirement]`。

`coordination.toml`：

```toml
[package]
name = "app"
version = "0.1.0"
edition = "2026"
source = "src"
description = "demo"
license = "MIT"
registry = "official"     # 本包依赖默认注册表
mirror = "cn"             # 本包默认镜像

[dependencies]
math = "^1.2"
local = { path = "../lib" }
core = { version = "~0.3", registry = "official", platform = "windows" }
optional = { version = "^0.1", optional = true }

[dev-dependencies]
testkit = "0.1"

[build-dependencies]
codegen = "0.2"

[registries]
official = "https://registry.coordination-lang.org"
internal = "file:///srv/coord-registry"

[[mirrors]]
name = "cn"
registry = "official"
url = "https://cn.registry.coordination-lang.org"
priority = 10
enabled = true
description = "Asia-Pacific"

[workspace]
members = ["lib", "tools"]   # 预留：当前只解析，不递归安装
```

依赖也可写成子表：

```toml
[dependencies.math]
version = "^1.2"
mirror = "cn"
```

依赖字段：`version`、`path`、`registry`、`mirror`、`platform`、`optional`；`requires` 仅为 `coorc deps` 的排序提示，解析器忽略。`platform` 过滤：`windows`、`linux`、`macos`、`unix`、架构名（`x86_64`/`aarch64`/...）、`os-arch`，支持 `,`/空格分隔与 `!` 取反；不匹配平台的依赖不会解析；`optional = true` 的依赖仅在 `--all-optional` 时解析。

### 8.3 版本与约束

语义化版本 `major.minor.patch[-prerelease][+build]`：

| 写法 | 范围 |
| --- | --- |
| `*` / 空 | 任意稳定版本 |
| `1.2.3` / `^1.2.3` | `>=1.2.3 <2.0.0` |
| `^0.2.3` | `>=0.2.3 <0.3.0` |
| `^0.0.3` | `>=0.0.3 <0.0.4` |
| `~1.2.3` | `>=1.2.3 <1.3.0` |
| `~1.2` | `>=1.2.0 <1.3.0` |
| `1.2.*` | `>=1.2.0 <1.3.0` |
| `>=1.0, <2.0` | 交集 |
| `=1.2.3` | 精确 |

预发布版本只在其 `major.minor.patch` 被显式写入约束时匹配（`>=1.0.0-alpha` 可以匹配 `1.0.0-beta`；`^1.0.0` 不匹配 `1.1.0-beta`）。

### 8.4 解析

1. 先处理路径依赖（递归读取对方清单），版本由对方清单决定；约束不满足或同名包来自两个路径时直接报错；路径环报 `cyclic path dependency`。
2. 注册表依赖做回溯搜索：每次选一个尚未确定的包，按“锁定的版本优先，其余从新到旧”尝试；加入该版本在索引中声明的依赖约束；冲突时回溯。
3. 同名包不能同时来自两个注册表；注册表包不能依赖路径包。
4. 结果按包名排序，保证确定性。
5. 锁文件存在且未 `update` 时优先使用锁定版本；`coorc update [name...]` 忽略对应包的锁定版本。

失败信息包含每个约束的来源与实际可用版本。

### 8.5 `coord.lock`

```toml
# Generated by coorc. Do not edit manually.
version = 1
manifest-hash = "sha256:..."

[[package]]
name = "math"
version = "1.2.0"
source = "registry"          # registry | path
registry = "official"
url = "https://..."
checksum = "sha256:..."
dependencies = ["core 0.3.1"]

[[package]]
name = "lib"
version = "0.1.0"
source = "path"
path = "../lib"
```

- 由 `coorc install` / `update` / `add` / `remove` 自动写入，原子替换。
- `manifest-hash` 是清单中影响解析的字段的 SHA-256。
- `--locked`：解析结果必须与锁文件一致，否则失败；`--frozen`：等价 `--locked`，且绝不写锁文件。
- 提交 `coord.lock`（库包可选，可执行包建议提交）；`coord_modules/` 通常加入 `.gitignore`。

### 8.6 注册表协议

注册表是目录树（本地目录或 HTTP(S) 根）：

```text
index.toml                        # 可选，coorc search 用
index/<name>.toml                 # 单包版本索引
packages/<name>/<version>.tar.gz  # 包归档（也接受 .tar / .zip）
health                            # 可选，镜像探测用
```

`index/<name>.toml`：

```toml
name = "math"
description = "math helpers"

[[version]]
version = "1.2.0"
checksum = "sha256:..."
dependencies = ["core ^0.3"]
yanked = false
archive = "packages/math/1.2.0.tar.gz"   # 可选，覆盖默认路径
```

- 归档根目录为 `<name>-<version>/`（提取时自动剥离单一顶层目录），其中必须含清单；清单 `name` 必须与索引一致，`version` 必须一致（`coord.pkg` 允许缺省为 `0.0.0`）。
- 校验和在解压之前完成；索引没有 checksum 时使用首次下载的实际 SHA-256 并发出警告。
- `coorc pack` 生成确定性 `.tar.gz`：条目排序、mtime 0、gzip 使用 stored 块。
- `coorc publish` 对本地注册表直接写归档、更新 `index/<name>.toml` 与 `index.toml`；对远程注册表使用 `PUT <base>/api/v1/packages/<name>/<version>`，凭据来自 `COORD_TOKEN`。

### 8.7 镜像管理器

- **registry**：包仓库端点，有名字（`official`）和 URL。
- **mirror**：同一 registry 的另一个端点，带 `priority`（越大越先尝试）。

配置层（后者覆盖前者）：

1. 内置目录：`official`（默认启用），`cn` / `eu`（默认禁用，`mirror use` 启用）。
2. 用户配置 `config.toml`（`coorc mirror add/use` 写入）。
3. 项目清单 `[registries]`、`[[mirrors]]`、`[package].registry/mirror`。
4. 环境变量：`COORD_REGISTRY`、`COORD_MIRROR`、`COORD_OFFLINE`、`COORD_TIMEOUT_MS`、`COORD_CONFIG`、`COORD_CACHE_DIR`、`COORD_CURL`。
5. 命令行：`--registry`、`--mirror`。

请求顺序：显式镜像 → 该 registry 已启用镜像按 priority 降序 → registry URL 本身；URL 去重。失败记录到 `<cache>/mirror-health.toml`，连续失败 2 次后 5 分钟内跳过（显式指定除外）。

```bat
coorc mirror list
coorc mirror add cn https://cn.example/coord --registry official --priority 10
coorc mirror use cn
coorc mirror test cn
coorc mirror auto
coorc config set mirror.timeout-ms 5000
```

`health` 不存在时探测 `index.toml`；`mirror test` 输出可达性与延迟；`mirror auto` 选择当前最快可达镜像。内置 `cn`/`eu` 的 URL 是占位端点，实际使用需用 `mirror add` 或项目清单覆盖。

### 8.8 缓存与 vendor

```text
<cache>/coord/
  registries/<hash>/index/...      # 索引缓存（远程 TTL 1h；本地直读）
  downloads/<name>-<version>.archive
  sources/<name>-<version>-<hash>/ # 解压后的包，含 .coord-package.toml
  tmp/                             # 解压临时目录
  mirror-health.toml

<project>/
  coord.lock
  coord_modules/<name>/            # vendor，含 .coord-package.toml
```

- `<cache>`：Windows `%LOCALAPPDATA%\coord\cache`，macOS `~/Library/Caches/coord`，Linux `${XDG_CACHE_HOME:-~/.cache}/coord`；可用 `COORD_CACHE_DIR` 覆盖。
- `coorc clean` 删除 `coord_modules/`；`coorc clean --cache` 同时清缓存。
- `coorc build` 自动把 `coord_modules/*`、清单中的路径依赖（递归）与当前包 `source` 目录合并，因此安装后无需改语言 import；增量 stamp 包含根清单、`coord.lock`、各 vendor 清单、文件数与最新 mtime，以及宿主目标三元组与编译器。

### 8.9 离线与可复现

- `coorc install --offline`：索引/归档只读缓存；vendor 完整时完全不访问网络与缓存。
- `--locked`：CI 推荐；清单或解析结果变化直接失败，提示 `coorc update`。
- `--frozen`：在 `--locked` 基础上去掉写锁文件，适合只读工作区。
- 缓存/索引损坏：删除 `<cache>/coord` 或 `coorc clean --cache` 后重试。

### 8.10 安全

- 所有归档成员名在剥离顶层目录**之前**校验：拒绝绝对路径、盘符、`..`、Windows 保留名与非法字符。
- 默认拒绝符号链接；启用时目标必须相对且不得逃逸。
- 解压有总字节数（512 MiB）与条目数上限；归档文件本身上限 1 GiB。
- 归档 SHA-256 在解压前校验；锁文件固定校验和。`[trust] require_checksum = true` 会拒绝没有校验和的注册表条目，而不是只警告。
- `[trust] allow_insecure_http = false`（默认）拒绝非回环 `http://` 注册表 URL；`file://`、`https://`、`localhost`、`127.0.0.1`、`[::1]` 允许。
- HTTP 由系统 `curl` 执行；失败信息包含每个镜像的尝试结果。私有注册表可通过 `COORD_TOKEN` 传递 Bearer token（`publish`）。包签名/信任链仍属未来工作。

### 8.11 CLI

```bat
coorc init [dir] [--name <name>] [--force]
coorc install [--locked|--frozen] [--offline] [--no-dev] [--all-optional]
coorc update [package...]
coorc add <name[@req] | path> [--dev] [--path]
coorc remove <name>
coorc list
coorc tree
coorc clean [--cache]
coorc pack [-o archive.tar.gz]
coorc publish [--dry-run]
coorc search <term>
coorc mirror list|show|add|remove|use|test|auto
coorc config list|get|set
```

包命令共用：`--manifest <path>`、`--mirror <name|url>`、`--registry <name|url>`、`--verbose`。清单自动从当前目录向上查找。`add`/`remove` 会先改清单再安装；安装失败自动回滚清单。

### 8.12 限制与后续

- workspace `members` 只解析、不递归安装；
- 无 feature/可选依赖组合（`--all-optional` 为全量开关）；
- 无 git 依赖、无包签名/信任链（SHA-256 + TLS + token 是当前边界）；
- 注册表 `search` 依赖 `index.toml`（本地注册表会扫描 `index/`）；
- 交叉编译已支持 `--target`/`--sysroot`/`--cxx`，但 MSVC 宿主不能交叉编译，交叉目标不能 `run`。

## 9 C++ 后端映射

### 9.1 类型映射

| Coordination | C++20 |
| --- | --- |
| `void` / `bool` | `void` / `bool` |
| `integer`（默认） | `std::int64_t` |
| `@Unsigned integer` | `std::uint64_t` |
| `@Signed integer` | `std::int64_t` |
| `@Length(8/16/32/64)` | `std::uint8_t`…`std::uint64_t` / `std::int8_t`…`std::int64_t` |
| `@Length(128)` | `coord_rt::UInt128` / `Int128` |
| `float` | `double` |
| `decimal` | `coord_rt::Decimal`（value + scale 定点） |
| `record` / `Record` | `coord_rt::Record`（根类型） |
| `char` / `string` | `char32_t` / `std::string` |
| `Enumeration` / 具体 `enumerate` | `enum class`（底层 `std::int64_t`），带方法/接口时为同布局包装 |
| `T&` / `const T&` / `T*` / `const T*` | 同名 C++ 形式 |
| `T[N]` | `std::array<T, N>` |
| `Array<T>` | `std::deque<T>` |
| `ArrayView<T>` | `coord_rt::ArrayView<T>` |
| `OrderedMap<K,V>` | `std::map<K,V>` |
| `UnorderedMap<K,V>` | `std::unordered_map<K,V>` |
| `OrderedSet<T>` | `std::set<T>` |
| `UnorderedSet<T>` | `std::unordered_set<T>` |
| `@Unique T` | `std::unique_ptr<T>` |
| `@Share T` | `std::shared_ptr<T>` |
| `Weak<T>` | `std::weak_ptr<T>` |
| `@Result<T>` | `coord_rt::Result<T>` |
| `@Expect<T>` | `coord_rt::Expect<T>` |
| 用户 `class` | 同名 `class`；与 C++ 关键字冲突时加后缀 `_` |
| `Reflector<T>` / `Proxy` / `Description` 等元类型 | 不承担运行期语义；`Description` 目前仅保留 `std::string` 占位 |

### 9.2 类、接口与继承

- 生成 `class C : public Base, public I1, public I2 { ... }`；`final` 类生成 `class C final`；访问段生成 `public:` / `private:` / `protected:` 标签，首个成员为 `private` 时省略标签（C++ 默认）。
- 接口生成抽象类：`virtual Ret m(params) = 0;`，带 `virtual ~I() = default;`；默认方法生成函数体。方法按声明输出 `static`/`virtual`/`override`/`final`，无函数体时 `= 0;`。
- 字段按声明顺序输出，`private` 字段也参与布局；构造体原样输出（`: super(args)` / `: field(expr)` 映射到 C++ 初始化列表）；析构：用户体原样输出，合成析构输出 `~T() = default;` 并附“逆序析构成员，最后基类”注释，类含 virtual 方法（或实现接口）时前置 `virtual`。
- `~this.member;` 不生成代码，只生成注释（避免二次析构）。

### 9.3 枚举

- 纯枚举降级为 `enum class Color : std::int64_t { Red, Green = 3, Blue };`；带方法或 `implements` 的枚举生成同布局包装类（一个底层整数字段 + `static constexpr` 变体常量 + 成员函数/接口 vtable），对象大小仍是一个整数（⏳）。
- 变体访问 `Color.Red` → `Color::Red`；`print` 输出底层整数；`match` 穷尽后生成 `switch`，每个变体一个分支、无 `default`；`e as integer` 取底层值，`n as Color` 生成范围检查（⏳）；`@Retention(enum)` 额外生成旁路表，不改变枚举值布局（⏳）。

### 9.4 RAII、泛型与入口

- `~this.member;` 不生成代码；成员析构完全交给 C++：析构体执行完后，C++ 按声明逆序析构成员，再析构基类。局部对象直接用 C++ 局部变量；`{}` 与 Scope 一一对应，花括号原样保留。错误冒泡在 P3 映射为提前 `return`，同样触发 C++ 析构。
- 泛型单态化到 C++ 模板：`integer add<N extends integer>(const N& a, const N& b);` 生成 `template <typename N> requires (std::is_integral_v<N>) std::int64_t add(const N& a, const N& b);`。`extends` 上界映射为 `requires` 子句；无上界时生成无约束模板；`where` 结构约束继续映射到 `requires`（⏳）。
- `@Constructure` 按匹配到的构造函数参数顺序降级为 `Type(arg1, arg2, ...)`；目标为 `@Proxy` 时先发射 handler 的 `@Construct` 调用。目标类 `@Destructure` 方法降级为“逐绑定默认初始化 + 可选逐绑定 `@Destructure` handler 调用 + `obj.method(binding1, binding2, ...)`”；字段解构则发射逐字段读取。
- `@Retention(Compile)` 代码块由解释器在编译期执行，C++ 后端省略；其中的 `print` 写入编译期输出（`coorc --print-consts` 可见），不进入生成程序。`@Retention(Runtime)` 代码块正常发射并在运行时执行。
- `integer main()` / `integer main(Array<string>)` / `integer main(integer, Array<string>)` 生成 `coord_program::coord_main(...)`，平台入口函数调它并返回其退出码；`void main` 报 `E2052`。
- `print(x)` 由生成的前导提供（`coord_rt::print` 重载：`std::string`、`bool`、`char32_t`、算术类型、`enum`），每次输出一行；标准库的 `Println` 将在 P5 接管/取代它。注解声明在 C++ 后端只生成“编译期专用、无运行期代码”的注释。

### 9.5 生成程序的跨平台契约

生成 C++ 使用标准 C++20 + 条件 `<windows.h>`；平台差异只进入生成前导与内建实现，用户程序不需要写平台分支。

| 关注点 | Windows / MSVC | POSIX（Linux/macOS/FreeBSD） |
| --- | --- | --- |
| 入口 | `wmain`，把 UTF-16 `argv` 转成 UTF-8 后再调用 `coord_main` | `main`，`argv` 已是字节串（按 UTF-8 解释） |
| 控制台 | 启动时 `SetConsoleOutputCP(CP_UTF8)` / `SetConsoleInputCP(CP_UTF8)` | 继承终端编码，不做切换 |
| 字符串 | 宽字符入口 + UTF-8 内部表示 | UTF-8 内部表示 |
| 路径 | UTF-8 → `std::filesystem::path`（用 `u8string` 构造） | UTF-8 → `std::filesystem::path` |

- 路径/文件 API 统一使用 UTF-8 → `std::filesystem::path`；Windows 侧通过 `u8string` 转换，避免 `std::string` 路径按 ANSI 代码页解释。
- 内建文件/时间函数：`readFile`、`writeFile`、`fileExists`、`deleteFile`、`timeMillis`、`sleepMillis`。
- 新增程序可用的平台内建：`operatingSystem()`、`architecture()`、`targetTriple()`、`pathSeparator()`、`lineSeparator()`、`executableSuffix()`、`currentDirectory()`、`temporaryDirectory()`、`homeDirectory()`、`environment(name) -> Result<string>`、`setEnvironment`、`pathJoin`、`pathFileName`、`pathParent`、`pathExtension`、`pathIsAbsolute`、`pathNormalize`、`pathExists`、`createDirectory`、`createDirectories`、`deleteDirectory`、`listDirectory(path) -> Result<Array<string>>`。
- 输出保持 LF；诊断/源码位置使用原始 `.coo` 的 Span，不因平台换行改变。

### 9.6 交叉编译

- `coorc --target <triple>`：clang 宿主会收到 `--target <triple>`；`--sysroot <path>` 传递 sysroot。
- GCC 交叉工具链通过 `--cxx <cross-g++>` 指定；工具链按 `--cxx-flavor gnu` 组织参数。
- MSVC 不能交叉编译；MSVC 宿主上只能构建 Windows 目标。
- 交叉编译目标不能执行 `coorc run`（运行会报错），只能 `check`/`build`/`--emit-cpp`。
- `coorc platform` 打印宿主与目标三元组、可执行/对象/库命名；`coorc doctor` 额外检查编译器候选、`curl`、配置/缓存目录与清单。

### 9.7 保留阶段、`#line` 与后端约束

- Runtime/Enum/Invocation 物化：旁路只读表在 `coord_meta` 命名空间（键为稳定的 `DeclarationId`），每个保留注解类型生成窄访问器；`@Inherit` 的 effective 列表编译期展开后写入；目标布局、字段顺序、vtable 不变（⏳ P2）。
- Invocation hook 的生成点：调用前 `handler.before(args...)`（参数以引用传入）、调用后 `handler.after(result)`、字段读改写为 `handler.on_get(field)`、字段写改写为 `handler.on_set(field, value)`、构造末尾 `handler.after_construct(this)`、proxy trap 运行期分派；值改写通过引用/返回槽完成，不引入新类型或对象头。
- `line(file, line)`：后端维护源码映射栈，进入被注解的声明/块时压入，退出时弹出；目标代码前输出 `#line <line> "<file>"`，结束时恢复上一层；文件名字符串需转义 `\` 与 `"`，行号必须 ≥ 1；坐标诊断仍使用原始 `.coo` 的 Span。
- 后端只使用受限 C++ 子集：C++20、无异常、无 RTTI、无用户可见模板元编程；ABI、调试信息与优化交给宿主编译器。宿主编译器 Windows 默认 `cl`、POSIX 默认 `c++`，可用 `--cxx`/`COORD_CXX`/`CXX` 覆盖；参数为 `/std:c++20 /EHsc /utf-8`（MSVC）或 `-std=c++20 -O2 -pthread`（POSIX）。生成文件默认临时删除，`--keep-cpp` 或 `--emit-cpp` 保留，`--emit-cpp -` 输出到 stdout；所有用户标识符经 C++ 关键字检查（冲突加 `_`），字符串/字符字面量转义后再输出。

## 10 诊断码

### 10.1 错误码

| 码 | 含义 |
| --- | --- |
| `E1001` | 词法错误（未终止字符串/注释、非法字符、数字越界、不支持的后缀） |
| `E1101` | 语法错误 |
| `E2001` | 重复类型声明 / 重复常量 |
| `E2002` | 同 Scope 重复声明 / 成员重复 / 模块级不允许的声明 |
| `E2003` | 未知类型 |
| `E2004` | 未知标识符 / 非法基类 / 非法成员访问 / `this` 越界 |
| `E2005` | 类型不匹配（含赋值、实参、运算符、const 赋值、Optional 回退值不可赋值） |
| `E2006` | 构造函数名错误 |
| `E2007` | 析构函数签名错误 |
| `E2008` | 非法 `~this.member;`（不在析构体内、未命名字段、字段不存在） |
| `E2009` | 元类型进入运行期声明 |
| `E2010` | 接口方法未实现 |
| `E2011` | `main` 签名错误（多定义、带参数、返回类型非 void/integer） |
| `E2012` | 尚未实现的特性（数组、`match` 等） |
| `E2013` | 接口不允许字段 |
| `E2014` | 注解体只允许方法 |
| `E2020` | 枚举变体值不是整数常量表达式 |
| `E2021` | 枚举上的 `@Retention` 必须是 `@Retention(enum)` |
| `E2022` | 重复的枚举变体名 |
| `E2023` | 重复的枚举值 |
| `E2024` | 枚举没有该变体 |
| `E2030` | 访问越界（字段/方法/构造） |
| `E2031` | 继承 `final` 类 |
| `E2032` | 覆写 `final` 方法 / 隐藏 `final` 字段 |
| `E2033` | `override` 缺少 virtual 基方法 |
| `E2034` | 返回值不协变 |
| `E2035` | 覆写 virtual 方法未标 `override`（含接口实现） |
| `E2039` | 非逃逸 `&` 被返回、存字段/容器、跨线程或逃逸闭包捕获——也覆盖按引用闭包被返回、存入字段/容器字面量或赋给外层作用域变量 |
| `E2040` | 注解目标不匹配（`@Target`） |
| `E2044` | 代码块上的 `@Retention` 阶段非法（只允许 `Compile`/`Runtime`；`Invocation`/`enum` 报错） |
| `E2045` | `@Unique` 被复制、移动后使用，或借用了已移动的 `@Unique` |
| `E2046` | 把空值赋给非 `@Optional` 的 `T*`/`@Unique`/`@Share` |
| `E2047` | `@Optional` 用于非 `T*`/`@Unique`/`@Share` 类型 |
| `E2048` | `ProxyPolicy.Error` 下直接使用未处理的代理：未收窄的 Optional 访问，或缺少处理路径的代理构造/解构 |
| `E2049` | `A a{...}` 不是聚合类，或初始化项过多 |
| `E2050` | 在非 `@Result`/`@Expect` 函数中调用 `@Result` 函数 |
| `E2051` | 同一函数同时标 `@Result` 与 `@Expect` |
| `E2052` | `error(...)` 不在 `@Result`/`@Expect` 返回位置，或 `@Result main` 返回类型错误 |
| `E2053` | 同一类型同时标 `@Signed` 与 `@Unsigned` |
| `E2054` | `@Signed`/`@Unsigned` 用于非 `integer` 类型 |
| `E2055` | `@Expect` 结果未消费（被当作语句丢弃） |
| `E2056` | `@Length` 不是 8/16/32/64/128 位 |
| `E2057` | `@Length` 用于非基本数值类型，或 `@Length(128)` 不是 integer |
| `E2058` | `@Processor` 方法返回类型不是 void |
| `E2059` | `@Processor` 方法参数不是单个 `description` |
| `E2060` | 一个 annotation 声明了多个 `@Processor` 方法 |
| `E2061` | `@Processor` 出现在 annotation body 之外 |
| `E2063` | `@Proxy` handler/trap 非法、缺少 trap 或策略非法；`@Destructure`/`@Construct` 钩子签名非法 |
| `E2064` | `@Test` 不是零参 void 函数，或测试/断言参数错误 |
| `E2065` | `@Unique/@Share` 同时使用或用于非 class/pointer 类型 |
| `E2066` | `@Invocation` handler 缺失/不是 class/缺少 public void before()/after() |
| `E2067` | 处理器体使用了不支持的语句/表达式/description 方法 |
| `E2068` | 处理器生成轮次超过上限（默认 8） |
| `E2069` | `Reflector<T>` 索引越界 |
| `E2070` | import 无法解析 |
| `E2071` | 标准注解库无法读取 |
| `E2073` | `@Result/@Expect` 缺少期望类型或与返回类型不匹配 |
| `E2074` | `@Proxy` handler 未实现 `ProxyHandler` 或其子接口 |
| `E2075` | `@Invocation` handler 未实现 `InvocationHandler` |
| `E2076` | `@Constructure` 字面量无匹配或歧义构造器；也用于文档注释标签非法/重复 |
| `E2077` | `match` 未穷尽（缺枚举变体/布尔值，或非枚举标量缺 `_`）、值类型不支持、模式与值类型不符、范围模式不是整数/字符、绑定或 `|` 用法非法 |
| `E2087` | lambda 捕获非法：未知名字、重复捕获、默认捕获位置/重复、混用默认模式、显式列表下使用未列出外层变量、未写 `[this]` 却隐式使用 `this` |
| `E2088` | 对显式按值 lambda 捕获赋值或 `++`/`--`（v0 只读） |
| `E2089` | `@FunctionalInterface` 非法：抽象方法数不为 1，或函数式接口类型实参数量错误 |
| `E2090` | Thread/ScopedThread 规则：入口调用/签名错误、按引用或捕获 `this`、调用期借用的函数值、拷贝/赋值线程值、`hardwareConcurrency()` 带参数 |
| `E2091` | `Mutex` 值不能拷贝或赋值 |
| `E2092` | `ScopedLock`/`Atomic` 值不能拷贝或赋值，或 `ScopedLock` 未用 `ScopedLock(mutex)` 就地初始化 |
| `E2095` | `@Copy` 错误：缺失/非法模式、`@Copy(Deep)` 含 `@Share`/指针/线程/mutex 字段或 shallow 字段、`@Copy(Shallow)` 含 `@Unique`、含 owning 字段的类未显式 `@Copy` 就被复制 |
| `E2094` | `@Lock` 能力错误：旧语法、非 Mutex/非具名 mutex 参数、写未捕获或 `const&` 对象、经 `const&` 调用非 `@ReadOnly` 方法、class/container/pointer 的值捕获 |
| `E2080`–`E2086` | 规划中的 Scope 专用诊断（不可见、同域冲突、歧义、声明点前使用、import 冲突、私有访问、初始化环） |
| `E3001` | 编译期求值错误（fuel 耗尽、递归超限、除零等） |

### 10.2 警告码

| 码 | 含义 |
| --- | --- |
| `W0001` | 用户析构漏写 class 类型成员的析构声明 |
| `W0002` | 对非 class 成员写析构声明 |
| `W0003` | 重复析构声明（最后一次生效） |
| `W0004` | lambda 参数遮蔽外层局部变量/参数 |
| `W0005` | `@Share` 引用环导致对象无法释放（泄漏警告） |

### 10.3 诊断渲染

诊断格式：`path:line:col: error[E2030]: message`，随后输出源码行与 `^~~~` 指示区间；行列号 1 基。`-Werror` 把 warning 提升为 error，但保留 warning 代码以便按需抑制。`Span` 始终指向原始 `.coo`；生成代码的 `#line` 只影响后续 C++ 编译器的调试/诊断位置。

## 11 实现状态与路线图

### 11.1 状态总表

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| Scope / 遮蔽 / 同域重复声明 / 局部逆序析构 | ✅ | 每个 `{}` 一个 Scope；`E2002`；解释器同构 |
| 类、构造/析构、方法、`this`、单继承、`implements` | ✅ | M0 垂直切片 |
| RAII 合成析构、`W0001/W0002/W0003`、`~this.member;` 擦除 | ✅ | 后端 `~T() = default;` + 逆序注释 |
| `enumerate`：变体值、`Color.Red`、方法、`implements`、`print`、`match` 穷尽 | ✅ | 变体 `match` 已可用；完整模式匹配 ⏳ |
| 泛型 `<N extends integer>` 单态化到 C++ 模板 | ✅ | `requires std::is_integral_v<N>` |
| `const` 编译期求值（无 `comptime` 关键字） | ✅ | fuel 2,000,000 / 递归 512 |
| Unicode/UTF-16/32/BOM 与 Unicode 标识符 | ✅ | 内部 UTF-8；`char` 32 位 |
| `T*`（含 `const T*`、`T&`） | ✅ | 后缀语法 + 后端映射 |
| C++ 后端 `build` 直出可执行文件 | ✅ | 调用宿主 `cl`/`c++` |
| 模块/包/依赖头（Module/Package/Project/Dependency） | ✅/⏳ | `Package`/`Dependency` 已接入 `coorc deps` 与工程模式；`Module`/`Project` 仍为头文件/测试 |
| AnnotationRegistry / Descriptor / Outline / Hint / Interface / Processor | ✅/🚧 | Processor 生成声明、outline、`.coox` 已接 driver；Hint 未接 CLI |
| Stream / File | ✅ | LF 归一、行/字节偏移读写 |
| 访问段 + 默认 `private` + 访问检查 | ✅ | `E2030` |
| `final` 类/方法/字段 + 同名字段隐藏 | ✅ | `E2031`/`E2032` |
| `virtual`/`override` 强制 + 协变返回 | ✅ | `E2033`/`E2035`/`E2034` |
| `super.method()` / `super.field` / `: super(args)` / `: field(expr)` | ✅ | e2e 覆盖 |
| 枚举方法 / `implements` 接口 | ✅ | 包装类 + 底层整数 |
| 基类引用/指针的虚分派 | ✅ | `examples/polymorphism.coo` |
| `@Share` 环检测 | ✅/⏳ | 类型图 + 直接自赋值发 `W0005`；运行期记录赋值边（含构造函数体/初始化列表）并在退出时 DFS 检测环；类内字段初始化/别名边仍部分缺失；`coord_e2e_share_cycle`、`coord_e2e_share_cycle_ctor` |
| `&` 借用 / `@Unique`/`@Share` 逃逸（JPA cascade） | 🚧 | `E2039` 返回/字段/容器引用与按引用闭包逃逸，`E2045` 自移动/移动后使用，`W0005` 直接 `@Share` 自环；完整 borrow/生命周期分析与跨线程检查 ⏳ |
| 元注解 `@Inherit/@Repeatable/@Target/@Meta/@Retention/@Documented` | ✅ | 校验 + `@Inherit` effective 列表 |
| Runtime/Enum 保留表 / 注解注释物化 | ✅ | `kRetainedAnnotations` / `k<Enum>Variants`；Invocation hook P4 |
| `integer` `@Length` 8/16/32/64/128 + `@Signed/@Unsigned` | ✅ | 128 位用 `coord_rt::UInt128` |
| `Result` / `Expect` | ✅ | 类型实参校验、独立类型、消费检查 |
| 注解处理器、`line`、proxy、反射五层 | ✅/🚧 | Processor 基础、`line`、proxy class/annotation/runtime、`@Invocation`、`Reflector` 已可用；完整 descriptor API/运行期反射仍推进 |
| `.coox`、多模块链接、标准库 | 🚧 | 相对 import、工程目录、`.coox` 读写、outline/hint、`@Test`、`coorc deps`、容器已可用；标准库其余 API 待做 |
| 包管理器与镜像管理器 | ✅ | 清单、回溯解析、锁、vendor、file/HTTP 注册表、SHA-256、`[trust]` 校验和/HTTPS 策略、离线、镜像故障转移、pack/publish |
| 跨平台工具链（Windows/macOS/Linux/FreeBSD） | ✅ | 宿主探测、argv spawn、UTF-8 路径、CMake preset；Linux/macOS 已用 g++ 验证 |
| 生成程序的跨平台契约（wmain/UTF-8 控制台/平台内建） | ✅ | `coord_e2e_platform` 覆盖 OS/路径/文件/环境内建 |
| 局部 `T*`/`@Unique`/`@Share` 的 `ProxyPolicy` | ✅ | `Error`/`Skip`/`Abort`/`Default` 的直接解引用、成员访问、隐式转非空、局部 Optional 解构；收窄；`coord_e2e_optional_policies` / `_error` / `_implicit` |
| `@Proxy(handler, policy)` 解构 + `@Construct`/`@Destructure` 钩子 | ✅ | `Skip`/`Abort`/`Error` 解构；静态 `@Construct` 与静态/runtime `@Destructure` 钩子；`coord_e2e_proxy_construct_destructure` |
| `ProxyHandler` 子接口（`MethodProxyHandler`、`DestructureProxyHandler`、`ConstructureProxyHandler`、`ClassProxyHandler`、`ObjectProxyHandler`、`FunctionProxyHandler`） | ✅ | 已支持 `interface X extends ProxyHandler`；实现子接口即满足 `@Proxy` |
| `@Constructure` 类型化/命名字面量 | ✅ | `examples/constructure.coo`；负例 `constructure_error`（`E2076`）；`@Proxy` 类默认支持类型化/命名构造 |
| `@Weak` C++ 映射 + weak API | ✅ | `std::weak_ptr<T>`；局部/字段/参数/返回值；`isPresent/isEmpty/lock/upgrade`；`coord_e2e_weak` |
| 默认初始化 | ✅ | 局部变量、字段、数组和对象字面量临时值生成 `{}`；`coord_e2e_default_init` |
| 整数运算检查 | ✅ | `+ - * / %`、`++/--`、`+= -= *= /= %=` 使用溢出/除零 helper，覆盖 64 位与 128 位；`<<`/`>>` 对 8/16/32/64/128 位使用范围/溢出 helper；`coord_e2e_division`、`coord_e2e_overflow`、`coord_e2e_compound`、`coord_e2e_shift`、`coord_e2e_share_cycle`、`coord_e2e_share_cycle_ctor` |
| `[]` 边界检查 | ✅ | `Array`/固定数组与 `ArrayView` 走 `checkedIndex`/`checkedIndexView`，越界终止；裸指针索引仍为 unsafe；`coord_e2e_bounds` |
| `@Destructure` 目标类方法 + ProxyHandler trap | ✅ | `coord_e2e_destructure_annotation`；逐绑定 hook |
| 代码块保留（`@Retention(Compile/Runtime)` 块） | ✅ | `examples/retention_block.coo` 与 `coord_e2e_retention_block`, `coord_e2e_radix`, `coord_e2e_radix_error`；`Invocation`/`enum` 块报 `E2044` |
| 返回值上的 `@Optional`；`@Proxy` 的 `Default(expr)` 构造/解构；runtime 构造前钩子；任意非默认构造类的 `Skip` 默认值 | 🚧 | 参数与字段已实现；返回值策略及部分构造/解构回退仍不完整 |
| `@Has/@Delete/@Keys/@Describe` trap | ⏳ | 仅声明，未展开 |
| ASan/UBSan + 解析器/编译器 fuzz | ✅ | `COORD_SANITIZE`、`COORD_FUZZ`、`linux-sanitize`；解析器 + 编译器 harness；诊断上限 256 |
| 交叉编译 `--target`/`--sysroot` | 🚧 | 设计锁定，正在实现 |
| workspace / feature / 签名 / git 依赖 | ⏳ | workspace `members` 只解析 |
| Thread 模型 | 🚧 | 内建 move-only `Thread`，由 `() -> void` 构造，支持 `join()`/`detach()`/`joinable()`/`hardwareConcurrency()`，跨线程捕获检查 `E2090`；内建不可拷贝 `Mutex`，手动 `lock()`/`unlock()`/`tryLock()`（`E2091`、`coord_e2e_mutex`）、RAII `ScopedLock`（`E2092`）、`ScopedThread`/`std::jthread` 自动 join（`coord_e2e_scoped_thread`）、seq_cst `Atomic` 的 `load`/`store`/`fetchAdd`/`fetchSub`/`exchange`/`compareExchange`（`coord_e2e_atomic`、`coord_e2e_atomic_ops`）与 `@Lock(Exclusive, mutex, [captures])` 块注解（值/可变/只读捕获、`@ReadOnly` 方法效果与写集检查，`E2094`、`coord_e2e_lock_capture`）、显式 `@Copy(Deep|Shallow)` 与 `@Unique` 链深拷贝（`E2095`、`coord_e2e_copy_deep`）；数据竞争安全、其它类型 atomic、condition variable、scoped thread 与非局部位置 ⏳ |
| lambda / `if` 表达式 / 完整模式匹配 | 🚧 | lambda 子集：函数类型目标、表达式/块体、隐式/显式捕获（`E2087` 捕获校验、`E2088` 按值只读）、按引用逃逸检查（`E2039`）、赋值/实参/返回、静态/绑定方法引用与链式调用；`@FunctionalInterface` 目标降为函数值并做泛型替换（`E2089`，`lib/standard/utility.coo` 的 `Function`/`Consumer`/`Predicate`/`Supplier`/`BiFunction`，测试 `coord_e2e_functional_interface*`）；具名函数式接口值/类实现、未绑定实例引用、函数指针 thunk、跨线程逃逸 ⏳；测试 `coord_e2e_lambda*`、`coord_e2e_method_ref*`。`if` 表达式（表达式分支）已可用（`coord_e2e_if_expression`；块分支 ⏳）。`match` 支持常量/`|`/范围/绑定/guard 与枚举、布尔、标量穷尽性（`coord_e2e_match_patterns`，负例 `_missing`/`_no_wildcard`/`_bad_pattern`）；类型模式与类解构 ⏳ |
| `@Native` / FFI | 🚧 | 无函数体的原始类型/string 函数通过生成包装调用 C 符号；`coord_e2e_native_ffi`、`coord_e2e_native_ffi_string`；class/指针/header/library ⏳ |
| `@Copy` / `@Wrap` / `@FunctionalInterface` 语义 | ⏳ | 注解已声明 |
| `Weak<T>` / 共享环检测 | ⏳ | 设计与映射已定 |
| `is` / `new` / `delete` / `unsafe` / `extern` | ⏳ | 保留字，未实现 |
| `operator` / `where` | 🚧 | 可解析，完整语义待做 |
| `T?` | ⏳ | 未解析 |

### 11.2 P1 明细

已完成：访问段解析与默认 `private`；字段/方法/构造访问检查（`E2030`）；`final` 类/方法/字段（`E2031`/`E2032`）；`virtual` + `override` 强制（`E2033`/`E2035`）；协变返回（`E2034`）；同名字段隐藏与 `final` 字段禁止隐藏；C++ 后端的访问标签、`class X final`、`virtual`/`override`/`final`、虚析构；测试 `testAccessControl`、`testInheritanceRules`、`testAccessCodegen`。

### 11.3 长程目标与执行计划（M3）

**目标**：在 descriptor 统一、包管理器、lambda 闭包切片、match/if 表达式切片、方法引用切片、函数式接口切片与线程、Mutex、ScopedLock/Atomic/@Lock 与 ScopedThread/Atomic-ops 切片落地后的 183 项基线上，完成 M3 最小闭环。

**基线（2026-11-28）**：`ctest` 注册 **183 项**，Windows 全套 **183/183 通过**；Linux 上 `coord_unit` + `coord_e2e_packages` 与新增示例已用 g++ 通过。P0–P3、descriptor 统一、collection/test、相对 import、工程目录、`.coox` 读写、outline/hint、基础 proxy/reflection/ownership、`ProxyPolicy`、`@Constructure`/`@Destructure`、代码块保留、main 参数形式，以及跨平台 driver、包管理器/镜像管理器/注册表已具备。

**最终验收**：

1. 两包依赖工作区可构建；
2. 运行期 proxy/反射可演示；
3. `@Unique/@Share` 字段/参数可用；
4. 标准库最小 API e2e 通过；
5. 每阶段 clean build + `ctest` 全绿。

**阶段计划**：

| 阶段 | 轮次 | 交付 | 验收 |
| --- | --- | --- | --- |
| A P4 运行期 | R1–R6 | 显式 `@Retention(runtime)`；方法/字段 per-object handler；析构释放；运行期成员/继承/类型反射；runtime 表与 reflector 一致性 e2e | runtime proxy 正负例；成员/继承查询 e2e |
| B Ownership/Invocation | R7–R12 | 字段/参数/返回值/初始化列表所有权；移动后 `E2045`；函数/方法多 handler；虚分派下 Invocation | 所有权 e2e；移动负例；多 handler/虚分派 e2e |
| C 模块/包 | R13–R20 | 两包构建/`coorc deps`/循环 e2e；`.coox` 读写；`--incremental`；选择性 import；别名函数与参数类型 | 两包依赖可构建；`.coox` 变更触发增量；循环报错 |
| D 标准库 v0 | R21–R28 | 标准注解由 `lib/standard/annotations.coo` 读取；显式 import、memory/io/fs/sync 最小 API、`@Test` 集成 | 标准注解不再 C++ 注入；File/Stream/Sync e2e |
| E lambda/match | R29–R33 | Java 语义 lambda；enum/match 穷尽 | lambda 捕获/调用 e2e；缺变体报错 |
| F 收尾 | R34–R36 | 整合、负例、确定性、文档、性能 | clean build；全量 ctest；文档校准 |

**阶段门槛**：每阶段结束必须 `--clean-first` 构建 + `ctest` 全绿；新特性必须含 Parser/Sema/C++ 后端或模块测试，以及至少一个负例；不把“部分实现”写成完成；跨阶段改动若破坏已通过测试，先回退再继续。

**执行规则**：每批次先补测试/规范，再改 Parser → Sema → Codegen；每项特性必须有正例、至少一个负例、C++ 产物检查与必要 e2e；文档只维护四份主文档；Meta 注解只在 C++，其余注解在标准库 Coo；生成声明必须重新进入 Sema，禁止文本拼接形成事实宏。

### 11.4 后续里程碑（M4）

M4 保留为后端与自举：LLVM 或自研后端、布局/vtable/错误 ABI/调试信息、自举。M4 不进入本目标。

### 11.5 横向风险与待决问题

| 风险 | 表现 | 缓解 |
| --- | --- | --- |
| 编译期膨胀 | 解释器慢、内存高 | fuel + 内存上限 + 内容缓存（⏳）；惰性实例化 |
| 隐藏 Result 的 ABI | 跨 C++/模块边界不稳定 | 先定义 Coo ABI，边界显式包装 |
| 注解/代理语义过强 | 变成事实上的宏 | 只允许生成新声明；禁止改写/文本拼接 |
| 反射 API 不稳定 | 用户代码易碎 | 五层分层演进；`.coox` 版本化 |
| C++ 后端泄漏 | 布局/异常语义不一致 | 生成代码只使用受限子集；E2E 可观察测试 |
| 诊断质量 | 生成代码错误难定位 | `#line` 映射 + 生成节点保留源 Span |
| `ProxyPolicy` 分歧 | `Skip`/`Abort`/`Default` 在 Optional、`@Proxy`、构造/解构间语义漂移 | 本规范锁定四策略与收窄规则；e2e 覆盖每个策略 |
| 跨平台入口差异 | argv/控制台/路径编码不一致 | 生成前导统一 wmain/main、UTF-8 控制台与 filesystem |

待决问题：① `description`/`descriptor` 的最终 API 命名与成员；处理器返回值语法（`declaration { ... }` 还是 `description[]`）。② proxy trap 的精确签名与 `default_*` 集合；生成声明的轮次上限与冲突消解。③ `Result`/`Expect` 在虚调用、接口调用与 `main` 中的具体降级；`@Expect` 的消费边界。④ `.coox` 二进制格式与版本兼容策略；workspace 递归安装与 feature 组合。⑤ 标准库命名空间与 `string` API 面；并发是否需要 `async`；registry 签名/信任链与私有源认证。⑥ 自举的最小可行子集。⑦ `ProxyPolicy(Skip)`/`Default(expr)` 在默认构造不可用时的行为、隐式 optional→非 optional 边界、runtime handler 构造钩子。

## 12 附录

### 12.1 文法摘要

以下是当前实现的子集摘要（ISO/IEC 14977 EBNF 风格）；完整文法（含规划语法）见 [docs/grammar.ebnf](docs/grammar.ebnf)。标注“规划”的产生式尚未实现，解析器会报 `E1101` 或 Sema 报 `E2012`。

```ebnf
compilation-unit = [ "module" path ";" ], { import-decl }, { top-level-decl } ;
import-decl      = "import", path, [ "as", identifier ],
                   [ ".", "{", identifier, { ",", identifier }, "}" ], ";" ;
top-level-decl   = { annotation-use }, [ "export" ],
                   ( class-decl | interface-decl | annotation-decl
                   | enum-decl | function-decl | const-decl ) ;

class-decl       = [ "final" ], "class", identifier, [ generic-params ],
                   [ "extends" type ], [ "implements" type, { ",", type } ],
                   class-body, [ ";" ] ;
class-body       = "{", { access-section | class-member }, "}" ;
access-section   = ( "public" | "private" | "protected" ), ":" ;
class-member     = { annotation-use },
                   ( field-decl | method-decl | constructor-decl | destructor-decl ) ;
field-decl       = [ "final" ], type, identifier, [ "=", expression ], ";" ;
method-decl      = { "virtual" | "override" | "final" | "static" },
                   type, identifier, [ generic-params ], "(", params, ")",
                   [ "const" ], ( block | ";" ) ;
constructor-decl = identifier, "(", params, ")",
                   [ ":", initializer, { ",", initializer } ], block ;
destructor-decl  = "~", identifier, "(", ")", block ;

enum-decl        = "enumerate", identifier, [ "implements", type, { ",", type } ],
                   "{", [ enum-variant, { ",", enum-variant }, [ "," ] ],
                   [ ";", { method-decl } ], "}", [ ";" ] ;
enum-variant     = { annotation-use }, identifier, [ "=", expression ] ;
interface-decl   = "interface", identifier, [ generic-params ], "{", { method-decl }, "}" ;
annotation-decl  = "annotation", identifier, [ generic-params ], [ "(", params, ")" ],
                   ( ";" | "{", { method-decl }, "}" ) ;
function-decl    = type, ( identifier | operator-name ), [ generic-params ],
                   "(", params, ")", [ "where", constraint, { ",", constraint } ],
                   ( block | ";" ) ;
const-decl       = "const", [ type ], identifier, "=", expression, ";" ;

generic-params   = "<", identifier, [ "extends", type ],
                   { ",", identifier, [ "extends", type ] }, ">" ;
params           = [ param, { ",", param } ] ;
param            = type, identifier, [ "=", expression ] ;
type             = [ "const" ], path, [ "<", type, { ",", type }, ">" ],
                   { "&" | "*" | "[", expression, "]" } ;              (* T?：规划 *)

block            = "{", { statement }, "}" ;
statement        = block | var-decl
                 | "if", "(", expression, ")", statement, [ "else", statement ]
                 | "while", "(", expression, ")", statement
                 | "return", [ expression ], ";" | "break", ";" | "continue", ";"
                 | "~", postfix-expression, ";"                        (* 析构声明 *)
                 | expression, ";" ;
var-decl         = [ "const" ], type, identifier, [ "=", expression ], ";" ;
expression       = assignment | logical-or | ... ;                    (* C++ 同优先级子集 *)

annotation-use   = "@", path, [ "(", arguments, ")" ] ;
(* 规划：for / in、unsafe、match 与完整 pattern（_、literal、path(...)、p | p、p if e） *)
```

### 12.2 术语表

| 术语 | 含义 |
| --- | --- |
| 根类型 | 类型宇宙的 14 个顶层类别，任何类型最终归入其一；加上 `string` 共 15 项 |
| 元类型 | `annotation`/`Description`/`Descriptor`/`Reflection`/`Reflector`/`Proxy`，只能存在于编译期（`E2009`） |
| Scope | 每个 `{}` 引入的名字可见性与生命周期边界；分 Module/Type/Function/Block |
| Scope Graph | 名字解析图：scope/declaration/reference 与 LEX/MEM/EXT/IMP/EXP/TP 边 |
| 相位 | 编译期或运行期；由语法上下文决定，无关键字 |
| `const` 初始化器 | 在 ConstEval 阶段解释执行的常量表达式 |
| description / descriptor | 声明属性的只读快照（数据）/ 处理属性描述符的处理器（行为） |
| reflection / `Reflector<T>` | 擦除式反射游标 / 类型化反射视图（成员访问静态检查） |
| proxy / trap | JS 风格行为代理 / 其单个行为钩子（`get`、`set`、`call`、`construct`、`destructure` 等） |
| Retention | 保留阶段：Compile（默认）/Runtime/Invocation；枚举用 Enum |
| 旁路表 | 保留阶段物化的只读元数据表，不进入对象布局 |
| 合成析构 | 无用户析构时由编译器生成的析构，按成员声明逆序 |
| 单态化 | 泛型按实参生成具体实例，映射到 C++ 模板 |
| 指令原语 | 注解处理器唯一可调用的编译器指令 `line(file, line)` |
| Span | `{file, begin, end}`，规范 UTF-8 字节偏移的半开区间 |
| Optional 代理 | lowering 生成的内部代理层，承载 `ProxyPolicy` 并位于用户 `@Proxy` 外层 |
| `ProxyPolicy` | 所有代理对象共享的策略：`Error`（`@Optional` 默认）/`Skip`/`Abort`/`Default(expr)`；`@Proxy` 默认 `Skip` |
| `@Constructure` | `@Target(constructor)` 标记，开启类型化/命名字面量构造；类型标签可省略 |
| `@Destructure` | 双形态注解：`ProxyHandler` trap，或带引用输出的目标类方法 |
| 代码块保留 | `@Retention(Compile)` 编译期执行并由后端省略；`@Retention(Runtime)` 正常运行 |

### 12.3 文档地图与归档说明

- 英文框架：[framework.md](framework.md)；中文框架：本文。
- 英文 QuickStart：[readme.md](readme.md)；中文 QuickStart：[readme.zh-CN.md](readme.zh-CN.md)。
- 形式文法：[docs/grammar.ebnf](docs/grammar.ebnf)。
- [docs/archive/](docs/archive/) 中的旧架构、规范、反射、编译器、路线图与实现计划**仅为历史归档**，不再更新；与本文冲突时以本文为准。`docs/archive/index.md` 也属于归档，其链接只保证在归档内部有效。
- 维护规则：语言/框架/后端/包协议变更只改四份主文档；`docs/grammar.ebnf` 随解析器同步；归档不修改。



