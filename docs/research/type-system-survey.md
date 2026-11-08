# 类型系统与 OOP/泛型设计调研——对四条已定规则的逐条评估

**范围**：Java、C#、Kotlin、Swift、Go、Rust、C++、TypeScript、Zig、D、Python 的类型系统、继承模型、泛型与模块机制。

**结论先行**：四条规则方向一致（AOT、编译期抽象、显式模块），但规则 2 有一个必须先解决的歧义——"interface 只在编译期存在"若理解为"没有运行时派发表"，interface 就无法作为运行时多态类型；若理解为"不生成运行时类型描述符"，则必须保留 vtable。建议把前者定义为 `concept`，后者定义为 `interface`，两条腿分开走。

## 1. 名义类型 vs 结构化类型

Java/C#/Swift 是名义类型：`class C implements I` 必须写出（[JLS 9.1](https://docs.oracle.com/javase/specs/jls/se21/html/jls-9.html)），类型身份由声明决定，与形状无关。Go 相反：只要方法集包含接口的全部方法就自动实现，无需声明（[Go spec](https://go.dev/ref/spec#Interface_types)）；嵌入接口时同名方法必须签名完全相同。Rust 的 trait 介于两者：`impl Trait for T` 是显式的，但"T 是否满足约束"由编译器按 impl 搜索，并受孤儿规则与一致性（coherence）约束。TypeScript 是结构化类型，只看形状，不看声明。

取舍在于耦合方向。隐式实现（Go）解耦定义方与使用方，便于跨包适配和测试替身，但"谁实现了它"无法被工具检索，删改方法可能悄悄破坏远处的接口；显式声明（Java）可检索、可生成文档、错误更早，但要求实现方提前知道接口，跨团队时形成依赖倒置。**建议**：名义 + 显式 `implements`，再用适配器或扩展 impl（Rust impl、Kotlin 扩展函数）补足便利性；不要做 Go 式全隐式，因为 AOT 与增量编译需要稳定的接口—实现图。

## 2. interface 多继承与菱形

Java 8 默认方法引入菱形：JLS 9.4.1.3 规定，若从多条路径继承到 override-equivalent 的默认方法，类必须显式覆盖，否则编译错误；可用 `I.super.m()` 调用指定父接口（[JLS 9.4.1](https://docs.oracle.com/javase/specs/jls/se21/html/jls-9.html)）。C# 的默认接口方法采用"最具体实现"规则：更具体的接口胜出；同一层出现歧义则必须显式实现（[C# spec §19](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/language-specification/interfaces)）；C# 还允许用显式接口实现为同名但不同返回类型的成员分别提供实现。Kotlin 要求必须覆盖并写 `super<Rectangle>.draw()`（[Kotlin inheritance](https://kotlinlang.org/docs/inheritance.html)）。Rust 没有菱形继承，但多个 trait 提供同名方法时，调用点必须用全限定语法 `<Form as UsernameWidget>::get(&form)` 消歧（[Rust by Example](https://doc.rust-lang.org/rust-by-example/trait/disambiguating.html)）。Swift 用 protocol composition `P & Q` 表达"同时满足"，默认实现放在 extension 里；两个 protocol 都提供同名默认实现时调用点歧义，需类型自己实现或显式限定。

**建议**：接口不持有实例状态（与 Java/C#/Swift 一致），菱形就只涉及方法表合并；默认方法冲突必须由最派生类显式重写，提供 `super<I>.m()`；禁止"按声明顺序取第一个"这类隐式规则。

## 3. class 单继承与覆盖

Java 用 `@Override`（可选注解）表达意图并在编译期校验；C# 用 `override` 扩展虚方法、用 `new` 隐藏（[MS docs](https://learn.microsoft.com/en-us/dotnet/csharp/programming-guide/classes-and-structs/knowing-when-to-use-override-and-new-keywords)）；Kotlin 默认 `final`，须 `open` 才能继承或覆盖，`override` 强制，且 override 成员默认仍 open（[Kotlin](https://kotlinlang.org/docs/inheritance.html)）；Swift 的 `override` 强制，`final` 封口。C++ 的陷阱最典型：派生类同名函数会隐藏基类全部重载，非虚函数经基类指针调用仍走基类版本；C++ Core Guidelines C.128 因此要求虚函数必须写 `virtual`/`override`/`final` 三者之一（[C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#Rh-override)）。字段隐藏更隐蔽：Java 字段按静态类型解析，C# 用 `new` 抑制警告，但字段始终不是多态的。

**建议**：`override` 强制；无 `override` 且与基类可覆盖成员同签名 → 编译错误；隐藏必须写 `hide` 且默认禁用；不允许字段隐藏。

## 4. 属性覆盖 vs 方法覆盖

属性有两种语义：存储字段（非虚，只能隐藏）与访问器（getter/setter，可虚）。C#/Kotlin 的属性是访问器，因此可 `override`；Kotlin 允许 `val` 被 `var` 覆盖（因为 val 只是 getter），反之不行（[Kotlin](https://kotlinlang.org/docs/inheritance.html)）；Java 没有属性，只有字段加方法。若把"同名属性覆盖"实现为字段覆盖，会同时踩三个坑：字段按静态类型访问（里氏替换下读到基类值）、对象布局在派生类中重复、基类构造期读到未初始化字段。协变返回（Java/C# 允许，C++ 对指针/引用允许）在值类型 ABI 下需要 thunk 或返回槽调整，不是免费的。构造期虚调用最危险：Java/C# 会调到最派生的 override，但派生字段尚未初始化；C++ 在基类构造期间只调到基类版本；Kotlin 文档明确警告基类构造器中调用 open 成员可能读到未初始化属性（[Kotlin](https://kotlinlang.org/docs/inheritance.html)）。

**建议**：属性 = 访问器，不是存储；字段是存储、不参与覆盖；构造器与字段初始化器中禁止虚调用和虚属性读取（编译期可判定）；协变返回只允许引用类型，值类型要求显式 `covariant` 并生成 thunk。

## 5. 编译期 interface/type：擦除 vs 单态化

Java 泛型擦除：编译后 `List<String>` 与 `List<Integer>` 是同一个类，类型参数被替换为 bound 或 Object，必要时插入 cast 和 bridge method（[Oracle Type Erasure](https://docs.oracle.com/javase/tutorial/java/generics/erasure.html)）。后果：不能 `new T()`、不能 `instanceof List<String>`、不能创建泛型数组；反射只能看到 raw type，序列化框架需 `TypeReference` 之类携带类型实参。TypeScript 更彻底：interface/type/泛型全部擦除，运行时只有 class、enum、namespace 留下对象；`instanceof` 只对 class 有效，`JSON.parse` 返回 `any`，校验必须依赖 zod 等运行时 schema；`import type` 用于避免把类型当值导入。C++/Rust/Zig 走单态化：每个实参组合生成一份代码，零成本、可内联，但代码膨胀、编译变慢，泛型体必须对调用方可见（C++ 放头文件，Rust 走 MIR/metadata）。Swift 介于两者：泛型默认通过 witness table 传递，编译器再对具体类型特化，同时保留运行时类型元数据。

**建议**：默认单态化（符合 AOT 与零成本），但为 `dyn interface` 保留 vtable；`type` 明确区分 alias（透明）与 `distinct`（不透明、同表示、编译期不可隐式转换）；运行时类型信息做成 opt-in。作为无 UI 的 CLI 工具链，反射需求主要来自序列化、调试器、插件和错误报告，而非 GUI 绑定，按需生成即可。

## 6. 模板元编程

C++ 模板是图灵完备的 token 替换：SFINAE 做重载选择，C++20 concepts 改善约束与报错，但"模板报错地狱"仍是工程事实，实例化深度有实现上限。Rust 用类型化泛型加 trait bound；`macro_rules!` 是 matcher/transcriber 的声明式宏，带 fragment specifier 与混合站点卫生（hygiene），过程宏处理 token stream（[Rust Reference](https://doc.rust-lang.org/reference/macros-by-example.html)）；宏展开有 recursion limit，诊断靠 span 定位。Zig 把类型当值：`comptime` 参数、`@typeInfo` 反射、`@compileError` 自定义报错、`@setEvalBranchQuota` 限制编译期执行步数（[Zig LangRef](https://ziglang.org/documentation/master/#comptime)）。D 提供字符串 mixin 与模板 mixin，`static if` 加 CTFE，能力强但字符串拼接破坏卫生、报错指向拼接后的代码（[D spec](https://dlang.org/spec/statement.html#MixinStatement)）。Swift 泛型以协议约束加 associatedtype 表达，没有 C++ 式值级模板。

**建议**：以类型化泛型为主，约束用 concept/trait 表达，不用 token 替换；元编程限制在纯 `comptime` 函数内（无 I/O、无全局可变状态、无地址依赖）；必须有求值燃料（步数、深度、内存）与确定性诊断（约束失败要打印"缺哪个约束、哪个实参"）；不提供字符串拼代码式 mixin，若要 AST 级生成，用带卫生的 quote/splice。

## 7. 无 namespace 的模块化

Go 以目录为 package，import path 定位，**禁止循环 import**，导出靠首字母大写（[Go spec](https://go.dev/ref/spec#Packages)）。Rust 以 crate 为编译单元、`mod` 组成模块树，`pub`/`pub(crate)` 控制可见性，`pub use` 重导出，crate 之间禁止循环依赖，crate 内 item 顺序无关。Python 以文件为 module、`__init__.py` 为 package，import 是运行时语句，循环 import 常表现为"部分初始化模块"的 AttributeError。ES modules 是静态图：import 提升、live binding、允许循环，但访问未初始化绑定会抛 ReferenceError，`export *` 可聚合，便于 tree-shaking（[MDN](https://developer.mozilla.org/en-US/docs/Web/JavaScript/Guide/Modules)）。C++20 modules 用 `export module`/`import`，支持 partition 与 `export import` 重导出，宏不随模块导出，编译产物是 BMI，导入顺序无关（[MS Learn](https://learn.microsoft.com/en-us/cpp/cpp/modules-cpp?view=msvc-170)）。

无 namespace 可行的前提是"模块名即命名空间"：模块名全局唯一（反向域名或项目前缀），跨模块引用必须限定或显式 import，模块可嵌套形成层级。增量编译的关键是导出接口稳定：只导出签名与可内联体，模块接口哈希决定是否重编；取消宏之后，缓存键不必包含预处理状态，这是规则 4 相对 C/C++ 的最大收益。**建议**：包内允许循环（item 级解析），跨包禁止循环（保证分层与增量构建）；`export` 逐声明显式、默认私有；提供 `export import` 重导出与别名，冲突必须显式 alias；细粒度 import 支持 `import m.{A, B}`，禁止无限定通配；包内可见性用 `internal`。

## 8. 四条规则逐条评估

| 规则 | 优点 | 风险 / 反例 | 建议细化 |
|---|---|---|---|
| 1 AOT | 启动快、可预测、易交叉编译 | 无 JIT 就无法运行时特化；反射与动态加载受限。反例：C++ 模板体必须对调用方可见，跨平台 ABI 不一致时预编译库无法复用 | 明确目标三元组与 ABI 版本；泛型单态化边界；RTTI opt-in |
| 2 interface/type 仅编译期；interface 多继承；class 单继承；同名覆盖 | 抽象零成本、布局简单、避开 C++ 多继承钻石 | 若"仅编译期"= 无 vtable，interface 不能做运行时多态；若保留 vtable，又不算"仅编译期"。反例：C# 同名不同返回类型必须显式接口实现才能共存 | 拆分 `concept`（静态约束）与 `interface`（vtable 契约）；接口无状态；override 强制；禁字段隐藏；构造期禁虚调用 |
| 3 模板元编程 | 零成本抽象、可生成专用代码、替代宏 | 编译期爆炸、报错难读、终止性不可判定。反例：C++ 约束失败常打印数十层实例化栈；D 字符串 mixin 的报错指向拼接后的代码 | comptime 纯函数加燃料上限与 concept 诊断；禁字符串拼代码；明确单态化/擦除策略 |
| 4 无 include 宏、无 namespace，只 import/export | 依赖图可静态分析、增量编译友好、无宏污染、无 ADL 意外 | 平台条件编译失去载体；命名冲突；循环依赖策略未定。反例：Go 直接拒绝循环 import，Python 循环 import 则在运行时抛 AttributeError | 语言级 `comptime if` 加 target 内建替代宏；模块名即命名空间；跨包无环；显式 export 与重导出 |

补充两点。规则 2 的"子类同名签名方法覆盖父类"要区分三种情况：(a) 父类方法是 virtual/abstract，合法覆盖且要求 `override`；(b) 父类方法是 final 或非虚，这是隐藏，必须报错或要求 `hide`；(c) 父类只有同名字段，字段不参与覆盖，应报错。规则 4 取消宏后，`#if defined(WIN32)` 这类跨平台分支必须有替代；否则跨平台 AOT 会退化为"每个平台维护一份源码"，这是四条规则组合后最现实的漏洞。

## 9. 建议的语言规则细化清单

1. **interface 定义**：`interface` 是名义类型，可多继承 interface；不得声明实例字段或存储；可含抽象方法与默认方法；默认方法不得访问 `this` 的字段（无状态）。
2. **菱形消歧**：若最派生类从多条路径继承到 override-equivalent 的默认方法，必须显式重写，否则编译错误；用 `super<I>.m()` 指定实现；禁止按声明顺序选择。
3. **class 继承**：class 单继承 class；class 可实现多个 interface；class 不继承 interface 的状态。
4. **override 强制**：覆盖必须写 `override`；与可覆盖基类成员同签名却无 `override` → 错误；`override` 成员默认仍可覆盖，`final override` 封口。
5. **隐藏显式化**：与基类非虚成员或字段同名 → 默认错误；确需隐藏写 `hide` 并对调用点告警；禁止字段隐藏。
6. **属性语义**：属性是 getter/setter 访问器，可 `override`；字段是存储，不参与覆盖；`val` 可被 `var` 覆盖，反之不行。
7. **协变返回**：引用类型允许协变返回；值类型需显式 `covariant` 并生成 thunk；协变不得改变参数类型（禁止逆变参数）。
8. **构造期安全**：构造器、字段初始化器、`init` 块中禁止虚调用与虚属性读取；违反为编译错误。
9. **type 二义性**：`type A = B` 为透明别名；`distinct type A = B` 为不透明、同表示、编译期不可隐式转换；两者均无运行时描述符。
10. **运行时类型信息**：默认不生成；`@derive(Reflect)` 为类型生成静态描述符；`dyn interface` 使用 vtable，但不含类型名字符串；跨 FFI 与序列化必须 opt-in。
11. **泛型策略**：默认单态化；`dyn` 边界用 vtable；导出泛型需同时导出可实例化接口或预实例化清单，保证跨包增量编译。
12. **comptime 约束**：comptime 函数纯、无 I/O、无全局可变状态；求值有步数/深度/内存上限，超限报错并指出位置；`@compileError` 可自定义诊断。
13. **元编程边界**：禁止字符串拼源码；AST 级生成必须带卫生（quote/splice 或 typed AST）；宏展开不得引入调用点不可见的名称。
14. **模块与包**：包 = 目录，模块 = 文件或显式 module；模块名全局唯一并参与符号修饰；包内允许循环，跨包禁止循环。
15. **import/export 粒度**：默认私有；`export` 逐声明；`import m` 限定访问，`import m.{A as B}` 非限定；`export import` 重导出，冲突须显式 alias；禁止 `import *` 进入当前作用域。
16. **条件编译**：不提供文本宏；用 `comptime if` 加内建 `target.os/arch/abi` 查询加声明级 `@cfg`；未选择分支仍须通过语法与名称解析（或明确仅做语法检查）。
17. **ABI 与版本**：模块导出需内容哈希；vtable 布局、字段布局、name mangling 纳入 ABI 版本；跨版本不兼容须显式 `@abi` 标注。

**参考来源**：JLS 8/9、C# 语言规范 §19、Kotlin inheritance、Swift Book inheritance、Go spec、Rust Reference 与 Rust by Example、C++ Core Guidelines C.128、Oracle Type Erasure、TypeScript Handbook、Zig LangRef、D Language Spec、MDN JavaScript modules、Microsoft Learn C++ modules。
