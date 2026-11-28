#include "sample.h"

#include <string.h>

int64_t coo_sample_add(int64_t left, int64_t right) { return left + right; }

const char* coo_sample_greet(const char* name) {
  return (name != NULL && strcmp(name, "coo") == 0) ? "hi" : "?";
}
