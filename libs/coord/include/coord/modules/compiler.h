#pragma once

#include "coord/driver.h"
#include "coord/interp.h"
#include "coord/sema.h"

#include <cstdlib>
#include <ostream>
#include <string>

namespace coord {

/// Semantic-analysis module alias: `Sema` is the only spelling.
using Sema = Checker;

/// Front-end + lowering + host invocation orchestration.
class Compiler {
public:
  int compile(const CompileOptions& options, std::ostream& out,
              std::ostream& err) const {
    return runCompiler(options, out, err);
  }
};

/// Compile-time evaluator facade over Interpreter.
class Evaluator {
public:
  Evaluator(ModuleAst& module, DiagnosticEngine& diagnostics)
      : interpreter_(module, diagnostics) {}

  void evaluate() { interpreter_.evaluate(); }
  [[nodiscard]] const std::map<std::string, Value>& constValues() const {
    return interpreter_.constValues();
  }
  [[nodiscard]] const std::string& compileTimeOutput() const {
    return interpreter_.compileTimeOutput();
  }

private:
  Interpreter interpreter_;
};

/// Host C++ compiler/linker boundary.
class Linker {
public:
  static int link(const std::string& compiler, const std::string& cppPath,
                  const std::string& executablePath,
                  const std::string& objectPath, std::ostream& err) {
    const std::string program = compiler.find(' ') == std::string::npos
                                    ? compiler
                                    : quote(compiler);
#ifdef _WIN32
    const std::string command =
        program + " /nologo /std:c++20 /EHsc /utf-8 " + quote(cppPath) +
        " /Fe" + quote(executablePath) + " /Fo" + quote(objectPath);
#else
    const std::string command = program + " -std=c++20 -O2 " + quote(cppPath) +
                                " -o " + quote(executablePath);
#endif
    const int result = std::system(command.c_str());
    if (result != 0) {
      err << "coorc: host C++ compilation failed (exit " << result << ")\n";
      err << "coorc: command: " << command << '\n';
      err << "coorc: on Windows run from a Visual Studio developer prompt, or "
             "pass --cxx <path-to-cl.exe>\n";
      return 3;
    }
    return 0;
  }

private:
  static std::string quote(const std::string& value) {
    return "\"" + value + "\"";
  }
};

}  // namespace coord
