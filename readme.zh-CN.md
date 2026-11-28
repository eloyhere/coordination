# Coordination 快速上手

> 本文件是 Coordination 的 QuickStart、CLI、工具链与包管理器使用说明；语言与框架的权威定义见 [framework.zh-CN.md](framework.zh-CN.md)。

Coordination 是面向系统编程的静态类型语言：**没有宏**，编译期元编程由注解、描述符、反射与 proxy 驱动；对象生命周期复用 C++ RAII，当前后端输出 C++20。

- 源文件 `.coo`；模块接口 `.coox`；编译器 `coorc`。
- 包清单 `coordination.toml` / `coord.pkg`；锁文件 `coord.lock`。
- 当前基线：**Windows 与 Linux 均为 217/217 项 CTest 通过**（两边注册同一套 217 项）；macOS 注册同一套测试，但本环境未运行。
- 语言与工具链仍在 M3 收尾；规范以 [framework.zh-CN.md](framework.zh-CN.md) / [framework.md](framework.md) 为准，本文只负责“怎么用”。

## 1 30 秒跑起来

前置：CMake 3.20+、Ninja、C++20 宿主编译器：

- Windows：Visual Studio 2022（MSVC 14.3x），在 **x64 Native Tools Command Prompt for VS 2022** 中执行；
- Linux/macOS：GCC 10+ 或 Clang 12+（`c++` 在 `PATH` 中）。

Windows：

```bat
cmake -S . -B out\build\coord -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out\build\coord --clean-first
ctest --test-dir out\build\coord --output-on-failure

set COORC=out\build\coord\tools\coorc\coorc.exe
%COORC% run examples\hello.coo
```

Linux/macOS：

```bash
cmake --preset linux-release     # 或 macos-release / linux-debug
cmake --build out/build/linux-release
ctest --test-dir out/build/linux-release --output-on-failure

./out/build/linux-release/tools/coorc/coorc run examples/hello.coo
```

输出：

```text
hello, coordination
0
```

创建一个包并构建：

```bat
%COORC% init my-app --name my_app
%COORC% build my-app -o out\my_app.exe
```

> **修改头文件后必须清理重建**：MSVC 的 `showIncludes` 输出为中文，Ninja 无法可靠解析头文件依赖。请使用 `--clean-first` 或删除 `out\build\coord`。

## 2 第一个程序与主函数

```coo
integer main() {
    print("hello");
    return 0;
}
```

主函数**只能**返回 `integer`，不存在 `int`、`void main` 或隐式返回。支持三种签名：

```coo
integer main() { return 0; }

integer main(Array<string> args) {
    print(args.length());
    return 0;
}

integer main(integer argc, Array<string> args) {
    print(argc);
    return 0;
}
```

- `void main()`、`fix main()`、任意其他参数形式报 `E2052`；
- `@Result<T>` 只改变错误传播，不改变“必须返回 `integer`”；
- 生成的 C++ 入口会把结果转成进程退出码。

### 2.1 数值表示注解

`@Length`、`@Radix`、`@Signed`、`@Unsigned`、`@Power` 是通用数值表示注解，不是 `fix` 专属：

| 注解 | 含义 |
| --- | --- |
| `@Length(N)` | 存储宽度：`integer` 8/16/32/64/128，`float` 32/64，`fix` 8/16/32/64/128 |
| `@Radix(R)` | `integer`/`float` 的字面量/打印进制；`fix` 的 scale 基数 |
| `@Signed` / `@Unsigned` | 数值符号 |
| `@Power(P)` | `fix` 的 scale 指数；`integer`/`float` + `@Power` 提升为 `fix` |

规则与转换细节见 [framework.zh-CN.md](framework.zh-CN.md) §2.2.1。

## 3 构建与测试

```bat
cmake -S . -B out\build\coord -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out\build\coord --clean-first
ctest --test-dir out\build\coord --output-on-failure
```

Linux/macOS preset：

```bash
cmake --preset linux-release
cmake --build out/build/linux-release
ctest --test-dir out/build/linux-release --output-on-failure
```

各 OS 测试矩阵：

| 操作系统 | 注册 CTest | 通过 | 说明 |
| --- | --- | --- | --- |
| Windows x64（MSVC 19.52 / VS 2022） | 217 | 217 | `out\build\coord-tests` clean build |
| Linux（WSL Ubuntu 26.04，g++ 15.2） | 217 | 217 | 全量，`ctest -j 8` |
| macOS | 预期 217 | 未运行 | 同一注册；本环境无 macOS |

目前没有 Windows 专属或 POSIX 专属 e2e。宿主差异（可执行后缀、CRLF 输出、`file://` 路径）由脚本归一化吸收而不是跳过测试；将来若有平台专属测试，必须用带注释的 `if(WIN32)`/`if(UNIX)` 单独包裹。

- Windows 使用 `windows-base` 派生 preset（Ninja + cl）；Linux/macOS 使用 `linux-release`/`macos-release`/`linux-debug`。
- `ctest` 在所有平台注册 **217 项**（1 个单元测试 + 216 个 e2e），注册不再使用一揽子编译器 guard。`coord_e2e_packages` 不依赖宿主编译器与网络，`coord_e2e_packages_build` 与其它 e2e 需要可用的宿主 C++ 工具链。
- Sanitizer：`cmake -S . -B out/build/asan -G Ninja -DCOORD_SANITIZE=ON` 启用 ASan + UBSan；提供 `linux-sanitize` 预设。若不关心泄漏检查，运行测试时设 `ASAN_OPTIONS=detect_leaks=0`。
- 解析器/编译器 fuzz：用 Clang 加 `-DCOORD_FUZZ=ON` 构建，见 [tests/fuzz/README.md](tests/fuzz/README.md)。解析器 fuzzer 已发现诊断无限增长问题；编译器 fuzzer 覆盖 Sema/emitter，要求 `Checker` 的生命周期长于 emitter。
- 修改头文件后必须 `--clean-first`；生成 C++ 可用 `coorc --emit-cpp out\x.cpp file.coo` 保留检查。

## 4 CLI

### 4.1 编译、运行与检查

```bat
coorc check examples\add.coo
coorc run examples\hello.coo
coorc build examples\project -o out\project.exe
coorc build examples\hello.coo -o out\hello.exe --incremental
coorc check examples\hello.coo --emit-coox out\hello.coox
coorc --emit-cpp out\hello.cpp examples\hello.coo
```

| 命令 / 选项 | 作用 |
| --- | --- |
| `check <file-or-dir>` | 加载、词法、语法、注解处理器、Sema、ConstEval；不生成可执行文件 |
| `build <file-or-dir>` | 到 C++ lowering + 宿主编译/链接；`-o` 指定产物 |
| `run <file>` | 构建并执行；交叉编译目标不能 `run` |
| `--emit-cpp <file>` | 输出生成的 C++（`-` 表示 stdout） |
| `--emit-coox <file>` | 输出声明级模块接口摘要 |
| `--incremental` | 基于 `.coo`、根清单与 `.coox` stamp 跳过未变更构建 |
| `--keep-cpp` | 保留中间 C++ 文件 |
| `--cxx <path>` | 指定宿主 C++ 编译器 |
| `--cxx-flavor msvc\|gnu` | 强制编译器参数风格 |
| `--target <triple>` | 交叉编译目标（clang 传 `--target`） |
| `--sysroot <path>` | 交叉编译 sysroot |
| `--dump-tokens` / `--dump-ast` / `--print-consts` | 调试输出；`--print-consts` 同时显示 `@Retention(Compile)` 块的编译期输出 |
| `-Werror` | warning 升级为 error |
| `--verbose` | 打印宿主命令行 |
| `--version` | 版本 |

宿主编译器查找顺序：`--cxx` → `COORD_CXX` → `CXX` → 平台候选
（Windows：`cl`/`clang++`/`g++`；其它：`c++`/`clang++`/`g++`）。

### 4.2 测试

```bat
coorc test examples\test_sample.coo
coorc test examples\test_sample.coo --filter addition
```

`@Test` 标注零参、返回 `void` 的函数；`coorc test` 编译并**串行**运行当前文件收集到的测试，失败非零退出并打印文件/行。断言 API：`assert`、`assertEq`、`assertTrue`、`assertFalse`。

### 4.3 outline、deps 与接口

```bat
coorc outline examples\hello.coo
coorc deps tests\fixtures\two_packages\app
coorc check examples\hello.coo --emit-coox out\hello.coox
```

- `outline` 输出模块/类/字段/方法/构造/析构/枚举变体，含签名、访问、注解、proxy 标记；tree 由 descriptor 构建。
- `deps` 输出依赖优先顺序，循环依赖报错。
- `.coox` 是声明级接口/元数据，供增量构建与跨包导入。

### 4.4 包命令

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
```

包命令共用：`--manifest <path>`、`--mirror <name|url>`、`--registry <name|url>`、`--verbose`。清单自动从当前目录向上查找。`add`/`remove` 先改清单再安装，安装失败自动回滚清单。

### 4.5 镜像、配置与诊断

```bat
coorc mirror list|show|add|remove|use|test|auto
coorc config list|get|set
coorc doctor [--network]
coorc platform
```

- `mirror test` 探测可达性与延迟；`mirror auto` 选择当前最快镜像。
- `doctor` 检查编译器候选、`curl`、配置/缓存目录、清单与网络；`platform` 打印宿主、目标三元组与可执行/对象/库命名。
- `config set mirror.timeout-ms 5000` 可调整超时。

## 5 包

包就是含 `coordination.toml`（推荐）或 `coord.pkg` 的目录。最小清单：

```toml
[package]
name = "app"
version = "0.1.0"
edition = "2026"
source = "src"

[dependencies]
math = "^1.2"
local = { path = "../lib" }
```

```bat
coorc init my-app --name my_app
cd my-app
coorc add math@^1.2
coorc install --locked
coorc build . -o out\my_app.exe
```

- `coord.lock` 固定版本、来源与 SHA-256；安装的包 vendor 到 `coord_modules/`。
- `--locked`/`--frozen`/`--offline` 控制可复现性；`mirror`/`config` 选择注册表与镜像。
- native 项目增加 `[build]`、`[toolchain.<target>]` 与 `[native]`；清单是编译器/target 的唯一来源。
- 完整清单字段、解析、注册表协议、`[trust]`、镜像、缓存与 vendor 规则见 [framework.zh-CN.md](framework.zh-CN.md) §8。

## 6 跨平台

Windows/Linux 运行完整 217 项测试（217/217）；macOS 注册同一套但本环境预期/未运行。详细矩阵见 [framework.zh-CN.md](framework.zh-CN.md) §6.6；宿主/target 三元组与编译器探测见 §6.5。

- 生成程序使用 C++20，按平台使用 `wmain`/`main`；路径/文件 API 为 UTF-8；见 §9.5。
- native 交叉编译由清单/toolchain 驱动；MSVC 不能交叉编译，交叉目标不能 `run`；见 §9.6。

## 7 教程示例

### 7.1 Optional 与构造

```coo
class Payload {
public:
    integer value;
    Payload() { value = 0; }
    Payload(integer initial) { value = initial; }
}

integer main() {
    @Optional(Skip) Payload* pointer = null;
    print((*pointer).value);              // 0

    @Optional(Default(Payload(7))) Payload* fallback = null;
    print((*fallback).value);             // 7
    return 0;
}
```

`@Optional` 与 `ProxyPolicy` 规则见 [framework.zh-CN.md](framework.zh-CN.md) §4.5.2；`@Constructure`/`@Destructure` 见 §4.8。

### 7.2 Native bridge

native 项目保留自己的 `.c`/`.cpp` 构建，然后调用 `coorc`：

```bat
cd examples\native\cpp_class
native\build.cmd
coorc run .
```

`coorc` 不编译用户 C/C++，只编译生成的 `extern "C"` bridge 并链接声明的 objects/libraries。完整契约见 [framework.zh-CN.md](framework.zh-CN.md) §7.4。

## 8 仓库结构

| 路径 | 内容 |
| --- | --- |
| [`libs/coord`](libs/coord) | `coorc_core`：前端、Sema、解释器、C++ 后端、driver、包管理/镜像/注册表；`include/coord/modules/` 下 38 个模块头 |
| [`tools/coorc`](tools/coorc) | CLI：编译、测试、包管理、镜像与诊断命令 |
| [`tests`](tests) | 单元测试、CTest、e2e 脚本与 fixtures |
| [`examples`](examples) | 可运行 `.coo` 示例 |
| [`lib/standard`](lib/standard) | 当前标准库源：`annotations.coo` 与 `utility.coo`；`documentation.coo` 是设计稿，未加载 |
| [`framework.md`](framework.md) / [`framework.zh-CN.md`](framework.zh-CN.md) | 权威框架与语言规范 |
| [`docs/grammar.ebnf`](docs/grammar.ebnf) | 跟踪 lexer/parser 的形式文法 |

## 9 限制与已知问题

- `lambda` 子集已可用：函数类型目标、表达式/块体、隐式/显式捕获（`[]`/`[=]`/`[&]`/`[x]`/`[&x]`/`[this]`）、捕获校验（`E2087`）、显式按值捕获只读（`E2088`）、按引用闭包逃逸检查（返回/字段/容器字面量/外层变量，`E2039`）、初始化/赋值、调用、实参与返回；省略捕获列表生成可变按值副本。`@FunctionalInterface` 已移除，lambda 以 `(A) -> R` 函数值传递；callable interface 的 `operator()`、未绑定实例引用与函数指针 thunk 仍属 🚧。未绑定实例方法引用与函数指针目标仍属 ⏳；跨线程入口捕获已由 `Thread` 检查（`E2090`），其余跨线程借用仍属 ⏳；静态（`Type::method`）与绑定（`object::method`）方法引用、函数类型字段调用与返回函数值的链式调用已可用，绑定接收者遵循按引用逃逸规则。`if` 表达式支持表达式分支（`if (c) a else b` 与 `else if` 链）；块分支 ⏳。`match` 支持常量、范围、`|`、绑定与 guard，并检查枚举/布尔/标量穷尽性；类型模式与类解构模式 ⏳。`@Native`/FFI 已改为项目配置：`@Native(file:, abi:, symbol:)` 只声明头文件/ABI，`coordination.toml` 的 `[build]`/`[toolchain.<target>]`/`[native]` 提供编译器/target/objects/libraries，`coorc` 生成 `extern "C"` bridge（C 函数包装、`const char*` 字符串、move-only 不透明 C++ class 句柄、无 `try`/`catch`）。旧 `@Native("symbol")` 报 `E2114`，单文件使用报 `E2112`；见 §7.7 与 `examples/native`。`@Wrap` 与 `unsafe`/`new`/`delete`/`extern` 仍属 ⏳；`@Copy` 与 `Weak<T>` 已实现；`@Wrap` 标 deferred（声明已移除）；详见 [framework.zh-CN.md](framework.zh-CN.md) §11。
- `ProxyPolicy` 的执行覆盖局部 `T*`/`@Unique`/`@Share` 变量，以及参数/字段/返回值上的基础 `Skip`/`Default`（`coord_e2e_optional_params`/`_fields`/`_return`），并覆盖 `@Proxy` 的 `Skip`/`Abort`/`Error` 解构。`@Proxy` 的 `Default(expr)` 构造/解构、runtime 构造前钩子、任意非默认构造类的 `Skip` 默认值仍属 🚧。
- `@Constructure` 类型化/命名构造与 `@Retention(Compile/Runtime)` 代码块已实现基础版；编译期块不能依赖运行期局部状态，代码块上的 `Invocation`/`enum` 报 `E2044`；复杂构造重载消歧仍属 🚧。
- Atomic v0：内建不可拷贝 `Atomic`，降为 `std::atomic<std::int64_t>`，支持 `load()`、`store(v)`、`fetchAdd(v)`/`fetchSub(v)`（返回操作前的值）、`exchange(v)`（返回旧值）与 `compareExchange(expected, desired)`（值等于 expected 并替换为 desired 时返回 true；expected 不被更新）。所有操作 seq_cst，无内存序参数；没有 `Atomic<T>` 泛型、浮点/指针原子、condition variable 或数据竞争静态分析。支持局部变量与类字段，参数/返回位置 ⏳。拷贝或赋值报 `E2092`。
- ScopedThread v0：内建 move-only `ScopedThread`，降为 `std::jthread`，析构时自动 join（离开声明块会等待线程结束）。支持 `join()`/`detach()`/`joinable()` 与和 `Thread` 相同的跨线程捕获规则（`E2090`）；不可拷贝/赋值，仅局部变量。没有 `stop_token`/`requestStop`、取消或跨线程异常传播。
- Safe/Unsafe v0：默认 safe；`@Unsafe` 可标 module（`@Unsafe;`）、class、function、method、constructor、block 并向下传播，`@Safe` 覆盖。`@Unsafe` 只解锁原始指针解引用、指针算术、`@Native`/FFI 调用与 unsafe cast，类型/借用/越界检查仍生效。safe 作用域调用 `@Unsafe`/`@Native` 报 `E2096`；`@Safe` 代码中可写 `@Unsafe { ... }` 审计边界。v0 先强制指针解引用与 FFI 调用。
- Copy v0：含 `@Unique`/`@Share`/指针字段的类默认不可复制，必须显式 `@Copy(Deep)`（值字段 + 可深拷贝 `@Unique` 链；拒绝 `@Share`/原始指针/引用/环）或 `@Copy(Shallow)`（逐成员拷贝，允许 `@Share` 与原始指针，禁止 `@Unique`）；可深拷贝性与“是否需要显式 @Copy”会递归检查值字段与基类；纯值类保留隐式复制。`@Copy` 错误报 `E2095`；类 `[x]` 锁捕获要求 `@Copy(Deep)`。
- ScopedLock/@Lock v0：`ScopedLock guard = ScopedLock(mutex);` 是 `std::lock_guard<std::mutex>` 形式的 RAII，由具名 `Mutex` 左值构造并在离开作用域时解锁；不可拷贝（`E2092`）且仅局部变量。`@Lock(Exclusive, mutex, [captures]) { ... }` 是块形式：`[x]` 复制 primitive/string/enum 值，class 值要求 `@Copy(Deep)`（否则 `E2094`），`[&x]` 可变借用，`[const&x]` 只读借用且只能调用 `@ReadOnly` 方法；写未捕获/`const&` 对象与能力错误报 `E2094`，捕获逃逸报 `E2039`，旧 `@Lock(mutex)` 语法报 `E2094`。只检查直接写/调用（无别名/数据竞争证明）；未捕获 class 对象只允许 `@ReadOnly` 调用，嵌套 `@Lock` 报 `E2094`，`[x]` 副本可返回、借用捕获不能逃逸（`E2039`）。没有超时、try-lock 或重入检查：同一 mutex 嵌套加锁会死锁。
- Mutex v0：内建不可拷贝 `Mutex`，降为 `std::mutex`，手动 `lock()`/`unlock()`/`tryLock()`；拷贝或赋值报 `E2091`。没有 RAII guard、没有静态数据竞争分析；忘记 unlock 会阻塞其它线程。`Mutex` 可作局部变量或类字段（含 Mutex 的对象用 `@Share` 共享）；参数/返回值位置 ⏳。
- Thread v0：内建 move-only `Thread`，由 `() -> void` 入口构造，支持 `join()`/`detach()`/`joinable()`/`hardwareConcurrency()`。入口闭包必须拥有捕获（`[]`、`[=]`、`[x]`、`@Share` 拷贝）；`[&]`、`[&x]`、`[this]` 与调用期借用的函数值报 `E2090`。可 join 的线程必须在析构前 join/detach；共享可变状态不保证无数据竞争，同步原语、scoped thread 与非局部 `Thread` 位置仍属 ⏳。
- 所有权与 borrow 检查仍是保守子集：`&` 不能返回/存字段/作容器元素；按引用闭包不能返回、存入字段/容器字面量或赋给外层作用域变量（`E2039`），完整跨线程借用与生命周期分析仍待补。函数类型参数按调用期借用处理：返回或存储它们会被拒绝。
- 别名局部变量声明 `l.Payload p = ...` 有语法歧义，当前只完整支持参数/返回类型位置。
- 包管理：workspace 只解析不递归安装；无 feature、git 依赖、包签名/信任链；`search` 依赖 `index.toml`。
- 交叉编译：native 项目由 `[build].target` + `[toolchain.<target>]` 驱动（程序与 bridge 同一 `cxx`）；外部 target 可用 `--emit-cpp` 做 compile-only。链接外部 target 需要已安装的交叉工具链，MSVC 宿主不能交叉编译，交叉目标不能运行。
- native bridge 遗留：为其它 target 构建的 object 只能由链接器发现；`--incremental` 不跟踪 `@Native` 声明的头文件（只跟踪源码、清单、objects 与工具链配置）；状态码 → Result proxy 映射、用户 C++ 异常（no-throw 契约未强制）、回调与单文件 `@Native` 仍为 ⏳。
- 修改头文件后必须清理重建；Windows 上 Ninja 对 MSVC 中文 `showIncludes` 不可靠。
- 测试数量是撰写时已验证的基线；请在本机重新运行 `ctest` 确认。
- 生成 C++ 后端是单翻译单元；大型项目/多模块链接与标准库完整 API 仍在推进。

## 10 文档

- 框架与语言规范：[framework.zh-CN.md](framework.zh-CN.md) / [framework.md](framework.md)
- 英文 QuickStart：[readme.md](readme.md)
- 形式文法：[docs/grammar.ebnf](docs/grammar.ebnf)
- 历史归档：已移除；[framework.zh-CN.md](framework.zh-CN.md) 是唯一来源。






