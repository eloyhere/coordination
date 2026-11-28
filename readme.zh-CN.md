# Coordination 快速上手

> 本文件是 Coordination 的 QuickStart、CLI、工具链与包管理器使用说明；语言与框架的权威定义见 [framework.zh-CN.md](framework.zh-CN.md)。

Coordination 是面向系统编程的静态类型语言：**没有宏**，编译期元编程由注解、描述符、反射与 proxy 驱动；对象生命周期复用 C++ RAII，当前后端输出 C++20。

- 源文件 `.coo`；模块接口 `.coox`；编译器 `coorc`。
- 包清单 `coordination.toml` / `coord.pkg`；锁文件 `coord.lock`。
- 当前基线：**213 项 CTest**，Windows 全套 **213/213 通过**；Linux 上 `coord_unit` + `coord_e2e_packages` 与新增示例已用 g++ 通过。
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

- Windows 使用 `windows-base` 派生 preset（Ninja + cl）；Linux/macOS 使用 `linux-release`/`macos-release`/`linux-debug`。
- `ctest` 注册 **213 项**（1 个单元测试 + 212 个 e2e）；`coord_e2e_packages` 不依赖宿主编译器与网络，`coord_e2e_packages_build` 与其它 e2e 需要可用的宿主 C++ 工具链。
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

## 5 包管理器与镜像简明用法

### 5.1 清单

包根目录有 `coordination.toml`（推荐）或 `coord.pkg`（M0 行式格式）即可成为包；两者同时存在时优先 `coordination.toml`。

`coord.pkg`：

```text
name app
version 0.1.0
source src
dependency ../lib
dependency math ^1.2
dev-dependency testkit 0.1
```

`coordination.toml`：

```toml
[package]
name = "app"
version = "0.1.0"
edition = "2026"
source = "src"

[dependencies]
math = "^1.2"
local = { path = "../lib" }
core = { version = "~0.3", platform = "windows" }

[dev-dependencies]
testkit = "0.1"

[registries]
official = "https://registry.coordination-lang.org"

[[mirrors]]
name = "cn"
registry = "official"
url = "https://cn.registry.coordination-lang.org"
priority = 10
```

- 版本约束：`^`、`~`、`*`、`>=`/`<`、`=1.2.3`；预发布只在其 `major.minor.patch` 被显式约束时匹配。
- `platform` 过滤支持 `windows`/`linux`/`macos`/`unix`/架构名与 `!` 取反；`optional = true` 仅在 `--all-optional` 时解析。
- workspace `[workspace] members` 当前只解析、不递归安装。

### 5.2 信任策略与安装

`coordination.toml` 可声明信任策略：

```toml
[trust]
require_checksum = true      # 拒绝没有 SHA-256 的注册表条目
allow_insecure_http = false  # 拒绝非回环 http:// 注册表
```

回环 HTTP（`localhost`、`127.0.0.1`、`[::1]`）和 `file://` 始终允许。策略错误在下载前报告；校验和不匹配会删除缓存归档。

### 5.3 安装、锁与 vendor

```bat
coorc install
coorc install --locked          # 锁文件过期直接失败，CI 推荐
coorc install --frozen          # 不写锁文件
coorc install --offline         # 只读缓存与 vendor
coorc update math               # 只更新指定包
coorc build tests\fixtures\two_packages\app -o out\app.exe
```

- 注册表包解压到 `coord_modules/<name>/`（含 `.coord-package.toml`），路径依赖不复制。
- `coord.lock` 固定版本、来源、注册表 URL、SHA-256 与依赖边；`install/update/add/remove` 自动原子写入。
- `coorc build` 自动收集 `coord_modules/*`、清单中的路径依赖（递归）与当前包 `source` 下的 `.coo`，因此安装后无需改语言 `import`。
- 增量 stamp 包含根清单、`coord.lock`、各 vendor 清单、文件数与最新 mtime，以及宿主目标三元组与编译器。

### 5.4 注册表

注册表就是一棵目录树（本地目录或 HTTP(S) 根）：

```text
index.toml
index/<name>.toml
packages/<name>/<version>.tar.gz
health
```

`coorc pack` 生成确定性 `.tar.gz`（条目排序、mtime 0、gzip stored 块）；`coorc publish` 写本地注册表或通过系统 `curl` PUT 到远程；凭据来自 `COORD_TOKEN`。归档 SHA-256 在解压前校验。

### 5.5 镜像

- **registry** 是仓库端点；**mirror** 是同一 registry 的另一个端点，带 `priority`（越大越先尝试）。
- 选择顺序：显式 `--mirror` → 项目配置 → 用户配置 → 内置目录；同一 registry 的镜像按 priority 降序尝试，失败自动切换。
- 连续失败写入 `<cache>/mirror-health.toml`，冷却 5 分钟；`health` 缺失时探测 `index.toml`。
- 配置层：内置 → 用户 `config.toml` → 项目清单 → 环境变量 → 命令行。环境变量：`COORD_REGISTRY`、`COORD_MIRROR`、`COORD_OFFLINE`、`COORD_TIMEOUT_MS`、`COORD_CONFIG`、`COORD_CACHE_DIR`、`COORD_CURL`。
- 内置 `cn`/`eu` 默认禁用，URL 是占位端点；实际使用时用 `mirror add` 或项目清单覆盖。

```bat
coorc mirror add cn https://cn.example/coord --registry official --priority 10
coorc mirror use cn
coorc mirror test cn
coorc config set mirror.timeout-ms 5000
```

### 5.6 缓存

```text
<cache>/coord/
  registries/<hash>/index/...
  downloads/<name>-<version>.archive
  sources/<name>-<version>-<hash>/
  tmp/
  mirror-health.toml
```

`<cache>` 默认 Windows `%LOCALAPPDATA%\coord\cache`、macOS `~/Library/Caches/coord`、Linux `${XDG_CACHE_HOME:-~/.cache}/coord`；`coorc clean` 删除 `coord_modules/`，`coorc clean --cache` 同时清缓存。

## 6 跨平台说明

### 6.1 支持的宿主

| 宿主 | 目标三元组 | 默认宿主 C++ 编译器 |
| --- | --- | --- |
| Windows x64/arm64 | `x86_64-pc-windows-msvc` / `aarch64-pc-windows-msvc` | `cl` |
| Linux x64/arm64 | `x86_64-unknown-linux-gnu` / `aarch64-unknown-linux-gnu` | `c++`（g++/clang++） |
| macOS x64/arm64 | `x86_64-apple-darwin` / `aarch64-apple-darwin` | `c++`（Apple clang） |
| FreeBSD | `*-unknown-freebsd` | `c++` |

Linux/macOS 已用 g++ 本地验证；Windows 为主开发平台。工具链源码不写 `#ifdef _WIN32`，平台差异集中在 `modules/platform`。

### 6.2 子进程与编译器

- 工具链用 `platform::run(argv)` 直接启动子进程，**不经过 shell**；参数按平台规则引用，避免路径空格、引号与 `cmd.exe` 展开问题。
- Windows 使用 `CreateProcessW`（UTF-16 命令行与环境块），Unix 使用 `fork`/`execvp` + 进程组；支持超时、输出捕获、stdin 管道与取消。
- MSVC 参数：`/nologo /std:c++20 /EHsc /utf-8 /Fe<exe> /Fo<obj>`；GCC/Clang：`-std=c++20 -O2 -pthread -o <exe>`。`--cxx-flavor` 可覆盖自动判断；`--verbose` 打印命令行。

### 6.3 路径、编码与目录

- 所有跨模块路径以 UTF-8 字符串传递；Windows 转换到 UTF-16 文件 API。
- 源文件内部统一 LF；读取时归一 CRLF/CR，写回 LF，行号/字节偏移跨平台一致。
- 可执行后缀 `.exe`（Windows），对象 `.obj`/`.o`，静态库 `foo.lib`/`libfoo.a`，动态库 `foo.dll`/`libfoo.so`/`libfoo.dylib`；`platform.h` 统一提供。
- 配置目录：`%APPDATA%\coord`、`~/Library/Application Support/coord`、`${XDG_CONFIG_HOME:-~/.config}/coord`；缓存目录见 §5.5。
- Windows 控制台启动时切换到 UTF-8 代码页；检测 `NO_COLOR` 与终端能力。

### 6.4 生成程序与交叉编译

生成 C++ 使用标准 C++20 + 条件 `<windows.h>`：

- Windows/MSVC 使用 `wmain`，把 UTF-16 `argv` 转成 UTF-8，并在启动时 `SetConsoleOutputCP/SetConsoleInputCP` 设 UTF-8；
- POSIX 使用 `main`，`argv` 按 UTF-8 解释；
- 路径/文件 API 使用 UTF-8 → `std::filesystem::path`（Windows 用 `u8string`）。

程序可用的内建：`readFile`、`writeFile`、`fileExists`、`deleteFile`、`timeMillis`、`sleepMillis`，以及 `operatingSystem()`、`architecture()`、`targetTriple()`、`pathSeparator()`、`lineSeparator()`、`executableSuffix()`、`currentDirectory()`、`temporaryDirectory()`、`homeDirectory()`、`environment(name)`、`setEnvironment`、`pathJoin`、`pathFileName`、`pathParent`、`pathExtension`、`pathIsAbsolute`、`pathNormalize`、`pathExists`、`createDirectory`、`createDirectories`、`deleteDirectory`、`listDirectory(path)`。

交叉编译：

```bat
coorc build src\main.coo --target x86_64-unknown-linux-gnu --sysroot C:\sysroots\linux
coorc build src\main.coo --cxx C:\toolchains\aarch64-linux-gnu\bin\aarch64-linux-gnu-g++
```

- clang 宿主会收到 `--target`；GCC 交叉工具链通过 `--cxx` 指定；`--sysroot` 传递 sysroot。
- MSVC 不能交叉编译；交叉编译目标不能 `coorc run`，只能 `check`/`build`/`--emit-cpp`。

## 7 示例

### 7.1 ProxyPolicy 与 Optional

`ProxyPolicy` 由所有代理对象共享；Optional 只是代理的一种。

```coo
class Payload {
public:
    integer value;
    Payload() { value = 0; }
    Payload(integer initial) { value = initial; }
}

integer main() {
    @Optional @Unique Payload empty = null;
    print(empty.isPresent());            // false
    print(empty.orElse(Payload(0)).value); // 0

    @Optional(Abort) Payload* pointer = null;
    // *pointer  -> 运行期终止并打印 optional value is null

    @Optional(Skip) Payload* skipped = null;
    print((*skipped).value);             // 0（默认构造对象）

    @Optional(Default(Payload(7))) Payload* fallback = null;
    print((*fallback).value);            // 7
    return 0;
}
```

默认 `@Optional` 策略是 `Error`，要求先收窄：

```coo
@Optional Payload* pointer = null;
if (pointer != null) {
    print(pointer.value); // 收窄后可直接访问
}
// print(pointer.value);  // E2048
```

`@Proxy` 使用同一套策略；不写 policy 时默认 `Skip`：

```coo
@Proxy(Trace, Abort)
class Service { ... }
```

`@Weak T` 映射为 `std::weak_ptr<T>`，观察 `@Share T` 但不延长生命周期：

```coo
@Share Box owner = Box(7);
@Weak Box weak = owner;
print(weak.isPresent());          // true
print(weak.lock().get().value);   // 7
```

`isPresent()/isEmpty()` 判断是否过期；`lock()/upgrade()` 返回可空 `@Share` 句柄。未初始化的局部变量和字段会默认初始化（`0`、空字符串、默认构造对象），不再保留不确定值。

### 7.2 构造与解构

`@Constructure` 开启类型化/命名字面量构造；`@Proxy` 类默认支持同样的类型化/命名构造和字段解构，无需显式 `@Constructure`/`@Destructure`。类型标签可省略，已有类型的变量/字面量直接写：

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

    @Destructure
    void unpack(integer& outX, string& outY) {
        outX = x + 1;
        outY = y + "!";
    }
}

integer main() {
    integer a = 3;
    string b = "hi";

    Point p = {a, b};                  // 已声明变量
    Point q = {5, "ok"};               // 位置值
    Point r = {y = "named", x = 9};    // 命名参数
    Point s = {integer a, string b};   // 显式标签消歧

    {integer ax, string by} = p;       // ax == 4, by == "hi!"
    return 0;
}
```

- 条目为 `[A] value`、`[A] name` 或 `[A] name = expr`；`A` 与 name 可省略。无 `=` 的裸 name 既是值也是匹配名；有 `=` 的按参数名匹配。
- 只匹配 `@Constructure` 构造函数：数量、名字、位置/值类型与可选类型标签；无匹配或歧义报 `E2076`。
- 目标类 `@Destructure` 方法接收默认初始化绑定的引用并写入输出；否则逐字段读取（存在代理 trap 时先经过 trap）。

### 7.3 代码块保留

```coo
integer main() {
    @Retention(Compile)
    {
        print("compile-time");
    }

    @Retention(Runtime)
    {
        print("runtime");
    }
    return 0;
}
```

- `@Retention(Compile)` 块由解释器在编译期执行；`print` 输出可用 `coorc --print-consts` 查看，生成程序不包含该块。
- `@Retention(Runtime)` 块不在编译期执行，运行时正常执行。
- 代码块只允许 `Compile`/`Runtime`；`Invocation`/`enum` 报 `E2044`。

### 7.4 数字进制与静态定点

```coo
@Power(4) fix convert(fix value) {
    return value;                    // 返回目标 @Power(4) 触发转换
}

integer main() {
    @Power(2) fix price = 1.5;
    @Power(4) fix fine = price;      // 目标注解加宽 power
    @Radix(2) @Power(4) fix q = price; // 目标注解改变 radix
    @Length(16) @Power(2) fix wide = price;
    @Power(2) fix one = 1.0;
    @Power(2) fix low = one / 3;     // 自然 power 下为 0.33
    @Power(4) fix high = one / 3;    // 0.3333：目标注解覆盖精度
    print(fine);
    print(q);                        // 以 2 进制打印 1.1
    print(wide);
    print(convert(2));
    print(low);                      // 0.33
    print(high);                     // 0.3333
    return 0;
}
```

- `fix` 是静态定点数：`raw = semantic × radix^power`，表示为 `coord_rt::Fix<Power, Radix, Length>`，只有一个有符号 8/16/32/64/128 位 raw 整数（无运行期 scale）。默认 `@Length(64) @Radix(10) @Power(0)`。
- `@Power` 作用于 `integer` 或 `float` 声明时提升为 `fix`；`@Radix` 单独不提升。小数字面量初始化 `fix` 必须显式给出 `@Power`（否则 `E2103`），编译期精确编码，超过目标 scale 时按 half away from zero 舍入。`@Length(128)` 字面量编码到有符号 128 位 raw（如 `@Length(128) @Power(0) fix max = 170141183460469231731687303715884105727.0;`），超出范围报 `E2105`。
- fix→fix 转换由目标类型注解驱动：给目标变量/字段/参数/返回值加 `@Length`/`@Radix`/`@Power`（`@Power(4) fix fine = price;`、`@Radix(2) @Power(4) fix q = price;`、`@Length(16) ...`）。**完全相同的 canonical fix 类型**（`@Length`/`@Radix`/`@Power` 均相同，含默认 `fix`）之间的复制是普通 storage 复制，不需要目标注解；改变类型且目标无注解时报 `E2109`。该规则同样适用于字段、参数、返回值、数组/队列字面量与 map value，因此容器操作不会引入隐式跨 storage 转换。不提供 `toRadix`/`toScale` 方法；`toInteger()`/`toFloat()` 只用于跨 storage 边界。
- 所有运算符可用：`+ - * / %`、一元 `-`、`++/--`、比较、`& | ^ ~ << >>`。算术/比较按语义（`+`/`-` 对齐更细 power，`*` power 相加，`/` 自然 `max(power1,power2)` 且显式目标可覆盖，`%` 符号跟随被除数）；位运算/移位按 raw，要求 radix/power/length 完全一致（`E2111`）。
- `@Length` 支持 8/16/32/64/128，并实现结果宽度加宽（`+`/`-`：`max(len1,len2)+1`，`*`：`len1+len2`；低于 128 的宽度保持原结果宽度，含 128 位操作数时结果加宽到 128）。带显式注解目标的 `*`/`/` 使用更宽中间值：旧宽度用 64×64→128，128×128 用可移植 256 位中间值，从而可安全转换到更窄的目标 scale 而不截断。
- `@Length(128)` 的 raw 是有符号二进制补码 128 位：C++ 后端用自定义双 limb `coord_rt::UInt128` 作为位模式并叠加有符号 helper，GCC/Clang 下 64×64→128 原语用 `__int128`，64 位 MSVC 用 `_mul128`（其它平台用可移植 32 位拆分），不依赖非标准库。溢出、除零与越界转换均 checked，打印 `fix ...` 诊断并终止。静态 `@Power` scale 常量仍必须放入有符号 64 位整数，因此 radix 10 的 `@Power(20)` 即使 `@Length(128)` 仍报 `E2102`。
- `print` 按该类型的 radix（2–36）输出语义值并去掉无意义尾零。
- `@Radix` 接受任意整数 `>= 2`；`0`、`1` 与负数报 `E2100`。作用于 `integer` 时只改变生成 C++ 的字面量写法（2/8/16 进制：`0XFF`、`0B1010`、C++ 八进制 `0100`），其他进制退回十进制；运行期打印整数不变。

### 7.5 注解处理器

annotation 通过实现 `AnnotationProcessor` 并 override `process(Description)` 生成声明；旧的 `@Processor` 方法注解已删除：

```coo
@Target(class)
annotation Serializable implements AnnotationProcessor {
    override void process(Description target) {
        addFunctionReturning("serialize", target.fieldCount());
    }
}

@Serializable
class Data {
public:
    integer first;
    string second;
}

integer main() {
    print(serialize());
    return 0;
}
```

- `Processor` 是根标记接口；`AnnotationProcessor`、`ProxyHandler`、`InvocationHandler` 都继承它。只有 annotation 声明可以实现 `AnnotationProcessor`（`E2060`），且必须恰好有一个 `override void process(Description)`（`E2058`/`E2059`/`E2060`）。
- 同一个 annotation 可以同时实现 `AnnotationProcessor` 与 `ProxyHandler`（编译期生成 + 运行期 trap）；见 [`examples/annotation_processor.coo`](examples/annotation_processor.coo)。
- 可用的生成原语：`addFunction`、`addFunctionReturning`、`addConstant`、`addAnnotatedFunction`，以及 `line(file, line)`。

### 7.6 包依赖

```bat
coorc init app --name app
cd app
coorc add math@^1.2
coorc install --locked
coorc build . -o out\app.exe
```

## 8 仓库结构

| 路径 | 内容 |
| --- | --- |
| [`libs/coord`](libs/coord) | `coorc_core`：前端、Sema、解释器、C++ 后端、driver、包管理/镜像/注册表；`include/coord/modules/` 下 38 个模块头 |
| [`tools/coorc`](tools/coorc) | CLI：编译、测试、包管理、镜像与诊断命令 |
| [`tests`](tests) | 单元测试、CTest、e2e 脚本与 fixtures |
| [`examples`](examples) | 可运行 `.coo` 示例 |
| [`lib/standard`](lib/standard) | 标准库源（`annotations.coo` 与 `utility.coo` 已接入） |
| [`framework.md`](framework.md) / [`framework.zh-CN.md`](framework.zh-CN.md) | 权威框架与语言规范 |
| [`docs/grammar.ebnf`](docs/grammar.ebnf) | 形式化文法 |
| [`docs/archive`](docs/archive) | 历史文档，以 `framework.md` 为准 |

## 9 限制与已知问题

- `lambda` 子集已可用：函数类型目标、表达式/块体、隐式/显式捕获（`[]`/`[=]`/`[&]`/`[x]`/`[&x]`/`[this]`）、捕获校验（`E2087`）、显式按值捕获只读（`E2088`）、按引用闭包逃逸检查（返回/字段/容器字面量/外层变量，`E2039`）、初始化/赋值、调用、实参与返回；省略捕获列表生成可变按值副本。函数式接口目标以函数值子集提供：`lib/standard/utility.coo` 的 `Function`/`Consumer`/`Predicate`/`Supplier`/`BiFunction` 降为函数类型（泛型实参替换，`.apply`/`.accept`/`.test`/`.get` 调用形式）；具名接口值与类实现 `@FunctionalInterface` 仍属 ⏳（`E2004`/`E2089`），且 `@FunctionalInterface` 已弃用，等 Lambda 模块（切片 12）结构化 `operator()` 后删除。未绑定实例方法引用与函数指针目标仍属 ⏳；跨线程入口捕获已由 `Thread` 检查（`E2090`），其余跨线程借用仍属 ⏳；静态（`Type::method`）与绑定（`object::method`）方法引用、函数类型字段调用与返回函数值的链式调用已可用，绑定接收者遵循按引用逃逸规则。`if` 表达式支持表达式分支（`if (c) a else b` 与 `else if` 链）；块分支 ⏳。`match` 支持常量、范围、`|`、绑定与 guard，并检查枚举/布尔/标量穷尽性；类型模式与类解构模式 ⏳。`@Native`/FFI 为 v1 子集：无函数体、原始类型/string 参数与返回值，通过生成包装调用 C 符号；class/指针/header/library 与 C++ ABI 选择仍属 ⏳，且 `@Native` 不再以 variable 为目标。`@Copy`/`@Wrap`、`Weak<T>`、`unsafe`/`new`/`delete`/`extern` 仍属 ⏳；`@Wrap` 标 deferred（声明已移除）；详见 [framework.zh-CN.md](framework.zh-CN.md) §11。
- `ProxyPolicy` 的执行主要验证了局部 `T*`/`@Unique`/`@Share` 变量（直接访问、收窄、局部 Optional 解构）与 `@Proxy` 的 `Skip`/`Abort`/`Error` 解构路径。字段/参数/返回值上的 `@Optional` 已解析/校验，但完整 policy 执行仍不完整；`@Proxy` 的 `Default(expr)` 构造/解构、runtime 构造前钩子、任意非默认构造类的 `Skip` 默认值仍属 🚧。
- `@Constructure` 类型化/命名构造与 `@Retention(Compile/Runtime)` 代码块已实现基础版；编译期块不能依赖运行期局部状态，代码块上的 `Invocation`/`enum` 报 `E2044`；复杂构造重载消歧仍属 🚧。
- Atomic v0：内建不可拷贝 `Atomic`，降为 `std::atomic<std::int64_t>`，支持 `load()`、`store(v)`、`fetchAdd(v)`/`fetchSub(v)`（返回操作前的值）、`exchange(v)`（返回旧值）与 `compareExchange(expected, desired)`（值等于 expected 并替换为 desired 时返回 true；expected 不被更新）。所有操作 seq_cst，无内存序参数；没有 `Atomic<T>` 泛型、浮点/指针原子、condition variable 或数据竞争静态分析。支持局部变量与类字段，参数/返回位置 ⏳。拷贝或赋值报 `E2092`。
- ScopedThread v0：内建 move-only `ScopedThread`，降为 `std::jthread`，析构时自动 join（离开声明块会等待线程结束）。支持 `join()`/`detach()`/`joinable()` 与和 `Thread` 相同的跨线程捕获规则（`E2090`）；不可拷贝/赋值，仅局部变量。没有 `stop_token`/`requestStop`、取消或跨线程异常传播。
- Safe/Unsafe v0：默认 safe；`@Unsafe` 可标 module（`@Unsafe;`）、class、function、method、constructor、block 并向下传播，`@Safe` 覆盖。`@Unsafe` 只解锁原始指针解引用、指针算术、`@Native`/FFI 调用与 unsafe cast，类型/借用/越界检查仍生效。safe 作用域调用 `@Unsafe`/`@Native` 报 `E2096`；`@Safe` 代码中可写 `@Unsafe { ... }` 审计边界。v0 先强制指针解引用与 FFI 调用。
- Copy v0：含 `@Unique`/`@Share`/指针字段的类默认不可复制，必须显式 `@Copy(Deep)`（值字段 + 可深拷贝 `@Unique` 链；拒绝 `@Share`/原始指针/引用/环）或 `@Copy(Shallow)`（逐成员拷贝，允许 `@Share` 与原始指针，禁止 `@Unique`）；可深拷贝性与“是否需要显式 @Copy”会递归检查值字段与基类；纯值类保留隐式复制。`@Copy` 错误报 `E2095`；类 `[x]` 锁捕获要求 `@Copy(Deep)`。
- ScopedLock/@Lock v0：`ScopedLock guard = ScopedLock(mutex);` 是 `std::lock_guard<std::mutex>` 形式的 RAII，由具名 `Mutex` 左值构造并在离开作用域时解锁；不可拷贝（`E2092`）且仅局部变量。`@Lock(Exclusive, mutex, [captures]) { ... }` 是块形式：`[x]` 复制 primitive/string/enum 值（class 值捕获拒绝），`[&x]` 可变借用，`[const&x]` 只读借用且只能调用 `@ReadOnly` 方法；写未捕获/`const&` 对象与能力错误报 `E2094`，捕获逃逸报 `E2039`，旧 `@Lock(mutex)` 语法报 `E2094`。只检查直接写/调用（无别名/数据竞争证明）；未捕获 class 对象只允许 `@ReadOnly` 调用，嵌套 `@Lock` 报 `E2094`，`[x]` 副本可返回、借用捕获不能逃逸（`E2039`）。没有超时、try-lock 或重入检查：同一 mutex 嵌套加锁会死锁。
- Mutex v0：内建不可拷贝 `Mutex`，降为 `std::mutex`，手动 `lock()`/`unlock()`/`tryLock()`；拷贝或赋值报 `E2091`。没有 RAII guard、没有静态数据竞争分析；忘记 unlock 会阻塞其它线程。`Mutex` 可作局部变量或类字段（含 Mutex 的对象用 `@Share` 共享）；参数/返回值位置 ⏳。
- Thread v0：内建 move-only `Thread`，由 `() -> void` 入口构造，支持 `join()`/`detach()`/`joinable()`/`hardwareConcurrency()`。入口闭包必须拥有捕获（`[]`、`[=]`、`[x]`、`@Share` 拷贝）；`[&]`、`[&x]`、`[this]` 与调用期借用的函数值报 `E2090`。可 join 的线程必须在析构前 join/detach；共享可变状态不保证无数据竞争，同步原语、scoped thread 与非局部 `Thread` 位置仍属 ⏳。
- 所有权与 borrow 检查仍是保守子集：`&` 不能返回/存字段/作容器元素；按引用闭包不能返回、存入字段/容器字面量或赋给外层作用域变量（`E2039`），完整跨线程借用与生命周期分析仍待补。函数类型参数按调用期借用处理：返回或存储它们会被拒绝。
- 别名局部变量声明 `l.Payload p = ...` 有语法歧义，当前只完整支持参数/返回类型位置。
- 包管理：workspace 只解析不递归安装；无 feature、git 依赖、包签名/信任链；`search` 依赖 `index.toml`。
- 交叉编译：MSVC 宿主不能交叉编译，交叉目标不能运行。
- 修改头文件后必须清理重建；Windows 上 Ninja 对 MSVC 中文 `showIncludes` 不可靠。
- 测试数量是撰写时已验证的基线；请在本机重新运行 `ctest` 确认。
- 生成 C++ 后端是单翻译单元；大型项目/多模块链接与标准库完整 API 仍在推进。

## 10 文档

- 框架与语言规范：[framework.zh-CN.md](framework.zh-CN.md) / [framework.md](framework.md)
- 英文 QuickStart：[readme.md](readme.md)
- 形式文法：[docs/grammar.ebnf](docs/grammar.ebnf)
- 历史归档：[docs/archive/](docs/archive/)（不再维护）
