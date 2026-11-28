#ifndef COO_SAMPLE_H
#define COO_SAMPLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Adds two 64-bit integers. */
int64_t coo_sample_add(int64_t left, int64_t right);

/* Returns a static greeting for "coo" and "?" for any other name. */
const char* coo_sample_greet(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* COO_SAMPLE_H */
