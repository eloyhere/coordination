// libFuzzer entry point for the Coordination lexer/parser.
//
// Build with:
//   cmake -S . -B out/build/fuzz -G Ninja \
//     -DCMAKE_CXX_COMPILER=clang++ -DCOORD_FUZZ=ON -DCOORD_SANITIZE=ON
//   cmake --build out/build/fuzz --target coord_parser_fuzz
//   ./out/build/fuzz/coord_parser_fuzz -runs=1000 tests/fuzz/corpus

#include "coord/parser.h"
#include "coord/support.h"
#include "coord/syntax.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                     std::size_t size) {
  if (data == nullptr || size == 0 || size > (1U << 20U)) {
    return 0;
  }
  coord::SourceManager sources;
  const std::uint32_t fileId = sources.addFile(
      "fuzz.coo", std::string(reinterpret_cast<const char*>(data), size));
  coord::DiagnosticEngine diagnostics;
  coord::Lexer lexer(sources, fileId, diagnostics);
  coord::Parser parser(lexer.lexAll(), diagnostics);
  (void)parser.parseModule(fileId);
  return 0;
}
