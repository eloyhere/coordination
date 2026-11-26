# 04 · 编译器实现架构

## 1. 目录与目标

```
libs/coord/
  include/coord/
    support.h    Span / SourceManager / DiagnosticEngine
    syntax.h     TokKind / Token / Lexer
    ast.h        AST + dumpAst
    parser.h     Parser
    sema.h       Type / Scope / Checker
    interp.h     Value / Interpreter
    codegen.h    CppEmitter
    driver.h     CompileOptions / runCompiler
  src/           对应实现
tools/coorc/     命令行入口
tests/           单元测试 + e2e.cmake
```

CMake 目标：

- `coorc_core`：静态库，包含全部编译器阶段；
- `coorc`：CLI，链接 `coorc_core`；
- `coord_tests`：单元测试；
- CTest：`coord_unit`、`coord_e2e_raii`、`coord_e2e_comptime`。

依赖方向单向：`support → syntax → ast → parser → sema → interp → codegen →
driver`。后续可把目录提升为独立静态库，接口不变。

## 2. 编译生命周期

| 阶段 | 输入 | 输出 | 关键不变量 |
| --- | --- | --- | --- |
| ① Load | 文件路径 | `SourceFile`（拥有缓冲） | `string_view` 永远指向稳定内存 |
| ② Lex | 源文本 | `Token[]` | 关键字、字面量、注释、Span 完整 |
| ③ Parse | Token 流 | `ModuleAst` | 语法错误恢复；每个节点带 Span |
| ④ Sema | AST | 类型/符号/诊断 | 类型可解析；Scope 栈平衡；RAII 检查 |
| ⑤ ConstEval | 已检查 AST | 常量表、编译期输出 | 纯函数、fuel 上限、确定性 |
| ⑥ Lower | AST + 常量表 | C++20 文本 | 只使用受限 C++ 子集 |
| ⑦ Host CC | `.cpp` | `.obj` + `.exe` | `--cxx`/`$CXX`/`cl`/`c++` 可替换 |
| ⑧ Run | 可执行文件 | 退出码 | 仅 `coorc run` |

`coorc check` 停在 ④/⑤；`--emit-cpp` 停在 ⑥；`build` 到 ⑦；`run` 到 ⑧。

## 3. 支撑层

### SourceManager

- 每个文件一个 `std::unique_ptr<SourceFile>`，地址稳定，`string_view` 安全。
- 预计算 `lineStarts`；`lineCol(Span)` 用 `upper_bound` 定位。
- `Span { file, begin, end }` 贯穿 lexer/parser/sema/diagnostics。

### DiagnosticEngine

- 诊断 = `{severity, code, message, span}`，可扩展 notes/fix-its。
- 渲染格式：`path:line:col: error[E2003]: ...` + 源码行 + `^~~~`。
- `-Werror` 把 warning 提升为 error；warning 仍然保留代码（`W0001` 等）便于抑制。

## 4. 词法

`TokKind` 用 X-macro 列表生成枚举与名字；关键字表是
`unordered_map<string_view, TokKind>`。数字支持十/十六/二/八进制、下划线、
小数与指数；字符支持 UTF-8 标量与 `\u{...}`；字符串做转义解码。

## 5. 语法

递归下降 + Pratt 表达式。类型上下文与表达式上下文分开：

- 类型：`const`、`T*`、`T&`、`T?`、`A | B`、`A & B`、`(A,B)->R`、`T[N]`、
  泛型实参；
- 参数类型中 `&` 解析为引用，约束中解析为交集；
- 表达式 `<` 是小于，类型上下文的 `<` 是泛型实参。

错误恢复：顶层同步到 `;`/`}`/下一个声明关键字；语句同步到 `;`/`}`。

## 6. 语义分析

### 类型表示

`Type` 是 intern 过的对象：`kind`、`name`、`base`、`pointee`、`decl`、
`fields`、`methods`、`interfaces`、`alternatives`、`isUnion`。

根类型在 `initializeBuiltins` 注册：`class` → `integer`/`decimal`/
`character`/`boolean`/`annotation`/`function`/`description`/`descriptor`/
`reflection`/`reflector`/`proxy`；具体类型 `int`(64 位)、`i8..u64`、`f32/f64`、
`char`、`bool`、`string`、`void`。

指针/联合/交集/`reflector<T>` 实例按名字缓存，避免重复创建。

### Scope 栈

```cpp
struct Scope {
  enum class Kind { Module, Class, Function, Block };
  Kind kind;
  std::unordered_map<std::string, Variable> variables;
};
std::vector<Scope> scopes_;
```

- `check()` 压入 Module；
- `checkClass`/`checkInterface`/`checkAnnotation` 压入 Class；
- `checkMethod`/`checkFunction`/构造/析构压入 Function；
- 每个 `{}` 由 `checkBlock` 压入 Block。
- `declareVariable` 只检查**最内层** Scope 是否重名：同域重复声明 `E2002`，
  外层同名允许遮蔽。
- 查找从内向外；字段不在 Scope 中，成员函数内先查局部再回退到当前类字段。

### 检查顺序

1. 顶层声明：创建类型/函数/常量符号，检测重名；
2. 类：基类、接口、字段、方法、构造/析构；接口实现检查；合成析构；
3. 接口/注解：方法签名与默认实现；
4. 常量：类型推断 + 初始化器检查；
5. 函数：签名 + 函数体；
6. `main` 签名检查。

### 继承检查（M1 完整）

- 基类必须是 class；`implements` 必须是 interface；
- 接口方法必须被实现，除非有默认实现；
- `virtual` / `override` / `final` 一致性；
- private 方法可覆写；`final` 类不可继承；
- 返回类型协变；
- 同名字段隐藏、重载集合合并。

### 注解与元注解检查（M1/M2）

- 每个 `annotation` 声明生成 `AnnotationInfo`：参数、`@Target` 集合、
  `@Repeatable`、`@Inherit`、`@Meta`、`@Retention(stage)`。
- 应用注解时检查目标是否在 `@Target` 内；重复应用时检查 `@Repeatable`；
  用户应用 `@Meta`、非内建元注解、在非注解声明上使用 `@Retention` 均报错。
- `@Retention(invocation)` 必须同时满足 Runtime；Invocation 的 hook 签名在
  Sema 中按“值可赋值、结构不可变”检查。
- 查询时计算 `effective_annotations()`：直接注解 → 沿基类/接口/虚方法链
  补缺；直接优先、不同类型共存、非 repeatable 只保留最近一份。
- 继承结果只存在于编译期查询视图，不回写 AST/`description`，因此不违反
  “注解不可修改其他注解/结构”。

### 枚举检查（M1）

- 变体名唯一；显式值必须是常量表达式；显式值唯一；落在底层类型范围内；
- 无实例字段/构造函数；方法可定义；可 `implements` 接口；
- 不可 `extends` class；不可被继承；
- `match` 必须覆盖全部变体，`_` 对枚举是错误；
- `@Retention(enum)` 只能出现在 `enumerate` 声明上；其他 `@Retention` 阶段
  不能出现在枚举上。

### RAII 检查

- 用户析构体中的 `~this.member;` 被记录为“已声明析构”；
- 类字段中类型是用户 class 的成员必须出现，否则 `W0001`；
- 非 class 成员被析构 → `W0002`；
- 重复声明 → `W0003`，语义上最后一次生效；
- 没有用户析构 → 合成 `~T()`，`destroyOrder` = 字段逆序。

## 7. 编译期求值

`Interpreter` 在已检查 AST 上树遍历：

- 值：`Void / Bool / Int / Decimal / Char / String`；
- Scope 栈与 Sema 同构；函数调用压入 Function Scope，函数体再压 Block；
- `return`/`break`/`continue` 用内部信号实现；异常回卷时**必须弹出 Scope**
  （否则递归调用会看到过期绑定）；
- fuel 默认 2,000,000 次表达式/语句求值；递归深度上限 512；
- 纯计算：无 IO/时间/随机/可变全局；
- 顶层 `const` 初始化器按声明顺序求值；无法求值的初始化器保留给运行期。

后续 M1 会把树遍历解释器替换为字节码 VM，接口保持
`eval(expr, env) -> Value`。

## 8. C++20 后端

### 类型映射

| Coordination | C++20 |
| --- | --- |
| `void` / `bool` | `void` / `bool` |
| `int` | `std::int64_t` |
| `i8/i16/i32/i64` | `std::int8_t` … `std::int64_t` |
| `u8/u16/u32/u64` | `std::uint8_t` … `std::uint64_t` |
| `isize/usize` | `std::ptrdiff_t` / `std::size_t` |
| `f32/f64` | `float` / `double` |
| `char` | `char32_t` |
| `string` | `std::string` |
| `Enumeration` / 具体 `enumerate` | `enum class`（底层 `std::int64_t`）或同布局的包装类 |
| `T&` / `const T&` | `T&` / `const T&` |
| `T*` | `T*` |
| 用户 `class` | `class`（同名，关键字加 `_`） |
| `reflector<T>` / `proxy` | 编译期专用，不出现 |

### 枚举

- 纯枚举（无方法、无接口）降级为
  `enum class Color : std::int64_t { Red, Green = 3, Blue };`；
- 带方法或 `implements` 的枚举生成同布局的包装类：一个底层整数字段 +
  `static constexpr` 变体常量 + 成员函数/接口 vtable；对象大小仍是一个整数；
- 变体访问 `Color.Red` 映射为 `Color::Red`；
- `match` 穷尽后生成 `switch`，每个变体一个分支，无 `default`；
- `e as int` 取底层值；`n as Color` 生成范围检查；
- `@Retention(enum)` 额外生成旁路表（见下），不改变枚举值布局。

### 类与继承

- `class C : public Base, public I1, public I2`；
- 接口生成纯虚类：`virtual Ret m(params) = 0;`，带 `virtual ~I() = default;`；
- `virtual` 方法加 `virtual`；`override` 可省略（M0 不检查）；
- 字段按声明顺序，private 也参与布局；
- 构造：`: super(args)` / `: field(expr)` 对应 C++ 初始化列表；
- 析构：用户体原样输出；合成析构输出 `~T() = default;` + 逆序注释。

### RAII 映射

- `~this.member;` **不生成代码**，只生成注释；
- 成员析构完全交给 C++：析构体执行完后，C++ 按声明逆序析构成员，再析构基类；
- 局部对象直接用 C++ 局部变量；`{}` 与 Scope 一一对应；
- 错误冒泡在 M1 映射为提前 `return`，同样触发 C++ 析构。

### 泛型

单态化到 C++ 模板：

```coo
int add<N extends integer>(const N& a, const N& b);
```

```cpp
template <typename N>
requires (std::is_integral_v<N>)
std::int64_t add(const N& a, const N& b);
```

### 保留阶段（`@Retention`）

`@Retention` 带阶段值：裸写 = `runtime`；`@Retention(invocation)` = runtime +
invocation；不写 = 纯编译期。被保留的注解在 C++ 后端产生：

1. **旁路只读表**：`coord_meta` 命名空间中的静态数组，键为稳定的
   `DeclarationId`（类型 id / 函数 id / 字段 id）；
2. 每个保留注解类型的窄访问器（`annotation_of<T>(id)` 形式），不暴露通用成员
   枚举/调用；
3. `@Inherit` 的 effective 列表在编译期展开后写入表，运行期不再走继承链；
4. 按需生成逻辑：proxy handler 带 `@Retention` 时生成运行期包装与 trap 分派；
   不带时仍在编译期展开；
5. 目标对象布局、字段顺序、vtable 不变；表位于只读段，不进入对象头。
6. `@Retention(enum)` 额外生成枚举变体表 `enum id -> [(name, value, ordinal)]`；
   枚举值仍是底层整数，布局/ABI 不变；不开启 invocation。

**Invocation 阶段**在 Runtime 之上生成值 hook：

| 触发点 | 生成代码 | 可改值 |
| --- | --- | --- |
| 调用前 | `handler.before(args...)`，参数以引用传入 | 参数 |
| 调用后 | `handler.after(result)`，结果以引用传入 | 返回值 |
| 字段读 | 访问点改写为 `handler.on_get(field)` | 读出值 |
| 字段写 | 访问点改写为 `handler.on_set(field, value)` | 待写值 |
| 构造后 | 构造函数末尾 `handler.after_construct(this)` | 字段值 |
| proxy trap | 运行期代理调用点 | 参数/返回值/字段值 |

约束：

- hook 的值类型由 Sema 检查为“可赋值给原类型”；不能改变签名、泛型实参、
  成员集合、字段顺序或 vtable；
- 同一目标有多个 handler 时按注解应用顺序串行执行，后者看到前者修改后的值；
- handler 实例的状态分配在旁路存储（静态/堆），不进入目标对象布局；
- hook 无“可恢复错误”，失败只能 panic；
- 值改写通过引用/返回槽完成，不引入新的类型或对象头；
- 未标 `@Retention(invocation)` 的注解不产生任何 hook。

局部变量/参数不能保留（无稳定 id）。`.coox` 导出公开声明的保留元数据。

### 入口

- `void main()` / `int main()` 生成 `coord_program::coord_main()`；
- 全局 `int main()` 调用它；M1 会传播返回值与未处理错误。

### 源码映射与 `#line`

注解处理器可在编译期调用唯一指令原语 `line(file, line)`：

- 后端维护一个源码映射栈；进入被注解的声明/块时压入映射，退出时弹出；
- 在该目标的生成代码前输出 `#line <line> "<file>"`，结束后恢复上一层；
- 文件名字符串需转义 `\` 与 `"`；行号必须 ≥ 1；
- 坐标诊断（`E1001` 等）仍使用原始 `.coo` Span；`#line` 只影响宿主 C++
  编译器对生成代码的诊断/调试位置；
- 同一目标多次 `line` 时最后一次生效；不同目标互不覆盖。

M0 尚未执行注解处理器，因此不产生 `#line`；接口在 M2 启用。

## 9. CLI

```
coorc check <file.coo>
coorc build <file.coo> [-o app.exe]
coorc build --emit-cpp out.cpp <file.coo>
coorc run <file.coo>
```

选项：`--cxx`、`--keep-cpp`、`--dump-tokens`、`--dump-ast`、`--print-consts`、
`-Werror`、`--version`。

`build` 默认产物：Windows `foo.exe`，POSIX `foo`；C++ 中间文件默认临时删除，
`--keep-cpp` 保留。

## 10. 诊断码

| 码 | 含义 |
| --- | --- |
| `E1001` | 词法错误（未终止字符串/注释、非法字符、数字越界） |
| `E1101` | 语法错误 |
| `E2001` | 重复声明 |
| `E2002` | 同 Scope 重复声明 / 成员重复 |
| `E2003` | 未知类型 |
| `E2004` | 未知标识符 / 非法基类 |
| `E2005` | 类型不匹配 |
| `E2006` | 构造函数名错误 |
| `E2007` | 析构函数签名错误 |
| `E2008` | 非法 `~this.member;` |
| `E2009` | 元类型进入运行期 |
| `E2010` | 接口方法未实现 |
| `E2011` | `main` 签名错误 |
| `E2012` | 尚未实现的特性 |
| `W0001` | 用户析构函数漏写成员析构 |
| `W0002` | 对非 class 成员写析构声明 |
| `W0003` | 重复析构声明（最后一次生效） |

## 11. 测试

- **单元**：词法、解析、Scope 遮蔽/重复声明、泛型参数、RAII 合成/告警、
  编译期求值、代码生成片段、接口。
- **端到端**：`coorc build` 生成可执行文件并运行：
  - RAII：`scope1 ~B ~A scope2 ~B ~A done`；
  - const：`fib(10) == 55`。
- **未来**：golden AST/诊断、conformance 套件、差分测试（C++ 后端 vs 解释器）、
  fuzzing。

## 12. 演进

| 阶段 | 变化 |
| --- | --- |
| M1 | 完整继承/访问/Result/Expect、字节码解释器、`#line`、类型化 AST 缓存 |
| M2 | 注解处理器执行、description/reflector API、proxy 展开 |
| M3 | `.coox` 序列化、多模块链接、`extern "C++"`、包管理 |
| M4 | LLVM 后端或自研后端；自举 |

当前代码刻意保持“单静态库 + 单向依赖”，使 M1 的拆分不需要改头文件边界。
