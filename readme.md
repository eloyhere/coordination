# Coordination QuickStart

> This document covers QuickStart, the CLI, the toolchain and package-manager usage. The authoritative language and framework definition is [framework.md](framework.md).

Coordination is a statically typed systems language with **no macros**: compile-time metaprogramming is driven by annotations, descriptors, reflection and proxies, while object lifetime reuses C++ RAII and the current backend emits C++20.

- Source files `.coo`; module interfaces `.coox`; compiler `coorc`.
- Package manifests `coordination.toml` / `coord.pkg`; lockfile `coord.lock`.
- Current baseline: **214/214 CTest cases passing on Windows and Linux** (the same 214-test suite on both); macOS registers the same suite but was not exercised in this environment.
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

### 2.1 Numeric Representation Annotations

`@Length`, `@Radix`, `@Signed`, `@Unsigned` and `@Power` are general numeric representation annotations, not `fix`-only:

| Annotation | Meaning |
| --- | --- |
| `@Length(N)` | storage width: `integer` 8/16/32/64/128, `float` 32/64, `fix` 8/16/32/64/128 |
| `@Radix(R)` | literal/print base for `integer`/`float`; scale radix for `fix` |
| `@Signed` / `@Unsigned` | numeric signedness |
| `@Power(P)` | `fix` scale exponent; `integer`/`float` + `@Power` promotes to `fix` |

Rules and conversion details: [framework.md](framework.md) §2.2.1.

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

Test status by OS:

| OS | CTest registered | Passed | Notes |
| --- | --- | --- | --- |
| Windows x64 (MSVC 19.52 / VS 2022) | 214 | 214 | clean `out\build\coord-tests` build |
| Linux (WSL Ubuntu 26.04, g++ 15.2) | 214 | 214 | full suite, `ctest -j 8` |
| macOS | 214 expected | not run | same registration; no macOS host here |

There are no Windows-only or POSIX-only e2e tests at present. The scripts absorb the host differences (executable suffix, CRLF output, `file://` paths) instead of skipping tests; a future OS-bound test must use its own commented `if(WIN32)`/`if(UNIX)` guard.

- Windows uses presets derived from `windows-base` (Ninja + cl); Linux/macOS use `linux-release`/`macos-release`/`linux-debug`.
- `ctest` registers **214 tests** on every platform (1 unit test plus 213 e2e); the registration has no blanket compiler guard. `coord_e2e_packages` needs neither a host C++ compiler nor network, while `coord_e2e_packages_build` and the other e2e tests need a working host C++ toolchain. The native bridge slice adds `coord_e2e_native_config`, `coord_e2e_native_annotations`, `coord_e2e_native_c_functions`, `coord_e2e_native_cpp_class`, `coord_e2e_native_cross` and `coord_e2e_native_unsafe`.
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

## 5 Packages

A package is a directory with `coordination.toml` (recommended) or `coord.pkg`. A minimal manifest:

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

- `coord.lock` pins versions, sources and SHA-256; installed packages are vendored under `coord_modules/`.
- `--locked`/`--frozen`/`--offline` control reproducibility; `mirror`/`config` select registries and mirrors.
- Native projects add `[build]`, `[toolchain.<target>]` and `[native]`; the manifest is the only compiler/target source.
- Full manifest fields, resolution, registry protocol, `[trust]`, mirrors, cache and vendor rules: [framework.md](framework.md) §8.

## 6 Cross-Platform

Windows/Linux run the full 214-test suite (214/214); macOS registers the same suite but is expected/not run in this environment. The detailed matrix is [framework.md](framework.md) §6.6; host/target triples and compiler discovery are §6.5.

- Generated programs use C++20 and `wmain`/`main` as appropriate; path/file APIs are UTF-8; see [framework.md](framework.md) §9.5.
- Native cross-compilation is manifest/toolchain driven; MSVC cannot cross-compile and cross targets cannot `run`; see [framework.md](framework.md) §9.6.

## 7 Tutorial Examples

### 7.1 Optional and construction

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

`@Optional` and `ProxyPolicy` rules: [framework.md](framework.md) §4.5.2. For `@Constructure`/`@Destructure`, see §4.8.

### 7.2 Native bridge

Native projects keep their own build system for `.c`/`.cpp`, then call `coorc`:

```bat
cd examples\native\cpp_class
native\build.cmd
coorc run .
```

`coorc` never compiles user C/C++; it compiles the generated `extern "C"` bridge and links the declared objects/libraries. Full contract: [framework.md](framework.md) §7.4.

## 8 Repository Layout

| Path | Contents |
| --- | --- |
| [`libs/coord`](libs/coord) | `coorc_core`: front end, Sema, interpreter, C++ backend, driver, package manager/mirror/registry; 38 module headers under `include/coord/modules/` |
| [`tools/coorc`](tools/coorc) | CLI: compile, test, package management, mirror and diagnostics |
| [`tests`](tests) | Unit tests, CTest, e2e scripts and fixtures |
| [`examples`](examples) | Runnable `.coo` examples |
| [`lib/standard`](lib/standard) | Current standard-library sources: `annotations.coo` and `utility.coo`; `documentation.coo` is a design draft and is not loaded |
| [`framework.md`](framework.md) / [`framework.zh-CN.md`](framework.zh-CN.md) | Authoritative framework and language specification |
| [`docs/grammar.ebnf`](docs/grammar.ebnf) | Formal grammar tracking the lexer/parser |

## 9 Limitations and Known Issues

- Lambda has a subset: function-type targets, expression/block bodies, implicit/explicit captures (`[]`/`[=]`/`[&]`/`[x]`/`[&x]`/`[this]`), capture validation (`E2087`), read-only explicit by-value captures (`E2088`), by-reference closure escape checks (`E2039`) for returns, field/container-literal stores and outer-scope variables, initialization/assignment, calls, arguments and returns. Omitted capture lists produce mutable by-value copies. `@FunctionalInterface` is removed; lambdas are function values typed `(A) -> R`. Callable interfaces use `operator()`, and unbound instance references and function-pointer thunks remain 🚧. Unbound instance method references and function-pointer targets are ⏳; thread-entry capture is checked by `Thread` creation (`E2090`), while the rest of cross-thread borrow analysis is ⏳; static (`Type::method`) and bound (`object::method`) method references, calls through function-typed fields and chained calls on returned function values work, with bound receivers following the by-reference escape rules. `if` expressions support expression branches (`if (c) a else b` and `else if` chains); block branches are ⏳. `match` supports constants, ranges, or-patterns, bindings and guards, with enumerate/boolean/scalar exhaustiveness; type patterns and class destructuring are ⏳. `@Native`/FFI is project-configured: `@Native(file:, abi:, symbol:)` declares the header/ABI only, `[build]`/`[toolchain.<target>]`/`[native]` in `coordination.toml` provide the compiler/target/objects/libraries, and `coorc` generates an `extern "C"` bridge (C function wrappers, `const char*` strings, move-only opaque C++ class handles, no `try`/`catch`). The old `@Native("symbol")` form is `E2114`; single-file use is `E2112`. See §7.7 and `examples/native`. `@Wrap` and `unsafe`/`new`/`delete`/`extern` remain ⏳; `@Copy` and `Weak<T>` are implemented; `@Wrap` is deferred (declaration removed); see [framework.md](framework.md) §11.
- `ProxyPolicy` execution covers local `T*`/`@Unique`/`@Share` variables plus basic `Skip`/`Default` on parameters, fields and returns (`coord_e2e_optional_params`/`_fields`/`_return`), and `@Proxy` `Skip`/`Abort`/`Error` destructuring. `@Proxy` `Default(expr)` construction/destructuring, runtime pre-construction hooks, and `Skip` defaults for arbitrary non-default-constructible classes are still 🚧.
- `@Constructure` typed/named construction and `@Retention(Compile/Runtime)` blocks are implemented at a basic level; compile-time blocks cannot depend on runtime local state, and `Invocation`/`enum` on a block reports `E2044`. Complex constructor-overload disambiguation remains 🚧.
- Atomic v0: a built-in non-copyable `Atomic` lowers to `std::atomic<std::int64_t>` with `load()`, `store(v)`, `fetchAdd(v)`/`fetchSub(v)` (return the previous value), `exchange(v)` (returns the previous value) and `compareExchange(expected, desired)` (returns true when the value equalled `expected` and was replaced; `expected` is not updated). All operations are seq_cst with no memory-order parameters; there is no `Atomic<T>`, floating-point/pointer atomic, condition variable or data-race static analysis. Locals and class fields are supported; parameters/returns are ⏳. Copying or assigning reports `E2092`.
- ScopedThread v0: a built-in move-only `ScopedThread` lowers to `std::jthread` and automatically joins in its destructor (leaving the declaring block waits for the thread). It supports `join()`/`detach()`/`joinable()` and the same cross-thread capture rules as `Thread` (`E2090`); copies/assignments are rejected and it is local-only. There is no `stop_token`/`requestStop`, cancellation or cross-thread exception propagation.
- Safe/Unsafe v0: code is safe by default; `@Unsafe` may mark a module (`@Unsafe;`), class, function, method, constructor or block and propagates downwards, while `@Safe` overrides. `@Unsafe` only unlocks raw-pointer dereference, pointer arithmetic, `@Native`/FFI calls and unsafe casts — type/borrow/bounds checks stay active. Calling `@Unsafe`/`@Native` from safe scope reports `E2096`; `@Unsafe { ... }` blocks are allowed inside `@Safe` code as audit boundaries. v0 enforces pointer dereference and FFI calls first.
- Copy v0: classes are not implicitly copyable when they contain `@Unique`/`@Share`/pointer fields; they must opt in with `@Copy(Deep)` (value fields + deep-copyable `@Unique` chain; rejects `@Share`/raw pointers/refs/cycles) or `@Copy(Shallow)` (member-wise copy, allows `@Share` and raw pointers, forbids `@Unique`). Deep-copyability and the need for explicit `@Copy` are checked recursively through value fields and base classes. Pure value classes keep implicit copies for compatibility. `@Copy` errors report `E2095`; a `[x]` lock capture of a class requires `@Copy(Deep)`.
- ScopedLock/@Lock v0: `ScopedLock guard = ScopedLock(mutex);` is an RAII `std::lock_guard<std::mutex>` for a named `Mutex` lvalue and releases on every scope exit; it is non-copyable (`E2092`) and local-only. `@Lock(Exclusive, mutex, [captures]) { ... }` is the block form: `[x]` copies primitive/string/enum values and class values require `@Copy(Deep)` (`E2094` otherwise), `[&x]` is a mutable borrow and `[const&x]` a read-only borrow whose calls must target `@ReadOnly` methods; writes to uncaptured/`const&` objects and capability errors report `E2094`, escaping captures report `E2039`, and the legacy `@Lock(mutex)` form reports `E2094`. Only direct calls/writes are checked (no alias/data-race proof); uncaptured class objects only allow `@ReadOnly` calls, nested `@Lock` is rejected (`E2094`), and `[x]` copies may be returned while borrow captures cannot escape (`E2039`); there is no timeout, try-lock or reentrancy check: nested locking of the same mutex deadlocks.
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
- Historical archive: removed; [framework.md](framework.md) is the single source.




