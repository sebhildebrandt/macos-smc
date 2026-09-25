#include "smc.h"

#include <IOKit/IOKitLib.h>
#include <stdlib.h>
#include <string.h>

#define SMC_CMD_READ_BYTES 5
#define SMC_CMD_READ_INDEX 8
#define SMC_CMD_READ_KEYINFO 9
#define SMC_KERNEL_INDEX 2
#define FOURCC(a, b, c, d) \
  (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

typedef struct {
  uint8_t major, minor, build, reserved;
  uint16_t release;
} smc_vers_t;

typedef struct {
  uint16_t version, length;
  uint32_t cpu_plimit, gpu_plimit, mem_plimit;
} smc_plimit_t;

typedef struct {
  uint32_t data_size;
  uint32_t data_type;
  uint8_t data_attributes;
} smc_keyinfo_t;

typedef struct {
  uint32_t key;
  smc_vers_t vers;
  smc_plimit_t plimit;
  smc_keyinfo_t keyinfo;
  uint8_t result;
  uint8_t status;
  uint8_t data8;
  uint32_t data32;
  uint8_t bytes[32];
} smc_keydata_t;

typedef struct {
  uint32_t key, type, size;
} smc_key_t;

static io_connect_t conn = 0;
static int tried = 0;
static smc_key_t* keys = NULL;
static size_t key_count = 0;

static int smc_call(smc_keydata_t* in, smc_keydata_t* out) {
  size_t out_size = sizeof(*out);
  memset(out, 0, sizeof(*out));
  kern_return_t kr = IOConnectCallStructMethod(conn, SMC_KERNEL_INDEX, in, sizeof(*in), out, &out_size);
  return kr == KERN_SUCCESS && out->result == 0;
}

static int is_numeric(uint32_t type, uint32_t size) {
  return (type == FOURCC('f', 'l', 't', ' ') && size == 4) || (type == FOURCC('u', 'i', '8', ' ') && size == 1) ||
         (type == FOURCC('u', 'i', '1', '6') && size == 2) || (type == FOURCC('u', 'i', '3', '2') && size == 4);
}

// Key set is fixed per boot: enumerate once, keep numeric T*/F*/P* keys only.
static void scan_keys(void) {
  smc_keydata_t in = {0}, out;
  in.key = FOURCC('#', 'K', 'E', 'Y');
  in.keyinfo.data_size = 4;
  in.data8 = SMC_CMD_READ_BYTES;
  if (!smc_call(&in, &out)) return;
  uint32_t total = ((uint32_t)out.bytes[0] << 24) | ((uint32_t)out.bytes[1] << 16) |
                   ((uint32_t)out.bytes[2] << 8) | (uint32_t)out.bytes[3];
  if (total == 0 || total > 10000) return;

  keys = calloc(total, sizeof(smc_key_t));
  if (!keys) return;

  for (uint32_t i = 0; i < total; i++) {
    memset(&in, 0, sizeof(in));
    in.data8 = SMC_CMD_READ_INDEX;
    in.data32 = i;
    if (!smc_call(&in, &out)) continue;
    uint32_t key = out.key;
    char c = (char)(key >> 24);
    if (c != 'T' && c != 'F' && c != 'P') continue;

    memset(&in, 0, sizeof(in));
    in.key = key;
    in.data8 = SMC_CMD_READ_KEYINFO;
    if (!smc_call(&in, &out)) continue;
    if (!is_numeric(out.keyinfo.data_type, out.keyinfo.data_size)) continue;

    keys[key_count++] = (smc_key_t){key, out.keyinfo.data_type, out.keyinfo.data_size};
  }
}

static int smc_open(void) {
  if (conn) return 1;
  if (tried) return 0;
  tried = 1;

  io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
  if (!svc) return 0;
  kern_return_t kr = IOServiceOpen(svc, mach_task_self(), 0, &conn);
  IOObjectRelease(svc);
  if (kr != KERN_SUCCESS) {
    conn = 0;
    return 0;
  }
  scan_keys();
  return 1;
}

static int read_value(const smc_key_t* k, double* out) {
  smc_keydata_t in = {0}, r;
  in.key = k->key;
  in.keyinfo.data_size = k->size;
  in.data8 = SMC_CMD_READ_BYTES;
  if (!smc_call(&in, &r)) return 0;

  if (k->type == FOURCC('f', 'l', 't', ' ')) {
    float f;
    memcpy(&f, r.bytes, sizeof(f));
    *out = (double)f;
    return 1;
  }
  uint32_t v = 0;
  for (uint32_t i = 0; i < k->size; i++) v = (v << 8) | r.bytes[i];
  *out = (double)v;
  return 1;
}

int smc_read_prefix(const char* prefix, smc_value_t** out, size_t* count) {
  *out = NULL;
  *count = 0;
  if (!smc_open()) return 1;

  size_t plen = strnlen(prefix, 4);
  smc_value_t* vals = calloc(key_count ? key_count : 1, sizeof(smc_value_t));
  if (!vals) return 2;

  size_t used = 0;
  for (size_t i = 0; i < key_count; i++) {
    char name[5] = {(char)(keys[i].key >> 24), (char)(keys[i].key >> 16), (char)(keys[i].key >> 8),
                    (char)keys[i].key, 0};
    if (strncmp(name, prefix, plen) != 0) continue;
    double v;
    if (!read_value(&keys[i], &v)) continue;
    memcpy(vals[used].key, name, sizeof(name));
    vals[used].value = v;
    used++;
  }
  *out = vals;
  *count = used;
  return 0;
}
