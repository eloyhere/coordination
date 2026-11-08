# Coordination

**English** | [简体中文](README.zh-CN.md)

A compiled systems language and the toolchain that will build it — currently bootstrapping itself in C++20.

> **Status: pre-alpha.** The specification, the architecture and the host implementation are all works in progress. Syntax, file formats and internal ABIs may change without notice.

## Overview

Coordination is an ahead-of-time compiled language for systems and tooling work. It targets Windows, Linux and macOS on x86-64 and arm64, and it ships a command-line toolchain (`coord`) rather than an IDE or a graphical installer.

Four rules are fixed and drive the rest of the design:

| Rule | Meaning |
| --- | --- |
| **R1** | Ahead-of-time compilation only — no JIT, no bytecode interpreter |
| **R2** | `interface` and `type` exist at compile time only; an interface may inherit from several interfaces, a class from exactly one class |
| **R3** | Template metaprogramming is a first-class feature (`concept`, `comptime`) |
| **R4** | No `#include`, no preprocessor macros and no `namespace`; modules expose symbols through `import` / `export` |

The guiding principles are zero-cost abstraction, errors decided at compile time, a statically analysable dependency graph — because there are no macros, the import graph is complete as soon as parsing finishes — and cross-platform support treated as a first-class concern rather than as one source tree per platform.

What sits in the repository today is the **stage-0 host implementation**: the C++20 library and front end that the self-hosted toolchain will eventually replace, alongside the language specification and the design record behind it.

## Repository layout

| Path | Contents |
| --- | --- |
| `structure/` | Host C++ library — tree nodes, binary search / AVL / red-black trees, skip lists, LRU caches, an object pool |
| `syntax/` | C++ prototype of the language's syntax tree: nodes, tokens, spans and a printer |
| `compiler/` | Front end — source manager, lexer, parser, AST, structured diagnostics, and a driver that ties them together |
| `tests/` | CTest suites for every component, including differential fuzz tests across the three tree implementations |
| `cmake/` | Shared CMake helpers (`CompilerWarnings.cmake`) |
| `docs/` | Language specification, architecture, process, lifecycle and background research |

## Requirements

- CMake 3.21 or newer; the bundled presets need 3.25 or newer
- A compiler with solid C++20 support — MSVC 2022, GCC 11 or newer, Clang 13 or newer
- Ninja, which every preset uses as its generator
- On Windows, a Visual Studio developer environment: `cl.exe` must be on `PATH` and `VSINSTALLDIR` must be set

## Building

The presets cover every supported platform. Each one configures into `out/build/<preset>` and installs into `out/install/<preset>`.

```sh
cmake --preset x64-debug
cmake --build --preset x64-debug
```

| Platform | Presets |
| --- | --- |
| Windows | `x64-debug`, `x64-release`, `x86-debug`, `x86-release` |
| Linux | `linux-debug`, `linux-release` |
| macOS | `macos-debug`, `macos-release` |

A plain out-of-tree build works too:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Either route produces the sample executable `coordination` from the `coordination_demo` target.

## Running the tests

```sh
ctest --preset x64-debug
```

Every `tests/test_*.cpp` file is picked up automatically and turned into an executable of the same name plus a matching CTest test; `CONFIGURE_DEPENDS` re-runs the glob whenever files are added or removed. A suite prints `PASS: <name>` when it succeeds and fails loudly with the offending expression otherwise.

## Using the library

The library is header-only. The `coordination::coordination` interface target carries the include directory — the repository root — together with the C++20 requirement, so headers are included by their path from that root:

```cpp
#include "structure/tree/AVLTree.h"

#include <iostream>

int main()
{
    coordination::tree::AVLTree<int> tree{};
    tree.insert(3);
    tree.insert(1);
    tree.insert(2);

    std::cout << tree.size() << '\n';      // 3
    std::cout << *tree.min() << '\n';      // 1
    std::cout << tree.validate() << '\n';  // 1
}
```

Bring it into a CMake build in-tree:

```cmake
add_subdirectory(third_party/coordination)
target_link_libraries(my_app PRIVATE coordination::coordination)
```

or with `FetchContent`:

```cmake
include(FetchContent)

FetchContent_Declare(coordination
    GIT_REPOSITORY https://github.com/eloyhere/coordination.git
    GIT_TAG main)

FetchContent_MakeAvailable(coordination)

target_link_libraries(my_app PRIVATE coordination::coordination)
```

## Installing

```sh
cmake --install out/build/x64-debug --prefix /some/prefix
```

That installs the `structure/` and `syntax/` headers and exports the `coordination::` targets file to `lib/cmake/coordination/coordinationTargets.cmake`. A `coordinationConfig.cmake` package file is not generated yet, so `find_package(coordination)` does not work today — include the exported targets file directly until it does.

## A first look at the language

```coordination
// A minimal module.
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

Three conventions are worth noting at this point: identifiers never contain underscores (upper camel case for types, lower camel case for everything else), statements always end with `;` so line breaks carry no meaning, and functions are declared without an `fn` keyword. The specification has the full set of rules.

## Documentation

| Document | Subject |
| --- | --- |
| [docs/language-spec.md](docs/language-spec.md) | Draft specification — types, interfaces, classes, generics, modules |
| [docs/architecture.md](docs/architecture.md) | Layering, key decisions, front end, IR, back end and toolchain components |
| [docs/process.md](docs/process.md) | Roles, change levels, RFC process, testing and release flow |
| [docs/lifecycle.md](docs/lifecycle.md) | Versioning, release channels, compatibility charter |
| [docs/research/](docs/research/) | Background research that informed the design |

All of this material is written in Chinese for the time being; the specification will be translated once it stabilises.

## Roadmap

| Stage | Main deliverables |
| --- | --- |
| MVP (months 0–6) | Driver, source manager and spans, lexer and parser, `import`/`export`, name resolution and type checking, HIR, basic monomorphisation, minimal `comptime`, LLVM x86-64 for Windows and Linux, `build` / `run` / `check` |
| Bootstrap (months 6–18) | MIR and optimisation, incremental caching, packages and lock files, `fmt`, `lint`, LSP, aarch64 / macOS / WASM, PDB, C ABI, standard library |
| Ecosystem (months 18–36) | Registry and signing, documentation generation, C interoperability, LTO and PGO, sanitizers, remote caching, a stable 1.0 specification |

## Contributing

The project is still too early for large external contributions, but issues and design discussion are welcome. [`docs/process.md`](docs/process.md) sets out how changes are graded and reviewed, and the build and test commands above are the same gates any change has to pass.

## Licence

No licence has been chosen yet, so the code is currently all rights reserved. A licence will be selected before the first public release.
