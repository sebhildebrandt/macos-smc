#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>

typedef struct {
  char name[32];
  int64_t residency;
} ir_state_t;

typedef struct {
  char group[64];
  char subgroup[64];
  char channel[64];
  char unit[16];
  int64_t value;  // simple channels
  int state_count;
  ir_state_t* states;  // state channels
} ir_channel_t;

// Returns a retained sample or NULL.
CFDictionaryRef ir_sample(void);

// Delta between two samples, optionally only channels whose name ends with `only`. Returns 0 on success; caller frees with ir_free.
int ir_delta(CFDictionaryRef a, CFDictionaryRef b, const char* only, ir_channel_t** out, size_t* count);

void ir_free(ir_channel_t* ch, size_t count);
