# 05 · 路线图与验收

## M0（已完成）：可运行骨架

**目标**：用真实 C++ 编译器验证语言骨架，而不是只写文档。

交付：

- `coorc` CLI：`check` / `build` / `run`；`--emit-cpp`、`--dump-tokens`、
  `--dump-ast`、`--print-consts`、`-Werror`。
- 词法/递归下降解析：类、接口、函数、泛型约束、注解、构造/析构、
  `this`、`~this.member;`。
- 语义：根类型注册、Scope 栈与遮蔽、同域重复声明、单继承、`implements`、
  泛型上界、字段/方法解析、`main` 检查。
- 注解：词法/语法层支持声明与应用；规范确定“只读、只增、不改结构”，执行
  引擎留到 M2。
- 编译期：`const` 初始化器解释执行；无 `comptime` 关键字。
- RAII：合成析构（成员逆序）、缺失析构 `W0001`、非 class 成员 `W0002`、
  重复声明 `W0003`。
- C++20 降级 + 宿主编译器链接；`build` 直接产出可执行文件。
- 测试：
  - `coord_unit`：词法/解析/泛型/Scope/RAII/求值/代码生成/接口；
  - `coord_e2e_raii`：输出 `scope1 ~B ~A scope2 ~B ~A done`；
  - `coord_e2e_comptime`：`fib(10) == 55`。

验收命令：

```bat
cmake --build out\build\coord
ctest --test-dir out\build\coord --output-on-failure
```

## M1：语言核心语义

**目标**：让“普通程序”完整可用。

1. **继承完整化**
   - `final` 类/方法/字段；
   - `virtual` + `override` 强制检查、协变返回；
   - `super.method()` / `super.field` / `: super(args)`；
   - 同名字段隐藏、private 方法可覆写、重载集合并；
   - 访问段 `public:/private:/protected:` 的检查；
   - 有 virtual 方法时自动 virtual 析构。
2. **错误模型**
   - 有效返回类型 `Result<T, Error>` 的隐藏与自动冒泡；
   - `@Result` / `@Expect` 的调用点类型与消费检查；
   - `Expect` 未消费编译错误；`Error` 接口与 backtrace；
   - 构造函数不可失败；工厂函数返回 `Result`。
3. **类型系统**
   - 数组 `T[N]`、`array<T>`、边界检查；
   - `T?` 明确不可空 / 指针为 0 的语义；
   - const 泛型、`where` 约束、泛型接口；
   - 枚举 `enumerate`：整数后端、变体、方法、`implements`、`match` 穷尽；
   - union/intersection 的完整检查与 `match` 收窄。
4. **控制流与表达式**
   - `if`/`match` 作为表达式；
   - 完整模式匹配（常量、范围、类型、守卫、解构）；
   - `for` / `in`；
   - 闭包、默认参数、命名参数。
5. **编译器**
   - 字节码解释器替换树遍历；
   - `#line` 映射与源码位置；
   - golden AST/诊断测试。

**验收**：反射无关的示例程序（排序、字符串处理、继承多态、枚举、Result 链）可
`coorc run`；枚举 `match` 漏变体必须报错；诊断覆盖率 ≥ 90%；无已知
Scope/RAII 回归。

## M2：反射、注解处理器与 proxy

**目标**：兑现“无宏”的元编程。

1. `description`：class/interface/enum/annotation/function/method/variable 的
   属性快照，可比较/哈希/序列化。
2. `descriptor`：只读处理器；输入 description，输出新声明；只能追加，
   不能改结构/其他注解。
3. `reflection`：擦除式游标；`reflector<T>`：类型化视图。
4. 注解处理器执行：目标 Sema 后、Lower 前；按注解顺序；轮次上限；
   生成声明参与 Sema。
5. proxy：`@Proxy(Handler)` + `@Get/@Set/@Has/@Delete/@Call/@Construct`；
   默认编译期展开、零开销；`@Retention` 的 handler 走运行期代理；
   `@Retention(invocation)` 的 handler 可在 trap 中改值。
6. 指令原语：`line(file, line)`；自定义注解（如 `@SourceMap`）驱动生成的
   C++ `#line` 映射；只允许该原语，绑定目标、离开恢复。
7. 元注解：`@Inherit`、`@Repeatable`、`@Target`、`@Meta`、`@Retention(stage)`；
   `effective_annotations()` 直接优先、继承补缺。
8. Retention 阶段：Compile（默认）→ Runtime（旁路表）→ Invocation（值 hook）；
   Invocation 只改参数/返回值/字段值，类型必须可赋值，签名/成员/布局/vtable
   不变；handler 可有运行期状态。
9. 确定性：fuel/内存上限、内容寻址缓存、capability 注解。
10. `@Retention(enum)`：枚举变体名字/序号旁路表，不改变枚举布局；默认整数
    后端无名字表。
11. 内建注解：`@Result`、`@Expect`、`@Copy`、`@Wrap`。

**验收**：

- `@Serializable` 生成独立 `toJson` 函数，目标类结构不变；
- `@Proxy(LogHandler)` 包装 class/function/method/variable，输出顺序正确，
  运行期无 handler 对象；
- `@SourceMap` 生成的 `#line` 能让宿主 C++ 编译错误指向映射后的文件/行；
- `@Inherit`：子类通过 `effective_annotations()` 看到父类/接口/被覆写方法
  的注解，直接注解优先；
- `@Retention`：未标注的注解零运行期足迹；`@Retention` / `@Retention(runtime)`
  生成旁路只读表 + 逻辑，对象布局/ABI 不变；proxy handler 走运行期分派；
- `@Retention(invocation)`：调用前改参数、调用后改返回值、字段 get/set 改值、
  构造后改字段值；值类型可赋值检查通过，签名/成员/布局/vtable 全部不变；
  handler 状态不进入对象布局；负例（改类型/加成员/改签名）全部报错；
- `@Retention(enum)`：变体表可查询名字/值/序号；枚举大小不变；未标注时
  无名字表；
- `@Repeatable`/`@Target`/`@Meta` 的正例与负例全部覆盖；
- 注解不能覆盖其他注解/不能改结构的负例全部报错；
- 同一输入两次编译产物字节级一致（缓存开启/关闭均成立）。

## M3：模块、工具链与标准库

1. `.coox`：公开接口 + 反射元数据的序列化、版本化、兼容检查。
2. 多模块：`import` 解析、循环依赖检测、拓扑初始化、增量编译。
3. `coordination.toml`：包、目标、依赖、锁文件；`coorc build` 工程模式。
4. `extern "C++"`：头文件、类型映射、ABI 包装；C ABI 子集。
5. 标准库 v0：`string`、`array<T>`、`box<T>`、`shared<T>`、`Result`、
   `Expect`、`Error`、文件/IO、线程/通道/原子。
6. 工具：formatter、LSP（补全/跳转/诊断）、测试框架、文档生成。

**验收**：两个包相互依赖可构建；`.coox` 变更触发正确的增量重编译；
`extern "C++"` 调用真实 C++ 库。

## M4：后端与自举

1. 评估 LLVM IR 后端；保留 C++ 后端作为“参考后端”。
2. 自研后端或 LLVM：布局、vtable、异常/错误 ABI、调试信息（DWARF/PDB）。
3. 优化：内联、常量传播、单态化裁剪、编译期缓存。
4. 自举：用 Coordination 重写前端/解释器/后端的一部分，最终 `coorc` 自编译。

**验收**：自举编译器可编译自身；性能与诊断不劣于 C++ 宿主版本。

## 横向风险

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 隐藏 Result 的 ABI | 跨模块/FFI 不稳定 | 先定 Coo ABI；边界显式包装 |
| 注解处理器图灵完备 | 编译期不可控 | fuel/内存/轮次上限；纯函数 |
| proxy 展开复杂 | 调试困难 | 生成代码保留源 Span；`--dump-generated` |
| C++ 后端语义泄漏 | 布局/异常不一致 | 限制 C++ 子集；差分测试 |
| 反射 API 过早固化 | 兼容负担 | 五层分层；`.coox` 版本化 |
| 编译期缓存失效 | 增量错误 | 内容寻址 + 依赖显式化 |

## 待决问题

1. `description` / `descriptor` 的最终 API 命名与成员。
2. 处理器返回值语法：`declaration { ... }` 还是 `description[]`。
3. `Result`/`Expect` 在虚调用、接口调用与 `main` 中的具体降级。
4. `@Expect` 的“消费”边界：是否允许存入字段/容器后延迟消费。
5. `.coox` 二进制格式与版本兼容策略。
6. `coordination.toml` 的 schema 与依赖解析规则。
7. 标准库命名空间与 `string` API 面。
8. 并发：线程/通道以外是否需要 `async`。
9. 包管理与 registry 的信任模型。
10. 自举的最小可行子集。
