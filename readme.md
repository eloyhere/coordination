# Coordination QuickStart

> This document covers QuickStart, the CLI, the toolchain and package-manager usage. The authoritative language and framework definition is [framework.md](framework.md).

Coordination is a statically typed systems language with **no macros**: compile-time metaprogramming is driven by annotations, descriptors, reflection and proxies, while object lifetime reuses C++ RAII and the current backend emits C++20.

- Source files `.coo`; module interfaces `.coox`; compiler `coorc`.
- Package manifests `coordination.toml` / `coord.pkg`; lockfile `coord.lock`.
- Current baseline: **214 CTest cases**, with **214/214 passing on Windows**; on Linux (WSL Ubuntu, g++ 15), `coord_unit`, `coord_e2e_packages` and all six native-bridge tests pass as well (Linux registers 8 of the 214 tests).
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

- `void main()`, `fix main()` and any other parameter form report `E2052`;
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
- `ctest` registers **214 tests** (1 unit test plus 213 e2e tests); `coord_e2e_packages` needs neither a host C++ compiler nor network, while `coord_e2e_packages_build` and the other e2e tests need a working host C++ toolchain. The native bridge slice adds `coord_e2e_native_config`, `coord_e2e_native_annotations`, `coord_e2e_native_c_functions`, `coord_e2e_native_cpp_class`, `coord_e2e_native_cross` and `coord_e2e_native_unsafe`.
- Native example projects under `examples/native` build their own objects first (`native/build.cmd` or `native/build.sh`) and then run `coorc run .`; the compiler and target come from their `coordination.toml`.
- Sanitizers: `cmake -S . -B out/build/asan -G Ninja -DCOORD_SANITIZE=ON` builds with ASan + UBSan; `linux-sanitize` preset is provided. Run tests with `ASAN_OPTIONS=detect_leaks=0` when leak checks are not relevant.
- Parser/compiler fuzzing: build with Clang and `-DCOORD_FUZZ=ON`; see [tests/fuzz/README.md](tests/fuzz/README.md). The parser fuzzer caught unbounded diagnostics; the compiler fuzzer exercises Sema/emitter and requires the `Checker` to outlive emission.
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
- Native projects add `[build]`, `[toolchain.<target>]` and `[native]`; the manifest is the only source of compiler/target truth. `coord.pkg` accepts the same settings as line fields: `target`, `cc`, `cxx`, `sysroot`, `cflag`/`cxxflag`/`ldflag` and `native-include`/`native-object`/`native-library`:

```toml
[build]
target = "x86_64-unknown-linux-gnu"

[toolchain.x86_64-unknown-linux-gnu]
cc = "/usr/bin/clang"          # required
cxx = "/usr/bin/clang++"       # required; compiles the generated bridge + program
sysroot = ""                   # optional
cflags = []                    # optional (project build hints)
cxxflags = []                  # optional; passed to the bridge/program compile
ldflags = []                   # optional; passed to the link step

[native]
include = ["native/include"]         # header search paths (project-relative)
objects = ["native/build/foo.o"]     # produced by the project's own build system
libraries = ["m"]                    # names ((m, user32) or paths to .a/.lib/.so)
```

- `@Native(file: "native/include/foo.h", abi: "c" | "c++", symbol: "c_name")` only declares the header and ABI (`symbol` defaults to the declaration name; `abi: "c++"` is required for a class). See §7.7 for the bridge model.
- Fail closed: missing `[build].target`, missing/incomplete `[toolchain.<target>]`, missing objects/libraries/includes or headers and target/flag conflicts with the command line are hard errors (`E2112`–`E2119`); `coorc` never guesses a compiler from `PATH` and never downloads one. Single-file `@Native` use reports `E2112`; the old `@Native("symbol")` form reports `E2114`.

### 5.2 Trust Policies and Install

`coordination.toml` may declare a trust policy:

```toml
[trust]
require_checksum = true      # reject registry entries without SHA-256
allow_insecure_http = false  # refuse non-loopback http:// registries
```

Loopback HTTP (`localhost`, `127.0.0.1`, `[::1]`) and `file://` registries are always allowed. Policy failures are reported before download; checksum mismatches remove the cached archive.

### 5.3 Install, Lock and Vendor

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

### 5.4 Registries

A registry is a directory tree (local directory or HTTP(S) root):

```text
index.toml
index/<name>.toml
packages/<name>/<version>.tar.gz
health
```

`coorc pack` emits a deterministic `.tar.gz` (sorted entries, mtime 0, gzip stored blocks); `coorc publish` writes a local registry or PUTs to a remote one through system `curl`; credentials come from `COORD_TOKEN`. Archive SHA-256 is verified before extraction.

### 5.5 Mirrors

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

### 5.6 Cache

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
- Compilation is two steps: compile each translation unit to an object (`/nologo /std:c++20 /EHsc /utf-8 /c <src> /Fo<obj>` for MSVC; `-std=c++20 -O2 -pthread -c <src> -o <obj>` for GCC/Clang), then link (`cl /nologo <objs> /Fe<exe>`; `cxx <objs> -o <exe>`). A native project adds `-I`/`/I` include dirs, `cxxflags` to both compiles, and the declared objects/libraries plus `ldflags` to the link. `--cxx-flavor` overrides auto-detection; `--verbose` prints every command line.

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
coorc build examples\native\c_functions        # target/toolchain from coordination.toml
coorc build src\main.coo --target x86_64-unknown-linux-gnu --sysroot C:\sysroots\linux
coorc build src\main.coo --cxx C:\toolchains\aarch64-linux-gnu\bin\aarch64-linux-gnu-g++
```

- A native project takes `[build].target` and `[toolchain.<target>]` from `coordination.toml`; the same configured `cxx` compiles the main translation unit and the generated bridge. A `--target`/`--cxx`/`--sysroot` that contradicts the manifest is `E2117`; a missing toolchain is `E2116`.
- Outside native projects a clang host receives `--target`; GCC cross toolchains are supplied via `--cxx`; `--sysroot` passes a sysroot.
- MSVC cannot cross-compile; cross targets cannot `coorc run` — only `check`/`build`/`--emit-cpp`. `--emit-cpp` also writes `<output>.native.cpp`, which is the compile-only proof of a foreign target.

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

### 7.4 Numeric Radix and Static Fixed Point

```coo
@Power(4) fix convert(fix value) {
    return value;                    // return target @Power(4) converts
}

integer main() {
    @Power(2) fix price = 1.5;
    @Power(4) fix fine = price;      // target annotation widens power
    @Radix(2) @Power(4) fix q = price; // target annotation changes radix
    @Length(16) @Power(2) fix wide = price;
    @Power(2) fix one = 1.0;
    @Power(2) fix low = one / 3;     // 0.33 at the natural power
    @Power(4) fix high = one / 3;    // 0.3333: target overrides precision
    print(fine);
    print(q);                        // 1.1 in base 2
    print(wide);
    print(convert(2));
    print(low);                      // 0.33
    print(high);                     // 0.3333
    return 0;
}
```

- `fix` is static fixed point: `raw = semantic × radix^power`, represented as `coord_rt::Fix<Power, Radix, Length>` with one signed 8/16/32/64/128-bit raw integer (no runtime scale). Defaults are `@Length(64) @Radix(10) @Power(0)`.
- `@Power` on an `integer` or `float` declaration promotes it to `fix`; `@Radix` alone does not. A fractional literal needs an explicit `@Power` to initialize a `fix` (otherwise `E2103`); it is encoded exactly at compile time, rounding half away from zero beyond the target scale. A `@Length(128)` literal is encoded into a signed 128-bit raw (e.g. `@Length(128) @Power(0) fix max = 170141183460469231731687303715884105727.0;`) and a literal beyond that range reports `E2105`.
- Fix-to-fix conversion is target-driven: annotate the destination variable/field/parameter/return type (`@Power(4) fix fine = price;`, `@Radix(2) @Power(4) fix q = price;`, `@Length(16) ...`). A **copy between identical canonical shapes** (`@Length`/`@Radix`/`@Power` all equal, including the default `fix`) is a plain storage copy and needs no target annotation; a conversion that changes the shape without a target annotation reports `E2109`. The same rule applies to fields, parameters, return values, array/queue literals and map values, so no implicit cross-storage fix conversion is introduced by container operations. There are no `toRadix`/`toScale` methods; `toInteger()`/`toFloat()` only cross the fix storage boundary.
- All operators work: `+ - * / %`, unary `-`, `++/--`, comparisons, `& | ^ ~ << >>`. Arithmetic/comparison is semantic (`+`/`-` align to the finer power, `*` adds powers, `/` uses `max(power1,power2)` and an explicit target can override it, `%` keeps the dividend sign); bitwise/shift operates on `raw` and requires identical radix/power/length (`E2111`).
- `@Length` supports 8/16/32/64/128 with result-width widening (`+`/`-`: `max(len1,len2)+1`, `*`: `len1+len2`; widths below 128 keep their previous result widths and a 128-bit operand widens the result to 128). A `*` or `/` assigned to an explicitly annotated target uses wider intermediates: 64×64→128 for the old widths and a portable 256-bit intermediate for 128×128, so a narrower target scale is produced without truncation.
- The `@Length(128)` raw is a signed two's-complement 128-bit integer: the C++ backend uses a custom two-limb `coord_rt::UInt128` as the bit pattern with signed helpers, `__int128` on GCC/Clang for the 64×64→128 primitive and `_mul128` on 64-bit MSVC (portable 32-bit split fallback elsewhere); no non-standard library is required. Overflow, division by zero and out-of-range conversions are checked and abort with a `fix ...` diagnostic. The static `@Power` scale constant must still fit a signed 64-bit integer, so e.g. radix-10 `@Power(20)` remains `E2102` even for `@Length(128)`.
- `print` renders the semantic value in the type's radix (2–36) and drops trailing zero digits.
- `@Radix` accepts any integer `>= 2`; `0`, `1` and negatives report `E2100`. For `integer` it only changes generated C++ literal spelling for bases 2/8/16 (`0XFF`, `0B1010`, C++ octal `0100`); other bases spell decimal, and runtime `print` of an integer is unchanged.

### 7.5 Annotation Processors

Annotations generate declarations by implementing `AnnotationProcessor` and overriding `process(Description)`; the old `@Processor` method annotation has been removed:

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

- `Processor` is the root marker; `AnnotationProcessor`, `ProxyHandler` and `InvocationHandler` extend it. Only annotation declarations may implement `AnnotationProcessor` (`E2060`); exactly one `override void process(Description)` is required (`E2058`/`E2059`/`E2060`).
- An annotation may implement `AnnotationProcessor` and `ProxyHandler` together (compile-time generation plus runtime traps); see [`examples/annotation_processor.coo`](examples/annotation_processor.coo).
- Available generation primitives: `addFunction`, `addFunctionReturning`, `addConstant`, `addAnnotatedFunction`, plus `line(file, line)`.

### 7.6 Package Dependencies

```bat
coorc init app --name app
cd app
coorc add math@^1.2
coorc install --locked
coorc build . -o out\app.exe
```

### 7.7 Native Bridge (C and C++)

Native code is declared with `@Native` and configured in the manifest; `coorc`
generates an ABI-compatible C bridge and links what the project built. The
checked-in projects live under [`examples/native`](examples/native).

```coo
// examples/native/cpp_class/src/main.coo
@Native(file: "native/include/counter.h", abi: "c++")
class Counter {
public:
    Counter();
    Counter(integer initial);
    ~Counter();
    integer value();
    void increment();
    integer add(integer delta);
    string label();
}

@Unsafe
integer main() {
    Counter counter = Counter(10);
    counter.increment();
    print(counter.value());   // 11
    print(counter.add(5));    // 16
    print(counter.label());   // counter
    return 0;
}
```

```bat
cmake --preset x64-debug
cmake --build out\build\x64-debug
cd examples\native\cpp_class
native\build.cmd                :: project-owned native build -> native\build\counter.obj
coorc run .
```

Responsibilities split:

| Layer | Responsibility |
| --- | --- |
| `coordination.toml` | target triple, `cc`/`cxx`, sysroot/flags, native include dirs, objects and libraries; the only source of compiler truth |
| `@Native(file:, abi:, symbol:)` | header and ABI of one body-less function or class; `symbol` defaults to the declaration name |
| project build system | compiles the user `.c`/`.cpp` into objects/libraries (coorc never does) |
| generated bridge (`<program>.native.cpp`) | `extern "C"` wrappers; C functions called directly, strings as `const char*`, C++ classes through a move-only opaque handle with external `new (std::nothrow)`/`delete`; no `try`/`catch`, no exception leakage |
| generated program | calls the bridge and converts values (null `const char*` → empty string, native handle → value class) |

Fail-closed behavior: missing `[build].target`, missing/incomplete
`[toolchain.<target>]`, missing objects/libraries/includes or headers and
command-line target conflicts are errors (`E2112`–`E2119`) — never a fallback
to the host compiler. Native calls stay behind `@Unsafe` (`E2096`).

## 8 Repository Layout

| Path | Contents |
| --- | --- |
| [`libs/coord`](libs/coord) | `coorc_core`: front end, Sema, interpreter, C++ backend, driver, package manager/mirror/registry; 38 module headers under `include/coord/modules/` |
| [`tools/coorc`](tools/coorc) | CLI: compile, test, package management, mirror and diagnostics |
| [`tests`](tests) | Unit tests, CTest, e2e scripts and fixtures |
| [`examples`](examples) | Runnable `.coo` examples |
| [`lib/standard`](lib/standard) | Standard-library sources (`annotations.coo` and `utility.coo` wired in) |
| [`framework.md`](framework.md) / [`framework.zh-CN.md`](framework.zh-CN.md) | Authoritative framework and language specification |
| [`docs/grammar.ebnf`](docs/grammar.ebnf) | Formal grammar |
| [`docs/archive`](docs/archive) | Historical documents; `framework.md` is authoritative |

## 9 Limitations and Known Issues

- Lambda has a subset: function-type targets, expression/block bodies, implicit/explicit captures (`[]`/`[=]`/`[&]`/`[x]`/`[&x]`/`[this]`), capture validation (`E2087`), read-only explicit by-value captures (`E2088`), by-reference closure escape checks (`E2039`) for returns, field/container-literal stores and outer-scope variables, initialization/assignment, calls, arguments and returns. Omitted capture lists produce mutable by-value copies. Functional-interface targets are available as a function-value subset: `Function`/`Consumer`/`Predicate`/`Supplier`/`BiFunction` from `lib/standard/utility.coo` lower to function types (generic arguments substituted, `.apply`/`.accept`/`.test`/`.get` call forms); nominal interface values and classes implementing a `@FunctionalInterface` are ⏳ (`E2004`/`E2089`), and `@FunctionalInterface` is deprecated until the Lambda module structures `operator()` (slice 12). Unbound instance method references and function-pointer targets are ⏳; thread-entry capture is checked by `Thread` creation (`E2090`), while the rest of cross-thread borrow analysis is ⏳; static (`Type::method`) and bound (`object::method`) method references, calls through function-typed fields and chained calls on returned function values work, with bound receivers following the by-reference escape rules. `if` expressions support expression branches (`if (c) a else b` and `else if` chains); block branches are ⏳. `match` supports constants, ranges, or-patterns, bindings and guards, with enumerate/boolean/scalar exhaustiveness; type patterns and class destructuring are ⏳. `@Native`/FFI is project-configured: `@Native(file:, abi:, symbol:)` declares the header/ABI only, `[build]`/`[toolchain.<target>]`/`[native]` in `coordination.toml` provide the compiler/target/objects/libraries, and `coorc` generates an `extern "C"` bridge (C function wrappers, `const char*` strings, move-only opaque C++ class handles, no `try`/`catch`). The old `@Native("symbol")` form is `E2114`; single-file use is `E2112`. See §7.7 and `examples/native`. `@Copy`/`@Wrap`, `Weak<T>` and `unsafe`/`new`/`delete`/`extern` remain ⏳; `@Wrap` is deferred (declaration removed); see [framework.md](framework.md) §11.
- `ProxyPolicy` execution covers local `T*`/`@Unique`/`@Share` variables, Optional parameters, fields, returns, and local Optional destructuring, plus `@Proxy` `Skip`/`Abort`/`Error` destructuring. `@Proxy` `Default(expr)` construction/destructuring, runtime pre-construction hooks, and `Skip` defaults for arbitrary non-default-constructible classes are still 🚧.
- `@Constructure` typed/named construction and `@Retention(Compile/Runtime)` blocks are implemented at a basic level; compile-time blocks cannot depend on runtime local state, and `Invocation`/`enum` on a block reports `E2044`. Complex constructor-overload disambiguation remains 🚧.
- Atomic v0: a built-in non-copyable `Atomic` lowers to `std::atomic<std::int64_t>` with `load()`, `store(v)`, `fetchAdd(v)`/`fetchSub(v)` (return the previous value), `exchange(v)` (returns the previous value) and `compareExchange(expected, desired)` (returns true when the value equalled `expected` and was replaced; `expected` is not updated). All operations are seq_cst with no memory-order parameters; there is no `Atomic<T>`, floating-point/pointer atomic, condition variable or data-race static analysis. Locals and class fields are supported; parameters/returns are ⏳. Copying or assigning reports `E2092`.
- ScopedThread v0: a built-in move-only `ScopedThread` lowers to `std::jthread` and automatically joins in its destructor (leaving the declaring block waits for the thread). It supports `join()`/`detach()`/`joinable()` and the same cross-thread capture rules as `Thread` (`E2090`); copies/assignments are rejected and it is local-only. There is no `stop_token`/`requestStop`, cancellation or cross-thread exception propagation.
- Safe/Unsafe v0: code is safe by default; `@Unsafe` may mark a module (`@Unsafe;`), class, function, method, constructor or block and propagates downwards, while `@Safe` overrides. `@Unsafe` only unlocks raw-pointer dereference, pointer arithmetic, `@Native`/FFI calls and unsafe casts — type/borrow/bounds checks stay active. Calling `@Unsafe`/`@Native` from safe scope reports `E2096`; `@Unsafe { ... }` blocks are allowed inside `@Safe` code as audit boundaries. v0 enforces pointer dereference and FFI calls first.\n- Copy v0: classes are not implicitly copyable when they contain `@Unique`/`@Share`/pointer fields; they must opt in with `@Copy(Deep)` (value fields + deep-copyable `@Unique` chain; rejects `@Share`/raw pointers/refs/cycles) or `@Copy(Shallow)` (member-wise copy, allows `@Share` and raw pointers, forbids `@Unique`). Deep-copyability and the need for explicit `@Copy` are checked recursively through value fields and base classes. Pure value classes keep implicit copies for compatibility. `@Copy` errors report `E2095`; a `[x]` lock capture of a class requires `@Copy(Deep)`.
- ScopedLock/@Lock v0: `ScopedLock guard = ScopedLock(mutex);` is an RAII `std::lock_guard<std::mutex>` for a named `Mutex` lvalue and releases on every scope exit; it is non-copyable (`E2092`) and local-only. `@Lock(Exclusive, mutex, [captures]) { ... }` is the block form: `[x]` copies primitive/string/enum values (class value captures rejected), `[&x]` is a mutable borrow and `[const&x]` a read-only borrow whose calls must target `@ReadOnly` methods; writes to uncaptured/`const&` objects and capability errors report `E2094`, escaping captures report `E2039`, and the legacy `@Lock(mutex)` form reports `E2094`. Only direct calls/writes are checked (no alias/data-race proof); uncaptured class objects only allow `@ReadOnly` calls, nested `@Lock` is rejected (`E2094`), and `[x]` copies may be returned while borrow captures cannot escape (`E2039`); there is no timeout, try-lock or reentrancy check: nested locking of the same mutex deadlocks.
- Mutex v0: a built-in non-copyable `Mutex` lowers to `std::mutex` with manual `lock()`/`unlock()`/`tryLock()`; copying or assigning reports `E2091`. There is no RAII guard and no static data-race analysis; forgetting `unlock()` blocks other threads. `Mutex` works as a local or class field (share the owning object with `@Share`); parameters/returns are ⏳.
- Thread v0: a built-in move-only `Thread` is created from a `() -> void` entry with `join()`/`detach()`/`joinable()` and `hardwareConcurrency()`. Entries must own their captures (`[]`, `[=]`, `[x]`, `@Share` copies); `[&]`, `[&x]`, `[this]` and call-scoped function values are rejected with `E2090`. A joinable thread must be joined or detached before destruction; shared mutable state is not data-race safe, and synchronization primitives, scoped threads and non-local `Thread` positions are ⏳.
- Ownership and borrow checking remain a conservative subset: `&` cannot be returned, stored in fields, or used as a container element; by-reference closures cannot be returned, stored in fields/container literals or assigned to outer-scope variables (`E2039`), while full cross-thread borrow analysis and lifetime analysis are incomplete. Function-typed parameters are call-scoped borrows: returning or storing them is rejected. `@Share` cycles have type-graph, self-assignment and runtime edge-graph checks; in-class field initializers and some aliases are not tracked.
- Aliased local declarations `l.Payload p = ...` are syntactically ambiguous; only parameter/return type positions are fully supported.
- Packages: workspace is parsed but not installed recursively; no features, git dependencies or signatures/trust chain. `[trust] require_checksum` and non-loopback HTTP refusal are implemented; `search` relies on `index.toml`.
- Cross-compilation: native projects are driven by `[build].target` + `[toolchain.<target>]` (same `cxx` for program and bridge); a foreign target emits main + bridge compile-only with `--emit-cpp`. Linking a foreign target needs an installed cross toolchain, an MSVC host cannot cross-compile, and cross targets cannot run.
- Native bridge leftovers: an object built for another target is only caught by the linker; `--incremental` does not track declared native headers (only sources, manifest, objects and toolchain configuration); state/error-code → Result proxy mapping, user C++ exceptions (the no-throw contract is not enforced), callbacks and single-file `@Native` use remain ⏳. The configured toolchain, include dirs, objects and libraries are passed to the compiler/linker verbatim.
- Header changes require a clean rebuild; on Windows, Ninja cannot reliably parse MSVC's localized `showIncludes` output.
- Test counts describe the verified baseline at the time of writing; rerun `ctest` for your environment.
- The C++ backend is a single translation unit; large-project/multi-module linking and the full standard-library API are still in progress.

## 10 Documentation

- Framework and language specification: [framework.md](framework.md) / [framework.zh-CN.md](framework.zh-CN.md)
- Chinese QuickStart: [readme.zh-CN.md](readme.zh-CN.md)
- Formal grammar: [docs/grammar.ebnf](docs/grammar.ebnf)
- Historical archive: [docs/archive/](docs/archive/) (unmaintained)




