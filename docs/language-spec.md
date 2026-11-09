# Coordination 语言规范草案 v0.1

> 状态：草案，供评审。本文把已经拍板的四条设计规则落成可实现的条款，并把调研中发现的歧义显式标出来（见 §8 待决问题）。
> 适用范围：编译型、无 UI、CLI 工具链、跨平台（Windows / Linux / macOS，x86-64 / arm64）。

## 0. 四条既定规则

| # | 规则 | 本文对应章节 |
|---|---|---|
| R1 | 编译型语言（AOT，无 JIT、无解释器） | §1、§4、§7 |
| R2 | `interface` 与 `type` 只在编译阶段；`interface` 可多继承 `interface`；`class` 只能单继承 `class`；子类同名签名方法、同名属性覆盖父类 | §3 |
| R3 | 支持模板元编程 | §4 |
| R4 | 不支持 `include` 宏、不支持 `namespace`，只有 `import` / `export` | §5 |

## 1. 设计原则

1. **零成本抽象**：不为语言特性引入隐藏的运行时开销；用不到的多态不生成派发表，用不到的元数据不生成描述符。
2. **编译期可判定**：能静态检查的错误不推迟到运行期；覆盖、可见性、构造期虚调用、comptime 终止性都在编译期判定。
3. **依赖图静态可分析**：没有文本宏、没有预处理，模块接口哈希即缓存键，增量编译不需要重放预处理状态。
4. **跨平台是一等公民**：没有宏之后，平台差异必须由语言内的 `comptime if` + `target.*` 内建表达，而不是每个平台维护一份源码。

## 2. 词法与语法基调

- 关键字全小写。标识符**不使用下划线**，合法形式只有三种：大写开头驼峰（`Point`、`UserProfile`）、小写开头驼峰（`findUser`、`toText`）、单个小写英文单词（`x`、`find`）。`_` 不能出现在标识符的任何位置，标准库与编译器内建名同样遵守。
- **语句以 `;` 结束**。换行不参与语法，可自由排版；解析器不做自动分号插入（避免 Go/JS 那种"换行改变语义"的坑）。
- 变量、字段与参数用 `const` / `mutable` 声明：`const` 只读（默认），`mutable` 可写；参数默认 `const`，需要修改时写 `mutable`。没有 `let` / `var` 关键字。
- **函数与方法声明不带 `fn` 关键字**：写作 `name(param: Type, ...): ReturnType`，无返回值写 `: void`；构造器 `init(...)` 不写返回类型。声明与实现使用同一套签名语法。

- 没有预处理指令、没有 `#include`、没有文本替换宏。以 `@` 开头的是**标注应用**（annotation），例如 `@reflect([]character(8))`、`@derive(Reflect)`、`@cfg(target.os == "windows")`；标注由 `annotation` 关键字声明（见 §3.5），是结构化语法节点，不是文本宏。
- 声明顺序无关：模块内可以先使用后声明，符号解析在模块级完成。

```coordination
// 一个最小模块
export class Point {
    mutable x: integer(32);
    mutable y: integer(32);

    init(x: integer(32), y: integer(32)) {
        this.x = x;
        this.y = y;
    }

    lengthSquared(): integer(32) {
        return this.x * this.x + this.y * this.y;
    }
}
```

## 3. 类型系统（R2）

### 3.0 基本数据类型

基本类型只有五种：

| 类型 | 表示 | 用途 |
|---|---|---|
| `integer(length)` | length 位有符号整数（补码） | 通用整数；length 是 comptime 常量 |
| `character(length)` | length 位字符 | 8 = UTF-8 字节，16 = UTF-16 单元，32 = Unicode 标量 |
| `timestamp` | 64 位整数 | Unix 时间（秒）；语义类型，不与 integer(64) 隐式互转 |
| `module` | 64 位无符号整数 | 模块句柄 / ID；语义类型 |
| `unknown` | 无固定布局 | 顶部类型：静态类型未知 |

条款：

1. 类型构造写作 `new integer(<module>)` / `new character(<module>)`：参数是一个 `module` 类型的编译期值，`new integer(64)` 即 64 位整数，`integer(64)` 是等价简写。`integer` 允许 8 / 16 / 32 / 64 / 128，`character` 允许 8 / 16 / 32。
2. 无符号整数写作 `new integer(<module>, unsigned)`（简写 `integer(64, unsigned)`），默认有符号。
3. `timestamp` / `module` 与同宽整数布局相同，但**不可隐式互转**；做算术前必须显式转换，避免把时间当秒数、把模块句柄当数组下标。
4. `unknown` 是顶部类型：任何值可赋给 `unknown`；`unknown` 值在算术、调用、成员访问前必须显式收窄（`as` 或模式匹配），否则编译错误；不参与隐式转换。
5. `bool` 是 `integer(1)` 的别名（0/1），`if` / `while` 的条件接受 `integer(1)`。
6. 文本是 `[]character(8)`（UTF-8 字节切片），字符串字面量的类型就是 `[]character(8)`；不设独立的字符串类型。
7. 浮点不在核心类型中（见 Q12）。
8. 复合类型（数组 `[T; N]`、切片 `[]T`、引用 `&T` / `&mut T`、裸指针 `*T` / `*const T`、函数 `(A): R`、`dyn I`）不是基本类型，按下文各节规则组合使用。
9. 字面量默认类型：整数 `integer(64)`、字符 `character(32)`、字符串 `[]character(8)`；需要其它宽度时显式标注。
### 3.1 `type`：只在编译期存在的类型定义

`type` 有两种形态，二者都不生成运行时描述符：

```coordination
type Meters = f64;              // 透明别名：与 f64 完全等价，编译期擦除
distinct type UserId = integer(64, unsigned);     // 不透明类型：表示相同，编译期不可与 integer(64, unsigned) 隐式互转
```

- 透明别名参与类型等价判断，不产生新类型。
- `distinct type` 产生新类型：布局与底层类型相同、零运行时开销，但赋值、比较、算术都需要显式转换（`UserId(x)`），避免"单位混用"类错误。
- `type` 不能有方法、字段、构造器；需要行为请用 `interface` 或 `class`。

### 3.2 `interface`：名义类型，可多继承，无实例状态

```coordination
export interface Drawable {
    draw(canvas: &Canvas): void;
}

export interface Shape : Drawable, Measurable {   // 多继承 interface
    area(): f64;

    describe(): []character(8) {                       // 默认方法
        return "shape with area " + this.area().to_text();
    }
}
```

条款：

1. `interface` 是**名义类型**：`class C : Shape` 必须显式写出，不做 Go 式隐式实现。
2. `interface` 只能继承 `interface`，可多继承；不允许继承 `class`。
3. `interface` **不得声明实例字段或存储**，只允许抽象方法、默认方法、关联类型与常量。
4. 默认方法不得读写实例字段（因为没有状态），只能调用接口自身的方法。
5. 菱形冲突：若最派生类型从多条路径继承到签名相同的默认方法，**必须显式重写**，否则编译错误；用 `super<I>.m()` 指定实现，禁止"按声明顺序取第一个"。
6. `interface` 作为运行时多态使用时写作 `dyn Shape`，通过 vtable 调用；vtable 里只有函数指针，**不含类型名字符串、不含反射描述符**（这就是"只在编译阶段"的准确含义，见 §8-Q1）。
7. `interface` 不做单态化的前提是它出现在 `dyn` 位置；泛型约束位置（`f<T: Shape>`）走单态化，不生成 vtable。

### 3.3 `class`：单继承 + 显式覆盖

```coordination
export class Circle : Shape {          // 单继承（这里继承的是 class 或实现 interface，语法一致）
    mutable radius: f64;

    init(radius: f64) { this.radius = radius; }

    override area(): f64 { return 3.14159 * this.radius * this.radius; }
    override draw(canvas: &Canvas): void { /* ... */ }
}
```

条款：

1. `class` 只能单继承 `class`；可实现任意多个 `interface`。父类与接口在 `:` 后混写，编译器按名字解析区分。
2. **覆盖必须写 `override`**。与基类可覆盖成员同签名却没有 `override` → 编译错误（避免 C++ 那种"以为覆盖了其实是隐藏"的陷阱）。
3. 与基类**非虚成员或存储字段**同名 → 编译错误；确需隐藏必须写 `hide`，并在调用点产生告警。存储字段一律不参与覆盖，也不允许字段隐藏。
4. 属性（property）是 getter/setter 访问器，**父类属性可被子类同名属性覆盖**，覆盖时写 `override`；只读属性可被可写属性覆盖，反之不行；getter 返回类型协变、setter 参数逆变（§3.7）。
5. 型变：覆盖允许**返回类型协变、参数类型逆变**（与函数子类型规则一致）；引用类型隐式允许，值类型必须显式 `covariant` / `contravariant` 并生成 thunk。详见 §3.7。
6. **循环继承禁止**：class/interface 的继承图必须是有向无环图；自继承、`A : B, B : A`、多继承环一律编译错误（E5002），在类型声明收集完成后统一检测，并给出环路径。
6. **构造期安全**：构造器、字段初始化器、`init` 块中禁止虚调用与虚属性读取（编译期判定），避免读到未初始化字段。
7. `class` 默认 `final`，需要被继承必须写 `open`；`override` 成员默认仍可覆盖，`final override` 封口。
8. 析构：确定性析构（RAII），`deinit` 在作用域结束时按逆序调用；没有 GC。

### 3.4 运行时类型信息

- 默认不生成任何 RTTI：`dyn` 调用只需要 vtable，`is` / `as` 只允许在 `dyn interface` 与 class 继承链上做**编译期可知的**向下转换。
- 运行时元信息只有一条通路：**标注 + Proxy**（§3.5、§3.6）。描述符是只读静态数据，不参与派发；未标注的类型不生成任何描述符。
- 跨 FFI 边界不传递带 Proxy 的对象，除非显式 `unwrap` 并开启对应开关。

### 3.5 `annotation`：编译期标注

```coordination
// 声明：标注名 + 编译期参数列表（类型本身也是编译期值）
export annotation reflect(value: Type, note: []character(8) = "");

// 标注类
@reflect([]character(8))
export class User {
    mutable name: []character(8);
}

// 标注函数
@reflect([]character(8), "按 id 查找用户")
export find(id: integer(64, unsigned)): User { ... }

// 标注变量
@reflect([]character(8))
export const VERSION: []character(8) = "1.0";
```

条款：

1. `annotation` 声明一个标注类型：名字 + 参数列表。参数类型必须是**编译期可求值**的（类型、字符串、整数、布尔、枚举）；不允许运行时值，不允许引用局部变量。
2. 应用写作 `@Name(args)`，紧贴被标注的声明之前，可连续写多个；声明无参数时可省略括号（`@deprecated`）。
3. 可标注：**函数、类**（含 `interface` / `concept` / `struct`）、**变量**（含字段与常量）。默认三者皆可；声明时可用 `@target(function, class, var)` 收窄，越界使用编译期报错。
4. 同一标注默认**不可重复**；需要重复时在声明处加 `repeatable`。
5. 标注**只在编译期存在**：声明、参数校验、目标检查全部在编译期完成，运行时不存在 annotation 对象。
6. 被标注的实体在运行时**包裹一层 Proxy**（见 §3.6）：Proxy 内只保存指向静态元信息描述符的指针，描述符记录标注名与编译期实参。未标注的实体不产生任何包装与开销。
7. `comptime` 可以读取标注：`annotationArgs(@reflect, decl)` 返回参数元组，未标注返回空元组；文档生成、lint、LSP 复用同一查询接口。
8. `@cfg` 是内建标注，**先于**其他标注求值：被 `@cfg` 排除的声明不参与后续检查与代码生成。
9. 未知标注、参数类型不匹配、目标不适用、重复标注、参数不是编译期常量——全部编译期报错，且错误信息指出标注名与位置。

### 3.6 Proxy：运行时包装与元信息

```coordination
@reflect([]character(8))
export class User {
    mutable name: []character(8);
}

demo(user: User): void {
    const meta = annotationOf(user);        // 运行时读取 Proxy 元信息
    if meta.has(@reflect) {
        const args = meta.args(@reflect);    // 编译期实参，运行时只读
    }
    const raw: &User = unwrap(user);         // 取出未包装的对象，零拷贝
}

@reflect([]character(8))
export find(id: integer(64, unsigned)): User { ... }

callSite(): void {
    const f = find;                          // 函数实例被 Proxy 包裹
    const u = f(42);                         // 经 call trap 转发到 find
    assert annotationOf(f).has(@reflect);
}
```

条款：

1. **生成规则**：每个被标注的声明生成 Proxy 类型 `Proxy<Decl, A...>`，含负载（变量/字段的值、类实例存储或函数指针）与指向静态 `AnnotationSet` 的指针；多个标注合并为同一个 AnnotationSet，按标注名索引。
2. **四种 trap**：`get`（读变量/成员）、`set`（写）、`call`（调用被标注函数）、`construct`（构造被标注类）。默认 trap 直接转发到负载；标注可用 `impl ProxyHooks` 覆盖 trap，静态生成、无动态派发。
3. **对源码透明**：被标注实体的静态类型不变，字段访问、调用、构造语法不变；Proxy 只是编译器插入的表示层。`unwrap(x)` 返回负载（类实例返回引用，值类型按上下文返回左值/副本）。
4. **元信息**：`AnnotationSet` 是只读静态数据 `{ 标注 id, 编译期实参（类型 id / 字符串 / 整数 / 布尔）, 目标种类 }`，**每声明一份**而非每实例一份，实例只存指针。
5. **可检查**：`annotationOf(value)` 返回 `AnnotationView`，提供 `has(@reflect)`、`args(@reflect)`、`names()`；`comptime` 侧有等价查询 `annotationArgs(@reflect, decl)`（§3.5-7），两侧语义一致。
6. **身份与等价**：Proxy 与负载不是同一个对象；比较、哈希、序列化默认经 trap 作用于负载；需要元信息时显式 `annotationOf`。
7. **派发不受影响**：Proxy 不改变 vtable 布局与 `dyn` 派发；`dyn Interface` 指向负载而不是 Proxy。
8. **ABI 边界**：Proxy 不属于稳定 ABI；传给 `extern "C"` 前必须 `unwrap`，否则编译错误。Proxy 布局参与 `.cif` 接口哈希，跨版本不兼容按 ABI 规则处理。
9. **开销**：未标注实体零开销；标注实体每次访问多一次指针解引用，trap 可内联，热路径通常被优化掉；元信息本身始终物化（这正是标注的目的），不因未使用而删除。
10. **构造与析构顺序**：被标注类先构造负载再绑定元信息；`deinit` 先解绑再析构负载。构造期仍禁止虚调用（§3.3-6）。
11. **限制**：`@cfg` 不产生 Proxy（纯编译期）；`type` / `distinct type` 不能标注；`annotation` 不能标注自身。

### 3.7 型变：协变与逆变

```coordination
interface Producer<out T> {
    get(): T;                       // T 只出现在输出位置
}

interface Consumer<in T> {
    put(value: T): void;            // T 只出现在输入位置
}

class Animal { }
class Cat : Animal { }

useVariance(p: Producer<Cat>, c: Consumer<Animal>): void {
    const p2: Producer<Animal> = p;   // 协变：Producer<Cat> <: Producer<Animal>
    const c2: Consumer<Cat> = c;      // 逆变：Consumer<Animal> <: Consumer<Cat>
}
```

条款：

1. **声明点型变**：类型参数可标注 `out`（协变）、`in`（逆变），默认不变（invariant）。`out` / `in` 只在类型参数列表中是关键字，其它位置仍可作标识符；不做使用点变型标注，避免双重声明。
2. **位置校验**（声明处检查）：`out` 参数只能出现在输出位置——返回值、只读属性、继续以 `out` 位置传出的参数；`in` 参数只能出现在输入位置——方法参数、可写属性、setter。违反即编译错误，错误信息指出违规位置。
3. **子类型规则**：`P<out T>` 满足 `P<Derived> <: P<Base>`；`C<in T>` 满足 `C<Base> <: C<Derived>`；不变参数必须完全相同（名义类型下不做结构化比较）。
4. **函数类型**：`(A) -> B` 对参数逆变、对返回协变，即 `(Base) -> Cat <: (Cat) -> Animal`；方法覆盖沿用同一规则（§3.3-5）。
5. **引用与容器**：`&T`（只读引用）协变；`&mut T` 不变；只读切片 `[]T` 协变；可变数组/容器不变，避免"协变数组写入"漏洞。
6. **`dyn` 与 vtable**：`dyn Producer<out T>` 保留声明点型变；向上转换涉及返回类型调整时由编译器生成调整 thunk（与 §3.6 的 Proxy 无关）。
7. **与单态化**：型变只是静态检查规则；单态化后每个实参组合独立生成代码，无运行时开销。泛型函数自身的类型参数（`f<T>`）不声明型变，按不变处理。
8. **禁止绕过**：`as` 不能绕过型变检查；需要时显式构造适配器或改走 `dyn`。
9. **与标注/Proxy**：标注实参中的类型按不变量处理；Proxy 布局不因型变改变。
10. **实现检查**：class 实现 `Producer<out T>` 时返回类型必须精确或更窄；实现 `Consumer<in T>` 时参数必须精确或更宽；否则编译错误。

### 3.8 运算符重载

```coordination
class Vec2 {
    mutable x: f64;
    mutable y: f64;

    operator +(other: Vec2): Vec2 {
        return Vec2 { x: this.x + other.x, y: this.y + other.y };
    }

    operator -(): Vec2 {                       // 一元负号：无参数
        return Vec2 { x: -this.x, y: -this.y };
    }

    operator ==(other: Vec2): bool {
        return this.x == other.x && this.y == other.y;
    }

    operator [](index: integer(64, unsigned)): f64 { ... }     // 下标读
}

// 二元运算符也可以写成自由函数：至少一个操作数必须是本模块定义的类型
operator *(scale: f64, v: Vec2): Vec2 {
    return Vec2 { x: v.x * scale, y: v.y * scale };
}
```

条款：

1. **声明形式**：`operator <op>(params): ReturnType`。成员形式下 `self` 隐式为左操作数，一元运算符无参数、二元运算符一个参数；自由函数形式显式写两个参数。
2. **可重载集合**：`+ - * / %`；比较 `< <= > >= == !=`；一元 `+ - ! ~`；下标 `[]` 与 `[]=`；调用 `()`；复合赋值 `+= -= *= /= %=`。
3. **不可重载**：`&&` / `||`（短路求值语义固定，不能被用户代码改变）、`?:`、`=`（赋值本身）、`.`、`is`、`as`、`@`，以及模块/导入相关运算符。
4. **至少一个操作数是用户定义类型**（class / struct / enum）；禁止重载内建类型之间的运算（如 `integer(32) + integer(32)`），也禁止重载后改变内建类型的语义。
5. **孤儿规则**：自由函数形式的运算符必须定义在**至少一个操作数所在模块**内，否则编译错误。这条保证跨包一致性，避免两个包为同一对类型给出不同实现。
6. **重载解析**：先精确匹配，再按 §3.7 的型变规则做子类型匹配；型变不引入隐式转换，转换必须显式。仍有多个候选 → 歧义错误，错误信息列出全部候选与不匹配原因。
7. **一致性**：重载 `==` 自动提供 `!=`（定义为 `!(a == b)`），显式定义 `!=` 时以显式为准；重载 `<` **不**自动推导 `>` / `<=` / `>=`，避免错误推导，需要时逐个定义。
8. **语义**：运算符就是普通静态函数调用，可内联、可单态化；`a + b` 与 `a.operator+(b)` 完全等价，不引入额外运行时调度。`dyn` 接口上的运算符按 vtable 派发。
9. **复合赋值**：未显式重载 `+=` 时，若 `a + b` 存在且 `a` 可赋值，`a += b` 自动脱糖为 `a = a + b`；显式重载优先。
10. **下标**：`a[i]` 调用 `operator [](i)`；`a[i] = v` 调用 `operator []=(i, v)`，未定义 `[]=` 时若 `[]` 返回可变引用则允许直接赋值，否则编译错误。
11. **调用运算符**：`operator ()(args)` 让实例可调用；与函数类型 `(A) -> B` 的兼容由签名决定，不自动互相转换。
12. **与 `concept` 的关系**：`concept` 中用 `operator +` 声明约束（§4.2），满足性检查按签名匹配；运算符约束不产生 vtable。
13. **与 `comptime`**：`comptime` 函数中可以使用运算符，但运算符实现必须可在编译期求值（纯函数，§4.3）。
14. **诊断**：运算符错误必须打印操作数类型、候选运算符与失败原因（参数不匹配/歧义/违反孤儿规则），不能只给一句 `no matching operator`。

### 3.9 作用域与销毁

- 局部变量离开作用域时自动调用 `deinit`，调用顺序与构造顺序相反（RAII，无 GC）。
- 编译器在**每个出口**插入析构调用：正常走到作用域末尾、`return`、`break`、`continue`、`panic` 展开路径都包含；不允许依赖运行时清理。
- `const` / `mutable` 只影响可写性，不影响销毁；参数默认在函数返回时销毁，`mutable` 参数销毁的是副本。
- **静态存储**：模块级 `static` 在模块卸载时销毁；class 的 `static` 属性在**类被卸载**（所属模块卸载）时销毁，而不是随实例销毁；同一作用域内按构造逆序销毁，跨模块按依赖逆序。
- `deinit` 不得抛出异常；析构期间不得访问已经销毁的 static 对象。
- 对象池等运行时设施可以复用内存，但对池中每个对象仍必须调用 `deinit`，不得跳过析构。
## 4. 泛型与模板元编程（R3）

### 4.1 泛型：默认单态化

```coordination
max<T: Ordered>(a: T, b: T): T {
    return if a > b { a } else { b };
}
```

- 类型参数默认**单态化**：每个实参组合生成一份代码，可内联、零成本；代价是代码膨胀与编译时间，需要工具链侧做实例化去重与缓存。
- 需要运行时多态时显式写 `dyn Interface`，走 vtable；`dyn` 不能出现在泛型实参位置以外的隐式转换里（避免"意外装箱"）。
- 约束用 `concept` 表达（见 §4.2），约束失败必须打印"缺哪个约束、哪个实参、在哪一行实例化"，禁止只给一句 `no matching function`。

### 4.2 `concept`：纯编译期约束

```coordination
concept Ordered {
    operator <(other: Self): bool;
}

concept Sized {
    const size: integer(64, unsigned);
}

copyArray<T: Sized + Copy>(src: &[T]): [T; T.size] { ... }
```

- `concept` 是**纯编译期**的约束集合，完全不产生运行时表示（与 `interface` 的区别见 §8-Q1）。
- `concept` 可以由编译器自动满足（结构化匹配），也可以显式 `impl concept for Type`；是否允许自动满足由 §8-Q3 决定，默认**允许**（便于适配既有类型）。
- `concept` 不参与 vtable，不能用于 `dyn`。

### 4.3 `comptime`：编译期求值

```coordination
comptime factorial(n: integer(64, unsigned)): integer(64, unsigned) {
    return if n <= 1 { 1 } else { n * factorial(n - 1) };
}

const N: integer(64, unsigned) = factorial(10);   // 编译期求值

stackBuffer(): void {
    mutable buf: [integer(8, unsigned); factorial(6)];  // 编译期确定大小
}
```

条款：

1. `comptime` 函数必须是**纯函数**：无 I/O、无全局可变状态、无地址依赖、无未定义行为。
2. 求值必须有**燃料上限**（步数、递归深度、内存），超限报错并给出调用栈；默认上限由编译器配置决定（建议步数 1e7、深度 512、内存 64 MiB）。
3. 支持 `comptime if`、`comptime for`、`comptime` 块；`target.os` / `target.arch` / `target.abi` / `target.pointerWidth` 是编译期内建常量，用来替代 `#ifdef`（见 §5.5）。
4. 不提供字符串拼源码式 mixin；需要 AST 级生成时用带卫生（hygiene）的 quote/splice，生成的代码必须能追溯到生成点。
5. `@compileError("...")` 可在 comptime 分支中产生自定义诊断。

## 5. 模块系统（R4）

### 5.1 没有 namespace：模块名即命名空间

- 一个文件是一个**模块**，也可以显式声明 `module foo.bar;`；模块名全局唯一（推荐反向域名或项目前缀），并参与符号修饰（mangling）。
- 目录是**包**（package），包可以包含多个模块；包之间通过 `import` 建立依赖。
- 没有 `namespace` 关键字，也没有嵌套命名空间；层级由模块名的点分路径表达。

### 5.2 import / export

```coordination
// math/vector.cn
export dot(a: Vec3, b: Vec3): f64 { ... }
export class Vec3 { ... }
helper(): f64 { ... }          // 默认私有，不导出

// app.cn
import math.vector;               // 限定访问：vector.dot(...)
import math.vector.{Vec3, dot};   // 非限定导入
import math.vector as mv;         // 别名
export import math.vector;        // 重导出
```

条款：

1. **默认私有**，`export` 逐声明标注；包内可见性用 `internal`（仅同包可见）。
2. `import m` 只引入模块名，访问需限定；`import m.{A, B as C}` 引入非限定名；禁止 `import *` 直接灌入当前作用域（冲突必须显式 alias）。
3. `export import m` 重导出；重导出链上的名字冲突必须显式 alias，否则编译错误。
4. 循环 import 由编译器检测并报 **E5001**：默认包内、跨包都禁止（增量编译与并行构建依赖无环图）。确需包内互引时，只能通过显式配置放开，并单独评审。
5. 模块接口以"导出声明 + 可内联函数体"计算内容哈希，作为增量编译缓存键；实现体变化不影响下游重编（除非该模块开启全量内联）。

### 5.3 没有 include 宏之后的条件编译

```coordination
@cfg(target.os == "windows")
pathSeparator(): character(8) { return '\\'; }

@cfg(target.os != "windows")
pathSeparator(): character(8) { return '/'; }

export allocate(size: integer(64, unsigned)): *integer(8, unsigned) {
    if comptime target.os == "windows" {
        return heapAllocWin(size);
    } else {
        return heapAllocPosix(size);
    }
}
```

- 没有文本宏，条件编译只有两条路：声明级 `@cfg(...)` 与表达式级 `comptime if`。
- 未选中的分支仍须通过语法检查与名称解析（只做类型检查可配置），避免"另一个平台才能编译"的代码腐烂。
- `@cfg` 只接受编译期内建量的布尔表达式，不接受任意代码。

## 6. 与既有代码的关系

当前仓库中的 `structure/`（节点、树、LRU、跳表）是语言**运行时/编译器的内部基础设施**，用 C++20 实现、通过 `coordination` 头文件库分发；`syntax/` 是将来语言前端（词法/语法/语义）的 C++ 侧原型。本文档描述的是**被实现的语言**（Coordination），不是承载它的 C++ 实现语言。

## 7. 一致性检查表

| 规则 | 条款 | 状态 |
|---|---|---|
| R1 AOT | §1、§4.1、§3.3-8（确定性析构、无 GC） | 已覆盖 |
| R2 interface/type 仅编译期 | §3.1、§3.2-6、§3.4 | 需确认 Q1 |
| R2 interface 多继承 | §3.2-2、§3.2-5 | 已覆盖 |
| R2 class 单继承 | §3.3-1 | 已覆盖 |
| R2 同名覆盖 | §3.3-2~6 | 已覆盖 |
| R3 模板元编程 | §4 | 已覆盖 |
| R4 无 include/namespace，只有 import/export | §5 | 已覆盖 |
| 扩展：annotation | §3.5 | 已覆盖 |
| 扩展：Proxy 运行时包装 | §3.6 | 已覆盖 |
| 扩展：型变（协变/逆变） | §3.7 | 已覆盖 |
| 扩展：运算符重载 | §3.8 | 已覆盖 |
| 扩展：基本类型 | §3.0 | 已覆盖 |

## 8. 待决问题（需要拍板）

**Q1（最关键）**："interface 只在编译阶段"指的是哪一种？
- A. 不生成 vtable → interface 不能做运行时多态，只能当编译期约束用（那它和 concept 重复）；
- B. 不生成运行时类型描述符/RTTI，但保留 vtable 做派发。

本文按 **B** 写（§3.2-6），并建议把 A 的语义单独命名为 `concept`（§4.2）。若选 A，则 `dyn` 语法、`@derive(Reflect)`、跨 FFI 多态三处都要删掉。

**Q2**：`type` 是否允许 `distinct type`？默认允许（§3.1），若只要别名可删掉该形态。

**Q3**：`concept` 是否允许编译器自动满足（结构化匹配）？默认允许；若要求显式 `impl`，泛型适配既有类型会啰嗦，但依赖图更清晰。

**Q4**：`comptime` 燃料默认值与是否允许用户调高？默认步数 1e7、深度 512、内存 64 MiB，可用编译选项调高但不可关闭。

**Q5**：循环 import 是否一律禁止？默认禁止（E5001），增量编译与并行构建最简单；若未来允许包内循环，需要显式配置与单独评审。

**Q6**：`class` 默认 `final` 还是默认 `open`？本文取 Kotlin 风格默认 `final`（§3.3-7），若希望继承更自由可改为默认 `open`。

**Q7**：是否允许构建配置擦除 Proxy 元信息（`--erase-annotations`）以换取零开销？默认**不允许**——标注的语义包含"运行时可通过 Proxy 检查"；若允许，需要定义擦除后的类型等价、`.cif` 哈希与跨包一致性规则。

**Q8**：是否引入 `<=>`（三路比较）自动派生 `< <= > >=`？默认不引入，保持逐个显式定义；若引入，需要先定 `Ordering` 类型与派生规则（只对全序类型生效）。

**Q12**：浮点怎么处理？默认 v1 不提供核心浮点类型（避免 IEEE 语义与 fast-math 争议），需要时由标准库以 `integer(64)` 位模式 + 运算函数提供；备选是核心增加 `float(length)`（32/64）。

**Q13**：无符号整数的写法选 `integer(64, unsigned)` 还是独立的 `uinteger(64)`？默认前者（一个类型、一个 comptime 参数）。

**Q14**：`bool` 保留为独立类型，还是就按本文用 `integer(1)` 别名？默认别名，减少基本类型数量。