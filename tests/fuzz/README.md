# Parser and compiler fuzzing

The harnesses drive the front end with arbitrary bytes:
- `coord_parser_fuzz`: Lexer + Parser only.
- `coord_compiler_fuzz`: Lexer + Parser + Sema + Interpreter + C++ emitter. It
  keeps the `Checker` alive until emission because `TypeExpr::resolved` points
  into checker-owned types.

Build with Clang:

```bash
cmake -S . -B out/build/fuzz -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCOORD_FUZZ=ON -DCOORD_SANITIZE=ON
cmake --build out/build/fuzz --target coord_parser_fuzz coord_compiler_fuzz
ASAN_OPTIONS=detect_leaks=0 \
  ./out/build/fuzz/coord_compiler_fuzz -runs=10000 \
  -artifact_prefix=out/fuzz/ tests/fuzz/corpus
```

`tests/fuzz/corpus/` holds the checked-in seed inputs. libFuzzer may add cases to
the corpus while running; remove generated cases before committing or point the
last argument at a scratch copy.
