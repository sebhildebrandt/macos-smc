#include "sys.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  sys_prop_t* items;
  size_t used, cap;
} collect_t;

static void collect(const void* key, const void* value, void* ctx) {
  collect_t* c = ctx;
  if (CFGetTypeID(key) != CFStringGetTypeID() || CFGetTypeID(value) != CFDataGetTypeID()) return;
  char name[64];
  if (!CFStringGetCString(key, name, sizeof(name), kCFStringEncodingUTF8)) return;
  if (strncmp(name, "voltage-states", 14) != 0 && strcmp(name, "acc-clusters") != 0 && strcmp(name, "clusters") != 0)
    return;
  if (c->used == c->cap) {
    size_t cap = c->cap ? c->cap * 2 : 32;
    sys_prop_t* grown = realloc(c->items, cap * sizeof(sys_prop_t));
    if (!grown) return;
    c->items = grown;
    c->cap = cap;
  }
  CFIndex len = CFDataGetLength(value);
  size_t n = (size_t)len / 4;
  uint32_t* words = calloc(n ? n : 1, sizeof(uint32_t));
  if (!words) return;
  const uint8_t* b = CFDataGetBytePtr(value);
  for (size_t i = 0; i < n; i++)
    words[i] = (uint32_t)b[i * 4] | ((uint32_t)b[i * 4 + 1] << 8) | ((uint32_t)b[i * 4 + 2] << 16) |
               ((uint32_t)b[i * 4 + 3] << 24);
  sys_prop_t* p = &c->items[c->used++];
  memcpy(p->name, name, sizeof(p->name));
  p->count = n;
  p->words = words;
}

int sys_pmgr_props(sys_prop_t** out, size_t* count) {
  *out = NULL;
  *count = 0;
  io_iterator_t it;
  if (IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching("AppleARMIODevice"), &it) != KERN_SUCCESS)
    return 1;

  collect_t c = {0};
  io_object_t e;
  while ((e = IOIteratorNext(it))) {
    io_name_t name;
    if (IORegistryEntryGetName(e, name) == KERN_SUCCESS && strcmp(name, "pmgr") == 0) {
      CFMutableDictionaryRef props = NULL;
      if (IORegistryEntryCreateCFProperties(e, &props, NULL, 0) == KERN_SUCCESS && props) {
        CFDictionaryApplyFunction(props, collect, &c);
        CFRelease(props);
      }
    }
    IOObjectRelease(e);
  }
  IOObjectRelease(it);
  *out = c.items;
  *count = c.used;
  return 0;
}

void sys_free_props(sys_prop_t* p, size_t count) {
  if (!p) return;
  for (size_t i = 0; i < count; i++) free(p[i].words);
  free(p);
}

int sys_gpu_cores(void) {
  io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AGXAccelerator"));
  if (!svc) return -1;
  int n = -1;
  CFTypeRef v = IORegistryEntryCreateCFProperty(svc, CFSTR("gpu-core-count"), NULL, 0);
  if (v) {
    if (CFGetTypeID(v) == CFNumberGetTypeID()) CFNumberGetValue(v, kCFNumberIntType, &n);
    CFRelease(v);
  }
  IOObjectRelease(svc);
  return n;
}
