# 02 · 语言规范草案

> 状态：M0 草案。标注 **【M0】** 的条目已在 `coorc` 骨架中实现；其余为已拍板
> 设计、待后续里程碑实现。实现差距见 [05-roadmap.md](05-roadmap.md)。

## 1. 词法

- 源文件：Unicode 文本，扩展名 `.coo`；模块接口为 `.coox`。
  - 支持 UTF-8、UTF-16LE/BE、UTF-32LE/BE；BOM 可选，读取时剥离。
  - 内部统一转 UTF-8；`Span` 使用 UTF-8 字节偏移。
  - 非法码点替换为 U+FFFD，不中断词法分析。
- 注释：`//` 行注释、`/* ... */` 可嵌套块注释。
- 标识符：Unicode 字母/下划线开头，后续 Unicode 字母、数字、下划线；
  关键字为 ASCII，保留字不可作标识符。
- 关键字（节选）：`module import export as class enumerate interface implements
  extends annotation function void const static virtual override final operator
  where if else while for in return break continue true false this is match
  extern unsafe new delete integer decimal character boolean descriptor
  description reflection reflector proxy public private protected`.
- 字面量：整数（十进制/`0x`/`0b`/`0o`，允许 `_`）、十进制小数（含指数）、
  字符 `'a'`/`'\u{...}'`、字符串 `"..."`、`true`/`false`。
- 运算符：`+ - * / % ** = += -= *= /= %= == != < <= > >= && || ! & | ^ ~ << >>
  -> => . .. ..= :: ? @ #`。
- `this` 指当前实例；**不使用 `self`**。

## 2. Scope：每个 `{}` 就是一个作用域

Coordination 把 **Scope** 作为一等语义概念：**一个 `{}` 块就是一个 Scope**。
Scope 同时决定三件事：名字可见性、遮蔽规则、对象生命周期边界。

### 2.1 Scope 的种类

| Scope | 引入方式 | 内容 | 生命周期终点 |
| --- | --- | --- | --- |
| Module scope | `.coo` 文件顶层 | import、类型、函数、常量 | 模块编译结束 |
| Type scope | `class` / `interface` / `annotation` 的 `{}` | 字段、方法、构造/析构、嵌套类型 | 类型检查结束 |
| Function scope | 函数/方法/构造/析构的参数列表 | 参数 | 函数返回 |
| Block scope | 语句块 `{ ... }` | 局部变量、局部常量 | 块执行结束（`}`、`return`、`break`、`continue`、错误冒泡） |

- 函数体本身是一个 Block scope，嵌套在 Function scope 内；因此参数与函数体
  最外层局部变量处于不同 Scope，允许同名遮蔽。
- 控制流子语句如果没有 `{}`，不引入新的 Block scope；需要局部声明时请显式
  写 `{}`。
- 类体是 Type scope，不是 Block scope；字段不参与局部变量查找，必须经
  `this.field` 或裸字段名（在成员函数内）访问。

### 2.2 名字查找与遮蔽

1. 从最内层 Scope 向外查找；找到即停。
2. 内层可以声明与外层同名的变量（遮蔽 / shadowing）。
3. **同一 Scope 内重复声明同名变量是错误**（`E2002`）。
4. 参数在 Function scope，函数体局部变量在 Block scope，二者同名合法。
5. 字段不属于局部 Scope；成员函数内的裸字段名先查局部 Scope，再回退到
   当前类字段，最后查模块常量。
6. `this` 在 Type scope 内可见；脱离类/接口/注解上下文使用 `this` 是错误。

### 2.3 生命周期

- 进入 Block scope 时，其中的局部对象按声明顺序构造。
- 离开 Block scope 时，局部对象**按构造的逆序逐个析构**。
- 提前离开（`return`、`break`、`continue`、错误冒泡）同样触发逆序析构。
- 编译器合成的析构函数按**成员声明逆序**析构成员，最后析构基类。
- Scope 边界与 C++ 的 `{}` 一一对应；C++ 后端直接保留花括号，因此析构顺序
  由语言与后端共同保证。

### 2.4 编译期 Scope

注解处理器、`const` 初始化器的求值使用同一套 Scope 规则；解释器维护一个
Scope 栈，块退出时弹出。编译期错误（越界、除零、fuel 耗尽）同样从当前 Scope
向外传播。

## 3. 声明

### 3.1 模块与导入

```coo
module app.main;            // 可选；缺省时模块名由文件路径决定
import std.io;
import app.model.{User, Order} as m;
export class File { ... }
```

- **一个 `.coo` 文件就是一个模块**；路径即模块名。
- 默认私有；`export` 导出声明（类型、函数、常量）。
- `.coox` 是编译后的模块接口/元数据（类似 BMI），供 `import` 使用。
- 模块初始化按依赖拓扑排序；禁止循环依赖。

### 3.2 类

```coo
final class File {
public:
    string path;
    File(string p) { this.path = p; }
    ~File() {}
    int size() const { return 0; }
}
```

- 访问段：`public:` / `private:` / `protected:`，默认 `private`。
- 构造：`: super(args)` 调基类构造，`: field(expr)` 初始化成员；顺序固定为
  基类 → 成员声明序 → 构造函数体。
- 析构：`~Name()`，无参数、不可 `static`/`const`、不可失败。
- 单继承；`implements I1, I2` 实现多个接口。

### 3.2.1 枚举

```coo
@Retention(enum)
enumerate Color {
    Red,
    Green = 3,
    Blue,          // 4
}

enumerate Shape implements Drawable {
    Circle,
    Square,
}
```

- `enumerate` 是声明关键字；具体枚举类型是第 14 个根类型 `Enumeration` 的
  子类型，与 `class` 平行。
- 采用 C 风格整数后端：默认底层类型为 `int`（64 位）；首个变体为 0，后续默认
  前一个值 +1；可用常量表达式显式赋值。
- 变体名在同一枚举内唯一，显式值必须唯一；底层值必须在该枚举范围内。
- 变体访问：`Color.Red`；类型是 `Color`；支持相等/比较。
- 转换：`Color.Red as int` 取底层值；`n as Color` 做范围检查，越界 panic。
- 枚举可定义方法，不能声明实例字段/构造函数；没有 class 继承；可实现
  interface。
- 枚举类型与单个变体都可被注解（目标 `enumerate` / `enum_value`）。
- `match` 对枚举强制穷尽：必须列出全部变体，不允许 `_` 兜底。
- 运行期表示是底层整数；默认不保留变体名字表。`@Retention(enum)` 标在
  `enumerate` 上时，编译器额外生成旁路只读表 `(name, value, ordinal)`，供
  日志/序列化/枚举遍历；不改变枚举值布局或 ABI。

### 3.3 接口

```coo
interface Drawable {
    void draw() const;
    int z() const { return 0; }   // 默认方法
}
class Point extends Shape implements Drawable {
    override void draw() const { ... }
}
```

- 接口方法默认 `virtual`；可提供默认实现。
- `class` 方法默认静态分派，`virtual` 显式开启。
- 覆写必须写 `override`；父方法必须 `virtual`；`final` 方法禁止覆写。
- 多接口同名方法冲突由实现类显式覆写消歧。

### 3.4 注解

```coo
annotation Proxy {
    void a() {}
    void b() {}
}

@Proxy
class Service { ... }
```

- `annotation` 声明可带参数与处理器方法。
- 注解是**只读、只增**的：应用不会覆盖/替换/修改先前注解；不能修改目标结构
  （字段、方法、布局）或其他注解。
- 默认同一目标上同类型注解只能出现一次；需要重复时由注解声明上的
  `@Repeatable` 开启。
- 注解默认只存在于编译期；被 `@Retention` 标注的注解会物化为运行期只读
  元数据表与生成逻辑（见 3.4.2）。
- 注解是编译期元数据；处理器方法在编译期执行，接收目标的 `description`，
  只能生成新的独立声明。
- 处理器可调用唯一的内建指令原语 `line(file, line)`，为被注解的声明/块设置
  生成的 C++ `#line` 映射；不能使用其他指令，也不能输出任意指令文本。
- 内置注解：`@Result`、`@Expect`、`@Proxy`、`@Get`、`@Set`、`@Copy`、`@Wrap`。

#### 3.4.1 元注解

元注解只能由编译器内建，用户不能定义新的元注解，也不能改写其语义。它们标注
在 `annotation` 声明上，决定该注解自身的行为：

| 元注解 | 作用 |
| --- | --- |
| `@Inherit` | 注解沿 class → 子类、interface → 实现类、virtual method → override 传播 |
| `@Repeatable` | 允许同一目标重复应用同一注解；查询返回列表 |
| `@Target(class, function, method, field, variable, parameter, module, interface, annotation, enumerate, enum_value)` | 限制可应用目标；越界报错 |
| `@Meta` | 标记“这是元注解”；只能由内建元注解使用，用户不可应用 |
| `@Retention(stage)` | 保留阶段：注解上用裸写/`runtime`/`invocation`；枚举上用 `enum`（阶段值，不是声明关键字） |

`@Inherit` 的冲突规则：

- 目标上**直接声明**的同类型注解优先；继承只“补缺”，不会覆盖直接注解；
- 不同类型的直接/继承注解全部共存；
- 非 `@Repeatable` 注解从多个父类型继承到多份时，按最近基类优先；距离相同
  按声明顺序取第一份；
- 查询通过 `effective_annotations()` 得到“直接 + 继承”的只读列表，源码上的
  注解列表本身不被修改。

`@Retention` 是**保留阶段元注解**，可带一个阶段值：

| 写法 | 可标注 | 阶段 | 含义 |
| --- | --- | --- | --- |
| 不写 | — | Compile | 纯编译期，零运行期足迹 |
| `@Retention` / `@Retention(runtime)` | `annotation` | Runtime | 旁路只读元数据表 + 按需生成逻辑 |
| `@Retention(invocation)` | `annotation` | Invocation | Runtime 的全部内容 + 逐次调用的值拦截 |
| `@Retention(enum)` | `enumerate` | Enum | Runtime + 枚举变体名字/序号表 |

- 阶段递进：Compile → Runtime → Invocation；Invocation 隐含 Runtime。
- Enum 是 Runtime 的枚举专用分支：只适用于 `enumerate` 声明，保留变体表，
  不开启 invocation。
- 只有被 `@Retention` 标注的注解/枚举才影响运行期；未标注纯编译期。
- 物化产物是**旁路只读元数据表 + 生成逻辑**，不改变对象布局、字段顺序或 ABI。
- 保留注解只能标注具有稳定身份的声明（class/interface/annotation/function/
  method/field/module/enum）；局部变量/参数上的 `@Retention` 报错。
- `@Inherit` 的 effective annotations 在编译期算好后一并物化，运行期只读。
- 只提供“按声明 id 查询已保留注解/枚举变体”的窄接口，不提供任意成员枚举/
  调用的通用运行期反射。

#### 3.4.2 保留阶段

```coo
@Retention(invocation)
@Target(method)
annotation Audit;

class Service {
    @Audit
    int charge(int amount) { ... }
}
```

**Runtime 阶段**：编译器为保留注解生成旁路只读表
（`declaration id -> annotation record`）与只读访问器；`@Inherit` 已展开。

**Invocation 阶段**：在 Runtime 之上生成逐次调用的 hook：

| hook | 触发点 | 可修改的值 |
| --- | --- | --- |
| 调用前 | function/method 进入 | 参数值 |
| 调用后 | function/method 返回 | 返回值 |
| 字段读 | `get` | 读出的值 |
| 字段写 | `set` | 即将写入的值 |
| 构造后 | 构造函数返回后 | 已构造对象的字段值 |
| proxy trap | `@Proxy` 的调用点 | 参数/返回值/字段值 |

约束：

- **只能改值**：参数、返回值、字段值必须仍可赋值给原类型（允许子类型）；
- **不能改类型与结构**：签名、泛型实例、成员集合、字段顺序、vtable、ABI 全部
  不变；不能新增/删除成员或注解；
- handler 允许保存运行期状态（计数、缓存等），状态放在旁路存储，不进入目标
  对象布局；
- hook 不引入可恢复错误；失败只能 panic；hook 链按注解应用顺序执行，后者看到
  前者修改后的值；
- `@Repeatable` 的多个 handler 依次执行；`@Inherit` 的直接注解 handler 先于
  继承 handler。

Proxy 据此二选一：handler 未标 `@Retention` 时编译期展开、零开销；标
`@Retention` 时生成运行期代理包装；标 `@Retention(invocation)` 时可在 trap
中修改值。

### 3.5 函数与算子

```coo
int add<N extends integer>(const N& a, const N& b);
Vec operator+(const Vec& lhs, const Vec& rhs);
```

- 支持重载、默认参数、命名参数、闭包（`function` 类型）。
- 泛型参数可带 `extends` 上界；`where` 子句表达结构约束。
- 支持 const 泛型（数组长度等）。
- `operator` 重载：成员或全局函数，可用集合由词法运算符表限定。

### 3.6 常量

```coo
const int FIB10 = fib(10);
const answer = 42;            // 类型可推断
```

`const` 初始化器在编译期求值；没有 `comptime`/`constexpr` 关键字。

## 4. 类型系统

### 4.1 根类型

`integer`、`decimal`、`character`、`boolean`、`Enumeration`、`class`、
`annotation`、`function`、`description`、`descriptor`、`reflection`、
`reflector`、`proxy` 是根类型；`string` 是独立的内建文本类型（运行期），
`void` 是单元类型。
具体内建类型：`int`(64 位有符号)、`i8..i64`、`u8..u64`、`isize`、`usize`、
`f32`、`f64`、`char`、`bool`、`string`。

### 4.2 子类型与约束

- 名义子类型：`S <: T` 当且仅当 `S == T`、`S` 的基类链含 `T`、`S` 实现了
  接口 `T`，或 `S` 是 `T` 的联合/交集成员。
- 泛型默认不变；`N extends integer` 表示 `N` 的每个实例都必须是 `integer`
  的子类型。
- 联合 `A | B`、交集 `A & B` 仅用于类型与约束；`&` 在参数类型位置表示引用，
  在约束位置表示交集（上下文消歧）。
- 指针 `T*` 可为零地址；引用 `T&` 不可为空。`const T`、`const T&`、`const T*`
  表示只读（`const T*` 是“指向 const T 的指针”）。
- 函数类型：`(A, B) -> R`。

### 4.3 错误类型

`Result<T, E>` 是普通泛型类型；`Expect<T, E>` 是必须被消费的包装类型。
默认 `E = Error`（见 §8）。

## 5. 继承

| 规则 | 语义 |
| --- | --- |
| 基类数量 | 单继承；接口可多个 |
| 字段同名 | **隐藏**：父子各一份，按声明者 Scope 解析 |
| private 字段 | 参与布局；子类不可见，但可声明同名私有字段 |
| 方法覆写 | 父方法 `virtual`，子类 `override`，`final` 终止 |
| private 方法 | **可以被覆写并参与动态分派**，但子类不能按名字直接调用父实现 |
| `final` | 修饰类：不可被继承；修饰方法/字段：不可被覆写/隐藏 |
| 重载集 | 子类同名声明显式隐藏父类重载，可用 `using Base::f;` 引入；否则自动合并 |
| 返回类型 | 允许协变（返回子类型） |
| 布局 | 基类子对象在前，随后字段按声明顺序；private 字段同样参与 |
| 析构 | 类含 virtual 方法时析构自动 virtual |
| 显式父访问 | `super.method()`、`super.field`；构造用 `: super(args)` |

`this` 的静态类型是当前类；通过 `Base&`/`*Base` 调用 virtual 方法时按动态
类型分派。

## 6. 对象生命周期与 RAII

这是语言的硬语义，直接复用 C++ RAII：

### 6.1 构造

1. 基类子对象构造（若有）。
2. 成员按**声明顺序**构造（有成员初始化器则用其值）。
3. 执行构造函数体。

构造函数**不能失败**；资源获取失败请用返回 `Result` 的工厂函数。

### 6.2 析构

1. 执行用户析构函数体（若有）。
2. 成员按**声明逆序**逐个析构。
3. 基类子对象析构。

每个类恰好有一个析构函数：用户声明，或编译器合成。

### 6.3 合成析构

没有声明析构函数时，编译器在编译期合成：

```
~T() 的语义 =
  for member in reverse(declaration_order):
      destroy(member)
  destroy(base)
```

【M0】后端把合成析构输出为 `~T() = default;`，并附逆序成员注释；C++ 保证
同样的顺序。

### 6.4 用户析构与 `~this.member;`

用户声明析构函数时，**除基本数据类型外，必须为每个属性声明显式析构**：

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

- 成员类型是用户定义的 `class` 时，漏写 `~this.member;` 产生 **warning
  `W0001`**；`-Werror` 升级为错误。
- 基本数据类型（`integer`/`decimal`/`character`/`boolean`/`string`/指针等）
  不需要、也不应显式析构；写了产生 `W0002`。
- 同一个成员重复声明析构时，**只有最后一次声明生效**（保证恰好一次）；
  重复时给 `W0003`。
- `~this.member;` 是**析构声明**，不是第二次析构：C++ 后端把它擦除，成员仍由
  C++ 在析构体之后逆序析构，从而避免二次析构。
- 基类析构由编译器负责，不需要在用户析构体中写。

### 6.5 作用域退出

- 局部对象按构造逆序逐个析构（见 §2.3）。
- 临时对象在完整表达式结束时析构。
- 错误冒泡等价于提前 `return`：离开的每个 Scope 都逆序析构局部对象。
- 移动后的源对象仍然会被析构；析构函数必须接受 moved-from 状态。

### 6.6 拷贝与移动

- 默认移动；拷贝需要 `@Copy` 注解显式开启。
- `box<T>` 独占所有权，`shared<T>` 引用计数；无 GC。
- `new`/`delete` 仅允许在 `unsafe` 块中。

## 7. 语句与表达式

- `if` / `match` 是**表达式**；块的值是最后一个表达式。
- 完整模式匹配：常量、范围、类型、`|`、守卫、解构（`Result`/`Expect`）。
- `while` / `for` / `break` / `continue` / `return` 为语句。
- `unsafe { ... }` 开启指针运算与 `new/delete`。
- 运算符优先级与 C++ 一致；`<` 在类型上下文解析为泛型实参，在表达式上下文
  解析为小于。

## 8. 错误模型

### 8.1 隐藏的 Result

- 每个函数/方法的声明返回类型 `T` 是**成功类型**；内部有效返回类型是
  `Result<T, Error>`，该包装对开发者隐藏。
- 调用返回 `Result` 的函数时，若调用者不处理错误，错误**沿调用栈自动冒泡**；
  调用者自身的有效返回类型也带上 `Error`。
- 没有 `?`、`try`、`throw`、`catch` 关键字，也没有运行期异常。
- 错误冒泡是提前返回，Scope 退出时局部对象逆序析构。

### 8.2 `@Result` 与 `@Expect`

- 二者标注在**函数/方法声明**上，**不改变声明的返回类型**。
- `@Result`：调用点得到 `Result<T, Error>`，由调用者显式处理。
- `@Expect`：调用点得到 `Expect<T, Error>`，**必须在离开当前 Scope 前消费**；
  未消费是编译错误。
- 消费方式：`match`、`if let`、`unwrap`、`expect`、`return` 均可。
- 构造函数不能失败；需要失败语义时用工厂函数返回 `Result`。

### 8.3 `Error`

`Error` 是全局接口：`code`、`message`、`source`、`backtrace`；错误可包装下层
错误。默认错误类型即 `Error`。

### 8.4 入口

`void main()` 或 `int main()`；未处理错误由运行时打印并以非零码退出。

## 9. 编译期与元编程

- **没有 `comptime` / `constexpr` / `consteval` 关键字**。编译期上下文由语法
  位置决定：注解声明体中的处理器方法、注解实参、`const` 初始化器、泛型实参/
  约束、`.coox` 元数据构造。
- 编译期只能调用纯函数；禁止 IO、时间、随机、可变全局。需要外部输入时使用
  显式 capability 注解。
- fuel 与内存上限防止编译期膨胀；结果按内容寻址缓存，要求确定性。
- 元类型（`description`/`descriptor`/`reflection`/`reflector`/`proxy`/
  `annotation`）的值不能进入运行期代码；违反为 `E2009`。
- 生成声明只能**追加新声明**，不能改写已有声明，也不能生成 token/文本。

### 9.1 源码映射指令

`line(file: string, line: integer)` 是唯一的编译器指令原语：

- 只能出现在注解处理器中；
- 作用域绑定被注解的声明/块，离开即恢复；
- 后端据此输出 `#line`，让宿主编译器/调试器指回用户指定的文件与行；
- 不提供 `#warning`/`#error`/`#pragma`，也不能定义新指令；
- 不改变结构、声明顺序或其他注解。

## 10. 模块

- 一个 `.coo` = 一个模块；`import` 导入，`export` 控制可见性。
- `.coox` 保存公开接口与反射元数据；模块之间通过 `.coox` 做增量编译。
- 模块初始化按依赖拓扑排序；循环依赖是错误。
- `coordination.toml` 描述包、目标、依赖与锁文件（规划）。

## 11. 内存与并发

- 值语义 + 引用 + RAII；无 GC。
- `box<T>` 唯一所有权，`shared<T>` 原子引用计数。
- 标准库提供线程、通道、原子类型；语言不内建 async/await。
- 指针解引用/运算需 `unsafe`；引用不可空。

## 12. FFI

`extern "C++"` 块声明头文件、类型与符号，编译器生成 C++ 边界包装；类型映射
表见 [04-compiler.md](04-compiler.md)。C ABI 是更窄的子集。

## 13. 【M0】实现范围

| 特性 | 状态 |
| --- | --- |
| Scope / 遮蔽 / 同域重复声明 | ✅ |
| 类、构造/析构、方法、`this`、单继承、`implements` | ✅（继承的 `final/override/super` 检查待 M1） |
| 泛型 `<N extends integer>`、单态化到 C++ 模板 | ✅ |
| `const` 编译期求值 | ✅ |
| RAII 合成析构、`W0001/W0002/W0003`、`~this.member;` 擦除 | ✅ |
| C++20 降级、`build` 直出可执行文件 | ✅ |
| Result/Expect、注解处理器、proxy、反射 API | ⏳ M1/M2 |
| `enumerate` 枚举：整数后端、变体、`Color.Red`、`print` | ✅ 核心（方法/`implements`/`match` 待 M1） |
| `@Retention(runtime)` / `@Retention(invocation)` | ⏳ M2 |
| `@Retention(enum)` 变体名字/序号表 | ⏳ M2 |
| 访问段、`final`/`override`/`super` 语义 | ⏳ M1 |
| 模式匹配、数组、const 泛型、闭包 | ⏳ M1 |
| `.coox`、包管理、标准库 | ⏳ M3 |
