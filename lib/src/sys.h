#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
  char name[64];
  size_t count;
  uint32_t* words;  // raw little-endian u32 words
} sys_prop_t;

// Reads pmgr voltage-states* / acc-clusters / clusters data properties. Caller frees with sys_free_props.
int sys_pmgr_props(sys_prop_t** out, size_t* count);
void sys_free_props(sys_prop_t* p, size_t count);

// GPU core count from AGXAccelerator "gpu-core-count", or -1.
int sys_gpu_cores(void);
