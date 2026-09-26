#include <node_api.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>

#include "ioreport.h"
#include "smc.h"
#include "sys.h"

static int get_string_arg(napi_env env, napi_callback_info info, char* buf, size_t n) {
  size_t argc = 1;
  napi_value argv[1];
  size_t len;
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  if (argc < 1 || napi_get_value_string_utf8(env, argv[0], buf, n, &len) != napi_ok) {
    napi_throw_type_error(env, NULL, "string argument expected");
    return 0;
  }
  return 1;
}

static void set_str(napi_env env, napi_value obj, const char* key, const char* s) {
  napi_value v;
  napi_create_string_utf8(env, s, NAPI_AUTO_LENGTH, &v);
  napi_set_named_property(env, obj, key, v);
}

static void set_num(napi_env env, napi_value obj, const char* key, double d) {
  napi_value v;
  napi_create_double(env, d, &v);
  napi_set_named_property(env, obj, key, v);
}

static napi_value js_smc_read(napi_env env, napi_callback_info info) {
  char prefix[5] = {0};
  if (!get_string_arg(env, info, prefix, sizeof(prefix))) return NULL;

  smc_value_t* vals;
  size_t count;
  if (smc_read_prefix(prefix, &vals, &count) != 0) {
    napi_throw_error(env, NULL, "Failed to open AppleSMC");
    return NULL;
  }
  napi_value arr;
  napi_create_array_with_length(env, count, &arr);
  for (size_t i = 0; i < count; i++) {
    napi_value item;
    napi_create_object(env, &item);
    set_str(env, item, "key", vals[i].key);
    set_num(env, item, "value", vals[i].value);
    napi_set_element(env, arr, (uint32_t)i, item);
  }
  free(vals);
  return arr;
}

// Tags our IOReport sample externals so irDelta rejects foreign ones.
static const napi_type_tag SAMPLE_TAG = {0x6d61636f73736d63ULL, 0x6972736d706c6531ULL};

static int get_sample(napi_env env, napi_value v, void** out) {
  bool ok = false;
  return napi_check_object_type_tag(env, v, &SAMPLE_TAG, &ok) == napi_ok && ok &&
         napi_get_value_external(env, v, out) == napi_ok;
}

static void release_sample(napi_env env, void* data, void* hint) { CFRelease((CFDictionaryRef)data); }

static napi_value js_ir_sample(napi_env env, napi_callback_info info) {
  napi_value res;
  CFDictionaryRef s = ir_sample();
  if (!s) {
    napi_get_null(env, &res);
    return res;
  }
  if (napi_create_external(env, (void*)s, release_sample, NULL, &res) != napi_ok) {
    CFRelease(s);
    napi_get_null(env, &res);
    return res;
  }
  napi_type_tag_object(env, res, &SAMPLE_TAG);
  return res;
}

static napi_value js_ir_delta(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_get_cb_info(env, info, &argc, argv, NULL, NULL);
  void *a = NULL, *b = NULL;
  char only[64] = {0};
  size_t len;
  int filter = argc > 2 && napi_get_value_string_utf8(env, argv[2], only, sizeof(only), &len) == napi_ok;
  if (argc < 2 || !get_sample(env, argv[0], &a) || !get_sample(env, argv[1], &b)) {
    napi_throw_type_error(env, NULL, "irDelta(a, b, channel?): samples expected");
    return NULL;
  }
  ir_channel_t* ch;
  size_t count;
  if (ir_delta(a, b, filter ? only : NULL, &ch, &count) != 0) {
    napi_throw_error(env, NULL, "IOReport delta failed");
    return NULL;
  }
  napi_value arr;
  napi_create_array_with_length(env, count, &arr);
  for (size_t i = 0; i < count; i++) {
    napi_value item;
    napi_create_object(env, &item);
    set_str(env, item, "group", ch[i].group);
    set_str(env, item, "subgroup", ch[i].subgroup);
    set_str(env, item, "channel", ch[i].channel);
    set_str(env, item, "unit", ch[i].unit);
    if (ch[i].states) {
      napi_value states;
      napi_create_array_with_length(env, (size_t)ch[i].state_count, &states);
      for (int k = 0; k < ch[i].state_count; k++) {
        napi_value st;
        napi_create_object(env, &st);
        set_str(env, st, "name", ch[i].states[k].name);
        set_num(env, st, "residency", (double)ch[i].states[k].residency);
        napi_set_element(env, states, (uint32_t)k, st);
      }
      napi_set_named_property(env, item, "states", states);
    } else {
      set_num(env, item, "value", (double)ch[i].value);
    }
    napi_set_element(env, arr, (uint32_t)i, item);
  }
  ir_free(ch, count);
  return arr;
}

static napi_value js_pmgr(napi_env env, napi_callback_info info) {
  sys_prop_t* p;
  size_t count;
  napi_value obj;
  napi_create_object(env, &obj);
  if (sys_pmgr_props(&p, &count) != 0) return obj;
  for (size_t i = 0; i < count; i++) {
    napi_value arr;
    napi_create_array_with_length(env, p[i].count, &arr);
    for (size_t k = 0; k < p[i].count; k++) {
      napi_value v;
      napi_create_uint32(env, p[i].words[k], &v);
      napi_set_element(env, arr, (uint32_t)k, v);
    }
    napi_set_named_property(env, obj, p[i].name, arr);
  }
  sys_free_props(p, count);
  return obj;
}

static napi_value js_sysctl_string(napi_env env, napi_callback_info info) {
  char name[128];
  if (!get_string_arg(env, info, name, sizeof(name))) return NULL;
  napi_value res;
  char buf[256];
  size_t len = sizeof(buf);
  if (sysctlbyname(name, buf, &len, NULL, 0) != 0 || len == 0) {
    napi_get_null(env, &res);
    return res;
  }
  napi_create_string_utf8(env, buf, strnlen(buf, len), &res);
  return res;
}

static napi_value js_sysctl_number(napi_env env, napi_callback_info info) {
  char name[128];
  if (!get_string_arg(env, info, name, sizeof(name))) return NULL;
  napi_value res;
  int64_t v = 0;
  size_t len = sizeof(v);
  if (sysctlbyname(name, &v, &len, NULL, 0) != 0 || (len != 4 && len != 8)) {
    napi_get_null(env, &res);
    return res;
  }
  if (len == 4) {
    int32_t v32;
    memcpy(&v32, &v, sizeof(v32));
    v = v32;
  }
  napi_create_double(env, (double)v, &res);
  return res;
}

static napi_value js_gpu_cores(napi_env env, napi_callback_info info) {
  napi_value res;
  int n = sys_gpu_cores();
  if (n > 0) {
    napi_create_int32(env, n, &res);
  } else {
    napi_get_null(env, &res);
  }
  return res;
}

static napi_value Init(napi_env env, napi_value exports) {
  const struct {
    const char* name;
    napi_callback fn;
  } fns[] = {
      {"smcRead", js_smc_read}, {"irSample", js_ir_sample}, {"irDelta", js_ir_delta},
      {"pmgr", js_pmgr},        {"sysctlString", js_sysctl_string}, {"sysctlNumber", js_sysctl_number},
      {"gpuCores", js_gpu_cores},
  };
  for (size_t i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
    napi_value f;
    napi_create_function(env, fns[i].name, NAPI_AUTO_LENGTH, fns[i].fn, NULL, &f);
    napi_set_named_property(env, exports, fns[i].name, f);
  }
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
