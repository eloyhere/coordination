# Coordination QuickStart

> This document covers QuickStart, the CLI, the toolchain and package-manager usage. The authoritative language and framework definition is [framework.md](framework.md).

Coordination is a statically typed systems language with **no macros**: compile-time metaprogramming is driven by annotations, descriptors, reflection and proxies, while object lifetime reuses C++ RAII and the current backend emits C++20.

- Source files `.coo`; module interfaces `.coox`; compiler `coorc`.
- Package manifests `coordination.toml` / `coord.pkg`; lockfile `coord.lock`.
- Current baseline: **119 CTest cases**, with **119/119 passing on Windows**; on Linux, `coord_unit` + `coord_e2e_packages` and the new examples pass with g++.
- The language and toolchain are wrapping up M3; [framework.md](framework.md) / [framework.zh-CN.md](framework.zh-CN.md) are authoritative, while this document focuses on usage.

## 1 Run It in 30 Seconds

Prerequisites: CMake 3.20+, Ninja and a C++20 host compiler:

- Windows: Visual Studio 2022 (MSVC 14.3x), run inside **x64 Native Tools Command Prompt for VS 2022**;
- Linux/macOS: GCC 10+ or Clang 12+ (`c++` on `PATH`).

Windows:

```bat
cmake -S . -B out\build\coord -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out\build\coord --clean-first
ctest --test-dir out\build\coord --output-on-failure

set COORC=out\build\coord\tools\coorc\coorc.exe
%COORC% run examples\hello.coo
```

Linux/macOS:

```bash
cmake --preset linux-release     # or macos-release / linux-debug
cmake --build out/build/linux-release
ctest --test-dir out/build/linux-release --output-on-failure

./out/build/linux-release/tools/coorc/coorc run examples/hello.coo
```

Output:

```text
hello, coordination
0
```

Create and build a package:

```bat
%COORC% init my-app --name my_app
%COORC% build my-app -o out\my_app.exe
```

> **Clean rebuild after header changes**: MSVC emits `showIncludes` output in Chinese, which Ninja cannot parse reliably. Use `--clean-first` or delete `out\build\coord`.

## 2 First Program and Main Function

```coo
integer main() {
    print("hello");
    return 0;
}
```

`main` **must** return `integer`; there is no `int`, `void main` or implicit return. Three signatures are supported:

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

- `void main()`, `decimal main()` and any other parameter form report `E2052`;
- `@Result<T>` changes error propagation only, never the "must return `integer`" rule;
- the generated C++ entry converts the result into the process exit code.

## 3 Build and Test

```bat
cmake -S . -B out\build\coord -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out\build\coord --clean-first
ctest --test-dir out\build\coord --output-on-failure
```

Linux/macOS presets:

```bash
cmake --preset linux-release
cmake --build out/build/linux-release
ctest --test-dir out/build/linux-release --output-on-failure
```

- Windows uses presets derived from `windows-base` (Ninja + cl); Linux/macOS use `linux-release`/`macos-release`/`linux-debug`.
- `ctest` registers **119 tests** (1 unit test plus 118 e2e tests); `coord_e2e_packages` needs neither a host C++ compiler nor network, while `coord_e2e_packages_build` and the other e2e tests need a working host C++ toolchain.
- Always `--clean-first` after header changes; keep generated C++ for inspection with `coorc --emit-cpp out\x.cpp file.coo`.

## 4 CLI

### 4.1 Compile, Run and Check

```bat
coorc check examples\add.coo
coorc run examples\hello.coo
coorc build examples\project -o out\project.exe
coorc build examples\hello.coo -o out\hello.exe --incremental
coorc check examples\hello.coo --emit-coox out\hello.coox
coorc --emit-cpp out\hello.cpp examples\hello.coo
```

| Command / option | Purpose |
| --- | --- |
| `check <file-or-dir>` | Load, lex, parse, processors, Sema, ConstEval; no executable |
| `build <file-or-dir>` | Through C++ lowering + host compile/link; `-o` selects the artifact |
| `run <file>` | Build and execute; cross targets cannot `run` |
| `--emit-cpp <file>` | Emit generated C++ (`-` for stdout) |
| `--emit-coox <file>` | Emit a declaration-level module interface digest |
| `--incremental` | Skip unchanged builds using `.coo`, root manifest and `.coox` stamps |
| `--keep-cpp` | Keep the intermediate C++ file |
| `--cxx <path>` | Select the host C++ compiler |
| `--cxx-flavor msvc\|gnu` | Force the compiler argument style |
| `--target <triple>` | Cross-compile target (clang receives `--target`) |
| `--sysroot <path>` | Cross-compilation sysroot |
| `--dump-tokens` / `--dump-ast` / `--print-consts` | Debug output; `--print-consts` also shows compile-time output from `@Retention(Compile)` blocks |
| `-Werror` | Promote warnings to errors |
| `--verbose` | Print the host command line |
| `--version` | Version |

Host compiler discovery order: `--cxx` → `COORD_CXX` → `CXX` → platform candidates (Windows: `cl`/`clang++`/`g++`; elsewhere: `c++`/`clang++`/`g++`).

### 4.2 Tests

```bat
coorc test examples\test_sample.coo
coorc test examples\test_sample.coo --filter addition
```

`@Test` marks zero-argument functions returning `void`. `coorc test` compiles and runs the tests collected in the file **serially**; failure exits nonzero and prints file/line. Assertions: `assert`, `assertEq`, `assertTrue`, `assertFalse`.

### 4.3 Outline, deps and Interfaces

```bat
coorc outline examples\hello.coo
coorc deps tests\fixtures\two_packages\app
coorc check examples\hello.coo --emit-coox out\hello.coox
```

- `outline` prints modules/classes/fields/methods/constructors/destructors/enum variants with signatures, access, annotations and proxy markers; the tree is built from descriptors.
- `deps` prints dependency-first order and reports cycles.
- `.coox` is a declaration-level interface/metadata file used for incremental builds and cross-package imports.

### 4.4 Package Commands

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

Package commands share `--manifest <path>`, `--mirror <name|url>`, `--registry <name|url>` and `--verbose`. Manifests are discovered upward from the current directory. `add`/`remove` edit the manifest before installing and roll the manifest back on failure.

### 4.5 Mirrors, Config and Diagnostics

```bat
coorc mirror list|show|add|remove|use|test|auto
coorc config list|get|set
coorc doctor [--network]
coorc platform
```

- `mirror test` probes reachability and latency; `mirror auto` picks the fastest mirror.
- `doctor` checks compiler candidates, `curl`, config/cache directories, manifests and network; `platform` prints the host, target triple and executable/object/library naming.
- `config set mirror.timeout-ms 5000` adjusts the timeout.

## 5 Package Manager and Mirrors in Brief

### 5.1 Manifests

A package is any directory with `coordination.toml` (recommended) or `coord.pkg` (M0 line format); `coordination.toml` wins when both exist.

`coord.pkg`:

```text
name app
version 0.1.0
source src
dependency ../lib
dependency math ^1.2
dev-dependency testkit 0.1
```

`coordination.toml`:

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

- Version constraints: `^`, `~`, `*`, `>=`/`<`, `=1.2.3`; prereleases match only when their `major.minor.patch` is explicitly constrained.
- `platform` filters `windows`/`linux`/`macos`/`unix`/architecture names with `!` negation; `optional = true` resolves only with `--all-optional`.
- `[workspace] members` is currently parsed but not installed recursively.

### 5.2 Install, Lock and Vendor

```bat
coorc install
coorc install --locked          # fail on stale lockfile; recommended for CI
coorc install --frozen          # do not write the lockfile
coorc install --offline         # cache/vendor only
coorc update math               # update only the named package
coorc build tests\fixtures\two_packages\app -o out\app.exe
```

- Registry packages extract to `coord_modules/<name>/` (with `.coord-package.toml`); path dependencies are not copied.
- `coord.lock` pins versions, sources, registry URLs, SHA-256 and dependency edges; `install/update/add/remove` write it atomically.
- `coorc build` automatically collects `coord_modules/*`, manifest path dependencies (recursively) and `.coo` under the package `source`, so installing never requires changing language `import`s.
- The incremental stamp covers the root manifest, `coord.lock`, vendor manifests, file counts and newest mtimes, plus the host target triple and compiler.

### 5.3 Registries

A registry is a directory tree (local directory or HTTP(S) root):

```text
index.toml
index/<name>.toml
packages/<name>/<version>.tar.gz
health
```

`coorc pack` emits a deterministic `.tar.gz` (sorted entries, mtime 0, gzip stored blocks); `coorc publish` writes a local registry or PUTs to a remote one through system `curl`; credentials come from `COORD_TOKEN`. Archive SHA-256 is verified before extraction.

### 5.4 Mirrors

- **registry**: a repository endpoint; **mirror**: another endpoint for the same registry with a `priority` (higher tried first).
- Selection order: explicit `--mirror` → project config → user config → built-in catalog; mirrors of one registry are tried by descending priority with automatic failover.
- Consecutive failures are recorded in `<cache>/mirror-health.toml` with a 5-minute cooldown; when `health` is missing, `index.toml` is probed.
- Config layers: built-in → user `config.toml` → project manifest → environment → command line. Environment: `COORD_REGISTRY`, `COORD_MIRROR`, `COORD_OFFLINE`, `COORD_TIMEOUT_MS`, `COORD_CONFIG`, `COORD_CACHE_DIR`, `COORD_CURL`.
- Built-in `cn`/`eu` mirrors are disabled and use placeholder URLs; override them with `mirror add` or the project manifest.

```bat
coorc mirror add cn https://cn.example/coord --registry official --priority 10
coorc mirror use cn
coorc mirror test cn
coorc config set mirror.timeout-ms 5000
```

### 5.5 Cache

```text
<cache>/coord/
  registries/<hash>/index/...
  downloads/<name>-<version>.archive
  sources/<name>-<version>-<hash>/
  tmp/
  mirror-health.toml
```

`<cache>` defaults to `%LOCALAPPDATA%\coord\cache` on Windows, `~/Library/Caches/coord` on macOS and `${XDG_CACHE_HOME:-~/.cache}/coord` on Linux; `coorc clean` removes `coord_modules/`, and `coorc clean --cache` also clears the cache.

## 6 Cross-Platform Notes

### 6.1 Supported Hosts

| Host | Target triple | Default host C++ compiler |
| --- | --- | --- |
| Windows x64/arm64 | `x86_64-pc-windows-msvc` / `aarch64-pc-windows-msvc` | `cl` |
| Linux x64/arm64 | `x86_64-unknown-linux-gnu` / `aarch64-unknown-linux-gnu` | `c++` (g++/clang++) |
| macOS x64/arm64 | `x86_64-apple-darwin` / `aarch64-apple-darwin` | `c++` (Apple clang) |
| FreeBSD | `*-unknown-freebsd` | `c++` |

Linux/macOS have been verified locally with g++; Windows is the primary development platform. Toolchain sources never write `#ifdef _WIN32`; platform differences live in `modules/platform`.

### 6.2 Subprocesses and Compilers

- The toolchain spawns subprocesses with `platform::run(argv)` **without a shell**, quoting arguments per platform to avoid spaces, quotes and `cmd.exe` expansion.
- Windows uses `CreateProcessW` (UTF-16 command line and environment block); Unix uses `fork`/`execvp` + process groups; timeouts, output capture, stdin pipes and cancellation are supported.
- MSVC flags: `/nologo /std:c++20 /EHsc /utf-8 /Fe<exe> /Fo<obj>`; GCC/Clang: `-std=c++20 -O2 -pthread -o <exe>`. `--cxx-flavor` overrides auto-detection; `--verbose` prints the command line.

### 6.3 Paths, Encoding and Directories

- All cross-module paths travel as UTF-8; Windows converts to UTF-16 file APIs.
- Sources use LF internally; CRLF/CR are normalized on read and LF is written back, keeping lines/offsets identical across platforms.
- Executable suffix `.exe` (Windows), objects `.obj`/`.o`, static libraries `foo.lib`/`libfoo.a`, shared libraries `foo.dll`/`libfoo.so`/`libfoo.dylib`; provided by `platform.h`.
- Config dirs: `%APPDATA%\coord`, `~/Library/Application Support/coord`, `${XDG_CONFIG_HOME:-~/.config}/coord`; cache dirs in §5.5.
- Windows consoles switch to the UTF-8 code page at startup; `NO_COLOR` and terminal capability are detected.

### 6.4 Generated Programs and Cross-Compilation

Generated C++ uses standard C++20 plus a conditional `<windows.h>`:

- Windows/MSVC uses `wmain`, converts UTF-16 `argv` to UTF-8, and calls `SetConsoleOutputCP`/`SetConsoleInputCP` with UTF-8 at startup;
- POSIX uses `main`, and `argv` is interpreted as UTF-8;
- path/file APIs use UTF-8 → `std::filesystem::path` (Windows constructs from `u8string`).

Program built-ins: `readFile`, `writeFile`, `fileExists`, `deleteFile`, `timeMillis`, `sleepMillis`, plus `operatingSystem()`, `architecture()`, `targetTriple()`, `pathSeparator()`, `lineSeparator()`, `executableSuffix()`, `currentDirectory()`, `temporaryDirectory()`, `homeDirectory()`, `environment(name)`, `setEnvironment`, `pathJoin`, `pathFileName`, `pathParent`, `pathExtension`, `pathIsAbsolute`, `pathNormalize`, `pathExists`, `createDirectory`, `createDirectories`, `deleteDirectory`, `listDirectory(path)`.

Cross-compilation:

```bat
coorc build src\main.coo --target x86_64-unknown-linux-gnu --sysroot C:\sysroots\linux
coorc build src\main.coo --cxx C:\toolchains\aarch64-linux-gnu\bin\aarch64-linux-gnu-g++
```

- A clang host receives `--target`; GCC cross toolchains are supplied via `--cxx`; `--sysroot` passes a sysroot.
- MSVC cannot cross-compile; cross targets cannot `coorc run` — only `check`/`build`/`--emit-cpp`.

## 7 Examples

### 7.1 ProxyPolicy and Optional

`ProxyPolicy` is shared by all proxy objects; Optional is one proxy kind.

```coo
class Payload {
public:
    integer value;
    Payload() { value = 0; }
    Payload(integer initial) { value = initial; }
}

integer main() {
    @Optional @Unique Payload empty = null;
    print(empty.isPresent());              // false
    print(empty.orElse(Payload(0)).value); // 0

    @Optional(Abort) Payload* pointer = null;
    // *pointer  -> terminates at runtime, printing "optional value is null"

    @Optional(Skip) Payload* skipped = null;
    print((*skipped).value);               // 0 (default-constructed object)

    @Optional(Default(Payload(7))) Payload* fallback = null;
    print((*fallback).value);              // 7
    return 0;
}
```

The default `@Optional` policy is `Error`, so direct access requires narrowing first:

```coo
@Optional Payload* pointer = null;
if (pointer != null) {
    print(pointer.value); // direct access after narrowing
}
// print(pointer.value);  // E2048
```

`@Proxy` uses the same policies; without a policy argument it defaults to `Skip`:

```coo
@Proxy(Trace, Abort)
class Service { ... }
```

`@Weak T` maps to `std::weak_ptr<T>` and observes an `@Share T` owner without extending its lifetime:

```coo
@Share Box owner = Box(7);
@Weak Box weak = owner;
print(weak.isPresent());          // true
print(weak.lock().get().value);   // 7
```

`isPresent()/isEmpty()` check expiry; `lock()/upgrade()` return a nullable `@Share` handle. Uninitialized locals and fields are default-initialized (`0`, empty string, default-constructed class) instead of left indeterminate.

### 7.2 Construction and Destructuring

`@Constructure` enables typed/named brace construction. `@Proxy` classes accept the same typed/named construction and field destructuring by default, even without an explicit `@Constructure`/`@Destructure`. Type labels are optional; write expressions directly when their types are already known:

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

    Point p = {a, b};                  // already-typed variables
    Point q = {5, "ok"};               // positional values
    Point r = {y = "named", x = 9};    // named arguments
    Point s = {integer a, string b};   // explicit labels for disambiguation

    {integer ax, string by} = p;       // ax == 4, by == "hi!"
    return 0;
}
```

- Entries are `[A] value`, `[A] name` or `[A] name = expr`; `A` and the name are optional. Without `=`, a bare name is both the value and the match name; with `=`, the entry matches a constructor parameter name.
- Matching considers only `@Constructure` constructors: count, names, positions/value types and optional type labels. No match or ambiguity reports `E2076`.
- A target-class `@Destructure` method receives references to default-initialized bindings and writes their outputs; otherwise destructuring reads fields (through the proxy trap when present).

### 7.3 Block Retention

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

- `@Retention(Compile)` blocks run in the interpreter at compile time; their `print` output is visible with `coorc --print-consts` and is omitted from the generated program.
- `@Retention(Runtime)` blocks are not executed at compile time and run normally.
- Only `Compile`/`Runtime` are valid on blocks; `Invocation`/`enum` report `E2044`.

### 7.4 Numeric Radix

```coo
@Radix(16)
const integer MASK = 255;

integer main() {
    @Radix(2)
    integer flags = 10;
    @Radix(8)
    integer mode = 64;
    print(MASK + flags + mode); // values are still 255, 10, 64
    return 0;
}
```

- `@Radix(2|8|10|16)` is a type annotation that changes only how generated C++ spells integer literals in declaration initializers, field defaults and `const` values: `0XFF`, `0B1010`, C++ octal `0100`.
- It does not change source literal parsing or runtime `print`; unsupported bases/types report `E2066`.

### 7.5 Package Dependencies

```bat
coorc init app --name app
cd app
coorc add math@^1.2
coorc install --locked
coorc build . -o out\app.exe
```

## 8 Repository Layout

| Path | Contents |
| --- | --- |
| [`libs/coord`](libs/coord) | `coorc_core`: front end, Sema, interpreter, C++ backend, driver, package manager/mirror/registry; 38 module headers under `include/coord/modules/` |
| [`tools/coorc`](tools/coorc) | CLI: compile, test, package management, mirror and diagnostics |
| [`tests`](tests) | Unit tests, CTest, e2e scripts and fixtures |
| [`examples`](examples) | Runnable `.coo` examples |
| [`lib/standard`](lib/standard) | Standard-library sources (annotations wired in) |
| [`framework.md`](framework.md) / [`framework.zh-CN.md`](framework.zh-CN.md) | Authoritative framework and language specification |
| [`docs/grammar.ebnf`](docs/grammar.ebnf) | Formal grammar |
| [`docs/archive`](docs/archive) | Historical documents; `framework.md` is authoritative |

## 9 Limitations and Known Issues

- `lambda`, `if` expressions, full pattern matching, `@Native`/FFI, `@Copy`/`@Wrap` semantics, `Weak<T>`, and `unsafe`/`new`/`delete`/`extern` are not implemented; see [framework.md](framework.md) §11.
- `ProxyPolicy` execution is verified mainly for local `T*`/`@Unique`/`@Share` variables (direct access, narrowing, local Optional destructuring) and for `@Proxy` `Skip`/`Abort`/`Error` destructuring. `@Optional` on fields/parameters/returns is parsed/validated but full policy execution is partial; `@Proxy` `Default(expr)` construction/destructuring, runtime pre-construction hooks, and `Skip` defaults for arbitrary non-default-constructible classes are still 🚧.
- `@Constructure` typed/named construction and `@Retention(Compile/Runtime)` blocks are implemented at a basic level; compile-time blocks cannot depend on runtime local state, and `Invocation`/`enum` on a block reports `E2044`. Complex constructor-overload disambiguation remains 🚧.
- Ownership and borrow checking remain a conservative subset: `&` cannot be returned/stored in fields, but cross-thread/closure escape checks are incomplete.
- Aliased local declarations `l.Payload p = ...` are syntactically ambiguous; only parameter/return type positions are fully supported.
- Packages: workspace is parsed but not installed recursively; no features, git dependencies or signatures/trust chain; `search` relies on `index.toml`.
- Cross-compilation: an MSVC host cannot cross-compile, and cross targets cannot run.
- Header changes require a clean rebuild; on Windows, Ninja cannot reliably parse MSVC's localized `showIncludes` output.
- Test counts describe the verified baseline at the time of writing; rerun `ctest` for your environment.
- The C++ backend is a single translation unit; large-project/multi-module linking and the full standard-library API are still in progress.

## 10 Documentation

- Framework and language specification: [framework.md](framework.md) / [framework.zh-CN.md](framework.zh-CN.md)
- Chinese QuickStart: [readme.zh-CN.md](readme.zh-CN.md)
- Formal grammar: [docs/grammar.ebnf](docs/grammar.ebnf)
- Historical archive: [docs/archive/](docs/archive/) (unmaintained)
