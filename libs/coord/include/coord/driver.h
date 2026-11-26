#pragma once

#include <iosfwd>
#include <string>

namespace coord {

enum class OutputKind {
  Check,       // front end only
  Cpp,         // emit a C++20 translation unit
  Executable,  // emit C++, invoke the C++ compiler, link an executable
};

struct CompileOptions {
  std::string inputPath;
  std::string outputPath;
  OutputKind output = OutputKind::Check;

  bool dumpTokens = false;
  bool dumpAst = false;
  bool dumpOutline = false;
  bool dumpOutlineDocs = false;
  bool printConstEval = false;
  bool warningsAsErrors = false;
  bool testMode = false;
  bool incremental = false;
  std::string testFilter;
  std::string interfacePath;

  bool runAfterBuild = false;
  bool keepCpp = false;
  bool verbose = false;
  std::string cxxCompiler;
  /// "", "msvc" or "gnu"; empty auto-detects from the compiler name.
  std::string cxxFlavor;
  /// Cross-compilation target triple; empty means the host triple.
  std::string targetTriple;
  /// Optional sysroot passed to clang/g++.
  std::string sysroot;
};

/// Compilation lifecycle:
///   1. load + lex
///   2. parse
///   3. semantic analysis (types, RAII checks, inheritance)
///   4. compile-time evaluation of const initializers
///   5. C++20 lowering
///   6. host C++ compiler + linker (Executable only)
///   7. execute (runAfterBuild only)
int runCompiler(const CompileOptions& options, std::ostream& out,
                std::ostream& err);

}  // namespace coord
