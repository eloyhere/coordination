# Parser fuzzing

The harness drives `Lexer` + `Parser` with arbitrary bytes. Build with Clang:

```bash
cmake -S . -B out/build/fuzz -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ -DCOORD_FUZZ=ON -DCOORD_SANITIZE=ON
cmake --build out/build/fuzz --target coord_parser_fuzz
ASAN_OPTIONS=detect_leaks=0 \
  ./out/build/fuzz/coord_parser_fuzz -runs=10000 \
  -artifact_prefix=out/fuzz/ tests/fuzz/corpus
```

`tests/fuzz/corpus/` holds the checked-in seed inputs. libFuzzer may add cases to
the corpus while running; remove generated cases before committing or point the
last argument at a scratch copy.
