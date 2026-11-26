// libFuzzer entry point for the Coordination front end (parse + Sema + emit).
//
// Build with COORD_FUZZ=ON using Clang. The harness intentionally stops before
// invoking a host C++ compiler; it exercises parser, semantic analysis,
// interpreter constants and the C++ emitter.

#include "coord/codegen.h"
#include "coord/interp.h"
#include "coord/parser.h"
#include "coord/sema.h"
#include "coord/standard_library.h"
#include "coord/support.h"
#include "coord/syntax.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                     std::size_t size) {
  if (data == nullptr || size == 0 || size > (64U << 10U)) {
    return 0;
  }
  coord::SourceManager sources;
  coord::DiagnosticEngine diagnostics;
  const std::uint32_t fileId = sources.addFile(
      "fuzz.coo", std::string(reinterpret_cast<const char*>(data), size));
  coord::Lexer lexer(sources, fileId, diagnostics);
  coord::Parser parser(lexer.lexAll(), diagnostics);
  auto module = parser.parseModule(fileId);
  if (!module || diagnostics.hasErrors()) {
    return 0;
  }

  const std::uint32_t standardId = sources.addFile(
      "<standard-annotations>",
      std::string(coord::standardAnnotationsSource()));
  coord::Lexer standardLexer(sources, standardId, diagnostics);
  coord::Parser standardParser(standardLexer.lexAll(), diagnostics);
  auto standard = standardParser.parseModule(standardId);
  for (auto& decl : standard->decls) {
    decl->isStandard = true;
  }
  module->decls.insert(module->decls.begin(),
                       std::make_move_iterator(standard->decls.begin()),
                       std::make_move_iterator(standard->decls.end()));

  std::unique_ptr<coord::Checker> checker;
  if (!diagnostics.hasErrors()) {
    checker = std::make_unique<coord::Checker>(sources, diagnostics);
    checker->check(*module);
  }
  if (diagnostics.hasErrors()) {
    return 0;
  }

  coord::Interpreter interpreter(*module, diagnostics);
  interpreter.evaluate();
  interpreter.executeRetainedBlocks();
  if (diagnostics.hasErrors()) {
    return 0;
  }

  std::ostringstream output;
  coord::CppEmitter emitter(*module, interpreter.constValues(), output);
  emitter.emit();
  return 0;
}
