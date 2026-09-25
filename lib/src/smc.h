#pragma once
#include <stddef.h>

typedef struct {
  char key[5];
  double value;
} smc_value_t;

// Reads all numeric keys starting with prefix. Returns 0 on success; caller frees *out.
int smc_read_prefix(const char* prefix, smc_value_t** out, size_t* count);
