# 03 · 反射与元编程模型

> 目标：**没有宏**，但保留“读声明、生成声明、包装行为”的能力。默认全部在
> 编译期完成，默认运行期零元数据、零代理开销；显式 `@Retention` 才物化为
> 运行期只读表与生成逻辑，`@Retention(invocation)` 再追加逐次调用的值 hook。

## 1. 为什么没有宏

宏的本质是“在语法层做文本替换”，代价是：

- 错误位置指向展开后的文本，诊断困难；
- 卫生性（hygiene）需要额外规则；
- 无法用类型系统约束生成物；
- 增量编译难以确定依赖。

Coordination 用四件事替代宏：

1. **注解**：把意图声明在目标上；
2. **description**：把目标的属性变成结构化数据；
3. **descriptor / annotation processor**：在编译期读取 description 并生成新声明；
4. **proxy**：在编译期把行为包在目标外面。

生成物只能是**新声明**，不能改写既有声明，也不能拼接 token。

## 2. 五层模型

```
源码声明
   │  编译期快照
   ▼
description ──(descriptor 处理器)──► 新声明（追加到模块）
   │                    │
   │ 擦除式查询          │ 类型化查询
   ▼                    ▼
reflection          reflector<T>
   │
   │ 行为包装
   ▼
proxy（@Get/@Set/@Call/@Construct…）
```

| 层 | 定义 | 何时存在 | 典型操作 |
| --- | --- | --- | --- |
| `description` | class / interface / enumerate / annotation / function / method / field / variable 的**属性描述符**：kind、name、type、members、annotations、visibility、span、flags | 编译期 | 读取字段、遍历成员、比较、作为处理器输入 |
| `descriptor` | **处理属性描述符的处理器**：接收一个或多个 `description`，产出新声明（以 description 描述） | 编译期 | `process(target)`、`generate()`、注解处理器 |
| `reflection` | **擦除式反射游标**：动态 kind/name/member 查询，运行期不存在的编译期句柄 | 编译期 | `kind()`、`name()`、`members()`、`get("x")` |
| `reflector<T>` | **类型化反射视图**：对已知类型 `T` 的静态检查视图 | 编译期 | `reflector<File>.fields`、`.methods`、`.annotations` |
| `proxy` | **行为代理**：JS Proxy 风格的 trap 集合；默认编译期展开为零开销代码，`@Retention` 时生成运行期代理 | 默认编译期；保留模式运行期 | `get`、`set`、`has`、`delete`、`call`、`construct` |

要点：

- `description` 是**数据**，`descriptor` 是**行为**（处理器）；二者不是同义词。
- `reflection` 是动态/擦除层，`reflector<T>` 是静态/类型化层；前者适合写通用
  处理器，后者适合业务代码。
- `proxy` 不产生运行期代理对象：它在编译期把目标调用改写成“trap + 直接转发”。

## 3. 注解

### 3.1 声明

```coo
annotation Deprecated(string reason);

annotation ProxyHandler {
    @Get description get(description target, description name) { ... }
    @Set description set(description target, description name, description value) { ... }
}

annotation Serializable {
    descriptor serialize = ...;   // 处理器；按属性描述符生成新声明
}
```

- `annotation` 是根类型 `annotation` 的子类型。
- 注解可有参数（编译期值）与处理器方法；处理器方法本身在编译期执行。
- 注解可重复、可标注注解自身（元注解）；注解类型之间可以有继承关系，但
  **应用是纯追加的**：多个注解共存，后应用的注解不会覆盖、替换或修改先前的
  注解。
- 注解不能修改目标的**结构**：不能增删/重排字段或方法、不能改变布局、不能
  改写已有声明。处理器只能生成**新的独立声明**。
- 注解只存在于编译期，不能作为运行期字段/参数类型（`E2009`）。

### 3.2 应用

```coo
@Deprecated("use open2")
void open2();

@Serializable
class User {
    string name;
    int age;
}

@Proxy(ProxyHandler)
class Service {
    void run() { ... }
}
```

目标集合：`class`、`interface`、`function`、`method`、`variable`、字段、参数、
模块、`enumerate`、`enum_value`。应用顺序按源码出现顺序；重复注解按声明顺序传入。

### 3.3 元注解

元注解是“注解的注解”，只由编译器内建，用户不能定义新的元注解。它们标注在
`annotation` 声明上：

```coo
@Inherit
@Target(class)
annotation Entity;

@Entity
class Base {}
class Derived extends Base {}   // Derived 通过 @Inherit 看到 Entity
```

| 元注解 | 语义 | 违反时 |
| --- | --- | --- |
| `@Inherit` | 注解沿 class → 子类、interface → 实现类、virtual method → override 传播 | 应用于非注解声明：错误 |
| `@Repeatable` | 同一目标可重复应用同一注解；`annotations_of` 返回列表 | 未开启却重复：错误 |
| `@Target(...)` | 限制目标集合：class/interface/annotation/function/method/field/variable/parameter/module/enum/enum_value | 目标不匹配：错误 |
| `@Meta` | 标记元注解；只能用于内建元注解，用户不可应用 | 用户使用：错误 |
| `@Retention(stage)` | 注解：裸写 = runtime，`invocation` = runtime + 值 hook；枚举：`enum` 阶段值 = 保留变体表 | 目标/阶段不匹配：错误 |

**`@Inherit` 的解析规则**（重要，和“注解不能覆盖”保持一致）：

1. 先收集目标上**直接声明**的注解，按源码顺序；
2. 若注解类型带 `@Inherit`，再沿继承关系收集继承注解：
   - class：基类链 → 接口列表；
   - interface：父接口；
   - method：被覆写的 virtual 方法链；
3. **直接注解优先**：同类型直接注解已存在时，不再加入继承的那一份；
4. 不同类型全部共存；非 `@Repeatable` 类型只保留最近的一份（距离相同按声明
   顺序）；
5. `effective_annotations()` 返回“直接 + 继承”的只读视图，并标注每项的
   `is_inherited`；源码/`description` 上的直接注解列表不被修改。

继承是“缺省填充”，不是覆盖另一个注解；因此不违反“注解不能覆盖别的注解”。

### 3.4 保留阶段：Runtime / Invocation / Enum

`@Retention` 是带阶段值的元注解：

| 写法 | 可标注 | 阶段 | 物化 |
| --- | --- | --- | --- |
| 不写 | — | Compile | 无运行期足迹 |
| `@Retention` / `@Retention(runtime)` | `annotation` | Runtime | 旁路只读元数据表 + 生成逻辑 |
| `@Retention(invocation)` | `annotation` | Invocation | Runtime + 逐次调用的值 hook |
| `@Retention(enum)` | `enumerate` | Enum | Runtime + 变体名字/序号表 |

Invocation 隐含 Runtime；Enum 是 Runtime 的枚举专用分支；阶段不能回退。

```coo
@Retention(enum)
enumerate Color { Red, Green = 3, Blue }
```

```coo
@Retention(invocation)
@Target(method)
annotation Audit;

class Service {
    @Audit
    int charge(int amount) { ... }
}
```

**Runtime 物化**：

1. **旁路只读表**：`declaration id -> annotation record`，放在只读数据段；
2. 为每个保留注解生成窄访问器（按声明 id 查询），不暴露任意成员枚举/调用；
3. 按需生成逻辑：例如 proxy handler 的代理包装；
4. **不改变目标布局**：字段顺序、大小、vtable 不受影响；
5. `@Inherit` 在编译期展开成 effective 列表后写入表，运行期只读；
6. 只有稳定身份的声明可保留：class/interface/annotation/function/method/
   field/module/enum；局部变量/参数带 `@Retention` 报错；
7. 跨模块时，`.coox` 携带公开声明的保留元数据，私有声明不导出。

**Enum 物化**：`@Retention(enum)` 只写在 `enumerate` 上，生成：

- 一张旁路只读表：`enum type id -> [(name, value, ordinal)]`，按声明顺序；
- 窄访问器：按枚举类型查询变体列表/按名字查值/按值查名字；
- 枚举值本身仍是整数，布局与 ABI 不变；
- 不保留成员的注解值（那属于 Runtime 注解表）；
- 不开启 invocation，也不提供通用运行期反射。

**Invocation 物化**：在 Runtime 之上生成值 hook：

| hook | 触发 | 能改的值 |
| --- | --- | --- |
| 调用前 | function/method 进入 | 参数 |
| 调用后 | 返回 | 返回值 |
| 字段读 | `get` | 读出值 |
| 字段写 | `set` | 即将写入的值 |
| 构造后 | 构造函数返回 | 对象字段值 |
| proxy trap | 代理调用点 | 参数/返回值/字段值 |

约束：

- **只能改值**：新值必须可赋值给原类型（允许子类型）；存储的静态类型不变；
- **不能改类型与结构**：签名、泛型实参、成员集合、字段顺序、vtable、ABI 不变；
  不能增删成员或注解；
- handler 允许运行期状态（计数/缓存），状态放在旁路存储，不进入目标布局；
- hook 无“可恢复失败”，只能 panic；按注解应用顺序执行，后者看到前者改后的值；
- `@Repeatable` 的 handler 依次执行；`@Inherit` 时直接注解的 handler 先于继承
  handler。

Proxy 的三种模式：

| handler 的 `@Retention` | 行为 |
| --- | --- |
| 无 | 编译期展开 trap，零运行期开销，无代理对象 |
| `@Retention` | 运行期代理包装，trap 运行期分派；布局/ABI 不变 |
| `@Retention(invocation)` | 运行期代理 + 可修改参数/返回值/字段值的 trap |

运行期保留不是通用反射：不能枚举任意成员、不能按字符串调用、不能修改注解；
只把显式保留的数据与生成逻辑编译进产物。

## 4. description 的结构

概念上（具体 API 在 M2 定稿）：

```coo
description d = description_of(User);

d.kind;          // class / interface / enumerate / annotation / function / method / variable ...
d.name;          // "User"
d.qualifiedName; // "app.model.User"
d.type;          // 类型描述
d.members;       // description[]
d.annotations;   // description[]
d.visibility;    // public / private / protected
d.flags;         // final / virtual / override / static ...
d.span;          // 源文件、行、列、长度
```

- `description` 是不可变值，可比较、可哈希、可序列化进 `.coox`。
- `description_of(x)` 接受类型、声明或值；对运行期值只能在编译期上下文中调用。
- `description` 不携带运行期地址，不能跨进程/跨编译单元持久化后直接使用。

## 5. descriptor：处理器

处理器是**编译期函数/方法**，输入 `description`，输出新声明：

```coo
annotation Json {
    descriptor generate(description target) {
        // 读取 target.members，按属性描述符构造新声明
        return declaration {
            function toJson(this: Target) -> string { ... }
        };
    }
}
```

规则：

- 处理器只能**追加**新声明到当前模块末尾，不能替换/删除/改写已有声明。
- 处理器**不能修改被注解目标的结构**（字段、方法、布局、可见性）；要生成
  `toJson` 之类的代码，只能生成独立的函数/类，而不是往目标里塞成员。
- 处理器不能读取、修改、覆盖目标上的其他注解；它只能看到 `description`
  快照并产出新声明。
- 生成声明进入正常 Sema 检查；错误定位到处理器或目标。
- 生成物名字由编译器分配（可预测、唯一），引用通过 description 身份而非
  字符串拼接解析。
- 处理器运行顺序：目标 Sema 通过后、全模块 Lower 之前；同一目标的处理器按
  注解应用顺序执行。
- 处理器不得读取 IO/时间/随机；需要外部输入时通过显式 capability 注解声明，
  并把输入纳入缓存 key。
- 处理器有 fuel/内存上限；超限是编译错误，不是挂起。

### 5.1 指令原语：`line`

注解处理器可以调用一个**唯一的编译器指令原语**：

```coo
line(file: string, line: integer);
```

- 只在注解处理器（编译期上下文）中可用；在普通函数/方法体中调用是编译错误。
- 语义绑定到**被注解的声明/块**：处理器为该目标设置生成的 C++ `#line`
  映射；离开该目标后恢复上一层映射。
- 后端在目标生成的代码前输出 `#line <line> "<file>"`，目标结束后恢复。
  宿主编译器/调试器报告的错误与断点因此指向映射后的文件与行。
- 同一目标的处理器多次调用 `line` 时，**最后一次生效**；处理器按注解应用
  顺序执行。
- 只允许这一个原语；**不提供** `#warning`、`#error`、`#pragma`，也不能定义
  新的指令名，更不能输出任意 `#...` 文本。
- `line` 只影响源码位置/调试信息，不改变目标结构、其他注解或声明顺序。

示例（语法为 M2 草案）：

```coo
annotation SourceMap(string file, int line) {
    descriptor apply(description target) {
        line(this.file, this.line);   // 绑定到 target
        return no_declarations;
    }
}

@SourceMap("generated/parser.y", 120)
int parse(string input) { ... }
```

## 6. reflection 与 reflector

### 6.1 擦除式：`reflection`

```coo
reflection r = reflect(value);        // 编译期
switch (r.kind()) {
    case kind::class_:
        for (description m : r.members()) { ... }
}
```

适合写通用算法：序列化、ORM、测试发现、依赖注入。访问不存在的成员在编译期
报错（动态检查）。

### 6.2 类型化：`reflector<T>`

```coo
reflector<User> ru = reflector<User>();
for (description field : ru.fields) {
    print(field.name);
}
description ann = ru.annotation<Serializable>();
```

`reflector<T>` 对 `T` 的成员做静态检查：成员名拼错是编译错误，而不是运行期
失败。它适合业务代码与需要 IDE 补全的场景。

## 7. proxy：JS Proxy 风格的 trap

### 7.1 绑定

```coo
annotation LogHandler {
    @Get description get(description target, description name) {
        print("get " + name.text);
        return default_get(target, name);
    }
    @Set description set(description target, description name, description value) {
        print("set " + name.text);
        return default_set(target, name, value);
    }
}

@Proxy(LogHandler)
class Service { ... }
```

`@Proxy(Handler)` 把 Handler 的 trap 绑定到目标。`Handler` 是注解（或实现
trap 方法的类）；未实现的 trap 使用默认行为（直接转发）。

### 7.2 trap 表（对齐 JavaScript Proxy）

| JS trap | Coordination 注解 | 触发点 | 默认行为 |
| --- | --- | --- | --- |
| `get` | `@Get` | 读字段/属性 | 直接读 |
| `set` | `@Set` | 写字段/属性 | 直接写 |
| `has` | `@Has` | `in` / 成员存在性查询 | 静态成员检查 |
| `deleteProperty` | `@Delete` | 删除/失效 | 编译错误或 no-op |
| `ownKeys` | `@Keys` | 遍历成员 | 返回声明的成员 |
| `getOwnPropertyDescriptor` | `@Describe` | 取成员描述 | 返回 `description` |
| `apply` | `@Call` | 调用函数/方法 | 直接调用 |
| `construct` | `@Construct` | 构造对象 | 直接构造 |

静态语言中“属性名”在编译期已知，因此 trap 参数是 `description` 而不是字符串；
`name.text` 可取出源码名。默认所有 trap 在编译期展开，运行期没有 handler 对象、
没有额外间接层；handler 注解标 `@Retention` 时改为生成运行期代理包装，trap
在运行期分派；标 `@Retention(invocation)` 时 trap 还可以修改参数、返回值与
字段值，开销显式可见。

### 7.3 目标与展开

| 目标 | 展开方式 |
| --- | --- |
| `class` | 构造调用包 `@Construct`；成员访问包 `@Get/@Set`；方法调用包 `@Call` |
| `function` | 每个调用点包 `@Call`；函数值传递时包一层同签名 thunk |
| `method` | 方法体入口/出口包 trap；虚调用按动态类型展开 |
| `variable` | 每次读/写改写为 `get`/`set` trap 调用 |

约束：

- 代理不改变目标的类型、成员集合、布局与 ABI；`@Proxy` 是“调用点重写”，
  不是新类型，也不是结构修改。
- 虚分派与接口调用同样适用；trap 在静态类型已知的调用点展开。
- 代理解析失败（trap 签名不匹配、目标不可代理）是编译错误。
- 代理可叠加；展开顺序按注解从内到外，显式 `@Proxy` 优先。叠加不会覆盖
  其他注解，只会增加包装层。

## 8. 代码生成规则

- 生成入口只有注解处理器；**没有 `emit` 关键字**。
- 生成物只能是新声明；不能修改已有声明、不能修改目标结构、不能注入 token。
- 多个注解共存时互不覆盖：每个处理器都基于同一份 `description` 快照工作，
  产出各自的声明；不存在“后注解替换先注解”的语义。
- 生成声明可被后续处理器再次看到（按轮次直到不动点，带轮次上限）。
- 生成的名字必须可预测（便于调试），但引用解析使用 description 身份，避免
  文本拼接。
- 生成的声明参与 Sema、过载解析、可见性检查。
- `.coox` 只保存公开接口与反射元数据；处理器与生成物在导入方重新执行或从
  接口读取缓存。

## 9. 内建注解

| 注解 | 作用 |
| --- | --- |
| `@Result` | 函数/方法：调用点暴露 `Result<T, Error>`，不改变声明返回类型 |
| `@Expect` | 函数/方法：调用点得到 `Expect<T, Error>`，离开 Scope 前必须消费 |
| `@Proxy(Handler)` | 为目标安装 proxy trap |
| `@Get` / `@Set` / `@Has` / `@Delete` / `@Call` / `@Construct` | 标记 handler 的 trap 方法 |
| `@Copy` | 允许拷贝（默认移动） |
| `@Wrap` | 允许整数回绕（默认检查溢出） |

内建注解由编译器识别；用户不能重新定义同名内建注解。

## 10. 确定性与限制

- 处理器执行禁止 IO/时间/随机；capability 注解是唯一例外，且必须进入缓存 key。
- 处理器不得依赖声明顺序之外的隐式全局状态。
- 注解是只读的：不能覆盖、修改其他注解，也不能修改目标结构或已有声明。
- 编译期结果（常量、生成声明）按内容寻址缓存，确保增量构建可重复。
- 反射值不能逃逸到运行期；`.coox` 只序列化描述数据，不序列化编译期句柄。
- 默认运行期零反射：没有 RTTI、没有注解表、没有 proxy 对象；显式
  `@Retention` 只物化被保留的注解表与生成逻辑，仍不提供通用运行期反射。

## 11. 待定（M2 前必须定稿）

1. `description` 的具体成员与 API 命名；
2. 处理器返回值语法（`declaration { ... }` 还是 `description[]`）；
3. proxy trap 的精确签名与 `default_*` 内置函数集合；
4. 生成声明的轮次上限与冲突消解；
5. `.coox` 中描述数据的版本化与兼容策略。
