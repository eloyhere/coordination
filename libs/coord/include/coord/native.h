#pragma once

// Native bridge: project-driven C/C++ interop.
//
// `@Native(file: "native/include/foo.h", abi: "c")` only declares the header
// and ABI of a native declaration. The compiler and linker come exclusively
// from the project manifest:
//
//   [build]
//   target = "x86_64-unknown-linux-gnu"
//   [toolchain.x86_64-unknown-linux-gnu]
//   cc = "/usr/bin/clang"
//   cxx = "/usr/bin/clang++"
//   [native]
//   include = ["native/include"]
//   objects = ["native/build/foo.o"]
//   libraries = ["m"]
//
// Coordination never compiles user `.c`/`.cpp` files: the project's own build
// system produces the objects/libraries. coorc generates an ABI-compatible
// bridge translation unit, compiles it with the configured `cxx` for the
// configured target, and links the configured objects/libraries. Any missing
// or contradictory configuration is a hard error; there is no fallback to the
// host default compiler, `PATH`, header preludes or downloads.

#include "coord/ast.h"
#include "coord/modules/manifest.h"

#include <filesystem>
#include <string>
#include <vector>

namespace coord::native {

/// One hard configuration error. `code` is a diagnostic code (E2112-E2119) and
/// `decl`, when set, supplies the source span for rendering.
struct Issue {
  std::string code;
  std::string message;
  const Decl* decl = nullptr;
};

/// Resolved native project configuration.
struct Project {
  bool active = false;
  bool legacyManifest = false;
  std::filesystem::path root;      // manifest directory
  std::filesystem::path manifest;  // manifest file
  std::string target;              // [build].target
  std::string cc;
  std::string cxx;
  std::string sysroot;
  std::vector<std::string> cxxflags;
  std::vector<std::string> ldflags;
  std::vector<std::filesystem::path> includes;   // resolved directories
  std::vector<std::filesystem::path> objects;    // resolved object files
  std::vector<std::string> libraries;            // names or resolved paths
};

/// Free functions and classes that need a generated bridge, in declaration
/// order (deterministic).
[[nodiscard]] std::vector<const Decl*> nativeDeclarations(
    const ModuleAst& module);

[[nodiscard]] bool hasNativeDeclarations(const ModuleAst& module);

/// Resolve and validate `[build]`, `[toolchain.<target>]` and `[native]`.
/// Appends one issue per problem. `project.root`/`manifest` are always set.
[[nodiscard]] bool resolveProject(const package::Manifest& manifest,
                                  Project& project,
                                  std::vector<Issue>& issues);

/// Reject command-line overrides that contradict the project configuration.
[[nodiscard]] bool validateOptions(const Project& project,
                                   const std::string& targetOption,
                                   const std::string& cxxOption,
                                   const std::string& sysrootOption,
                                   std::vector<Issue>& issues);

/// True for the types allowed across the bridge: integer, boolean, float,
/// string and void (128-bit integers are rejected because the C ABI of the
/// bridge uses fixed-width scalar types).
[[nodiscard]] bool isAbiTypeSupported(const Type* type, int length);

/// C ABI spelling of a Coo type inside the bridge (e.g. `std::int64_t`,
/// `bool`, `double`, `const char*`, `void`). Empty when unsupported.
[[nodiscard]] std::string abiType(const Type* type, int length);

/// Bridge symbol names, shared by the C++ emitter and the bridge emitter.
/// `index` is the position in `nativeDeclarations`; member indices are the
/// position inside the class declaration's member list. Numbered symbols keep
/// the bridge collision-free without a name mangling scheme.
[[nodiscard]] std::string bridgeFunctionSymbol(std::size_t index,
                                               const Decl& function);
[[nodiscard]] std::string bridgeConstructorSymbol(std::size_t classIndex,
                                                  const Decl& cls,
                                                  std::size_t memberIndex);
[[nodiscard]] std::string bridgeDestructorSymbol(std::size_t classIndex,
                                                 const Decl& cls);
[[nodiscard]] std::string bridgeMethodSymbol(std::size_t classIndex,
                                             const Decl& cls,
                                             std::size_t memberIndex,
                                             const Decl& method);

/// Position of `decl` inside `decls`; `decls.size()` when it is not a native
/// declaration.
[[nodiscard]] std::size_t nativeIndexOf(const std::vector<const Decl*>& decls,
                                        const Decl* decl);

/// Generate the bridge translation unit for `decls`. All headers must exist.
[[nodiscard]] bool generateBridge(const ModuleAst& module,
                                  const std::vector<const Decl*>& decls,
                                  const Project& project, std::string& source,
                                  std::string& error);

}  // namespace coord::native
