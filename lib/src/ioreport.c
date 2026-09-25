#include "ioreport.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>

typedef void* IOReportSubscriptionRef;

static CFDictionaryRef (*fCopyChannelsInGroup)(CFStringRef, CFStringRef, uint64_t, uint64_t, uint64_t);
static void (*fMergeChannels)(CFDictionaryRef, CFDictionaryRef, CFTypeRef);
static IOReportSubscriptionRef (*fCreateSubscription)(void*, CFMutableDictionaryRef, CFMutableDictionaryRef*, uint64_t,
                                                      CFTypeRef);
static CFDictionaryRef (*fCreateSamples)(IOReportSubscriptionRef, CFMutableDictionaryRef, CFTypeRef);
static CFDictionaryRef (*fCreateSamplesDelta)(CFDictionaryRef, CFDictionaryRef, CFTypeRef);
static CFStringRef (*fGetGroup)(CFDictionaryRef);
static CFStringRef (*fGetSubGroup)(CFDictionaryRef);
static CFStringRef (*fGetChannelName)(CFDictionaryRef);
static CFStringRef (*fGetUnitLabel)(CFDictionaryRef);
static uint8_t (*fGetFormat)(CFDictionaryRef);
static int64_t (*fSimpleGetIntegerValue)(CFDictionaryRef, int32_t);
static int32_t (*fStateGetCount)(CFDictionaryRef);
static CFStringRef (*fStateGetNameForIndex)(CFDictionaryRef, int32_t);
static int64_t (*fStateGetResidency)(CFDictionaryRef, int32_t);

#define IR_FORMAT_SIMPLE 1
#define IR_FORMAT_STATE 2

static int resolve(void) {
  void* h = dlopen("/usr/lib/libIOReport.dylib", RTLD_LAZY);
  if (!h) return 0;
#define SYM(var, name) *(void**)(&var) = dlsym(h, name)
  SYM(fCopyChannelsInGroup, "IOReportCopyChannelsInGroup");
  SYM(fMergeChannels, "IOReportMergeChannels");
  SYM(fCreateSubscription, "IOReportCreateSubscription");
  SYM(fCreateSamples, "IOReportCreateSamples");
  SYM(fCreateSamplesDelta, "IOReportCreateSamplesDelta");
  SYM(fGetGroup, "IOReportChannelGetGroup");
  SYM(fGetSubGroup, "IOReportChannelGetSubGroup");
  SYM(fGetChannelName, "IOReportChannelGetChannelName");
  SYM(fGetUnitLabel, "IOReportChannelGetUnitLabel");
  SYM(fGetFormat, "IOReportChannelGetFormat");
  SYM(fSimpleGetIntegerValue, "IOReportSimpleGetIntegerValue");
  SYM(fStateGetCount, "IOReportStateGetCount");
  SYM(fStateGetNameForIndex, "IOReportStateGetNameForIndex");
  SYM(fStateGetResidency, "IOReportStateGetResidency");
#undef SYM
  return fCopyChannelsInGroup && fMergeChannels && fCreateSubscription && fCreateSamples && fCreateSamplesDelta &&
         fGetGroup && fGetSubGroup && fGetChannelName && fGetUnitLabel && fGetFormat && fSimpleGetIntegerValue &&
         fStateGetCount && fStateGetNameForIndex && fStateGetResidency;
}

static void cfstr(CFStringRef s, char* out, size_t n);

// Energy Model has hundreds of per-core channels; keep only the aggregates.
static int wanted(CFDictionaryRef ch) {
  char g[64], name[64];
  cfstr(fGetGroup(ch), g, sizeof(g));
  if (strcmp(g, "Energy Model") != 0) return 1;
  cfstr(fGetChannelName(ch), name, sizeof(name));
  size_t n = strlen(name);
  return (n >= 10 && (strcmp(name + n - 10, "CPU Energy") == 0 || strcmp(name + n - 10, "GPU Energy") == 0)) ||
         strncmp(name, "ANE", 3) == 0 || strncmp(name, "DRAM", 4) == 0;
}

static IOReportSubscriptionRef sub = NULL;
static CFMutableDictionaryRef chans = NULL;
static int tried = 0;

static int subscribe(void) {
  if (sub) return 1;
  if (tried) return 0;
  tried = 1;
  if (!resolve()) return 0;

  const char* groups[][2] = {
      {"Energy Model", NULL},
      {"CPU Stats", "CPU Core Performance States"},
      {"GPU Stats", "GPU Performance States"},
  };
  CFDictionaryRef merged = NULL;
  for (size_t i = 0; i < sizeof(groups) / sizeof(groups[0]); i++) {
    CFStringRef g = CFStringCreateWithCString(NULL, groups[i][0], kCFStringEncodingUTF8);
    CFStringRef s = groups[i][1] ? CFStringCreateWithCString(NULL, groups[i][1], kCFStringEncodingUTF8) : NULL;
    CFDictionaryRef c = fCopyChannelsInGroup(g, s, 0, 0, 0);
    CFRelease(g);
    if (s) CFRelease(s);
    if (!c) continue;
    if (!merged) {
      merged = c;
    } else {
      fMergeChannels(merged, c, NULL);
      CFRelease(c);
    }
  }
  if (!merged) return 0;

  chans = CFDictionaryCreateMutableCopy(NULL, CFDictionaryGetCount(merged), merged);
  CFRelease(merged);
  CFArrayRef all = CFDictionaryGetValue(chans, CFSTR("IOReportChannels"));
  if (all && CFGetTypeID(all) == CFArrayGetTypeID()) {
    CFMutableArrayRef keep = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    for (CFIndex i = 0; i < CFArrayGetCount(all); i++) {
      CFDictionaryRef ch = CFArrayGetValueAtIndex(all, i);
      if (wanted(ch)) CFArrayAppendValue(keep, ch);
    }
    CFDictionarySetValue(chans, CFSTR("IOReportChannels"), keep);
    CFRelease(keep);
  }
  CFMutableDictionaryRef subbed = NULL;
  sub = fCreateSubscription(NULL, chans, &subbed, 0, NULL);
  if (subbed) CFRelease(subbed);
  return sub != NULL;
}

CFDictionaryRef ir_sample(void) {
  if (!subscribe()) return NULL;
  return fCreateSamples(sub, chans, NULL);
}

static void cfstr(CFStringRef s, char* out, size_t n) {
  out[0] = '\0';
  if (s) CFStringGetCString(s, out, (CFIndex)n, kCFStringEncodingUTF8);
}

int ir_delta(CFDictionaryRef a, CFDictionaryRef b, const char* only, ir_channel_t** out, size_t* count) {
  *out = NULL;
  *count = 0;
  CFDictionaryRef d = fCreateSamplesDelta(a, b, NULL);
  if (!d) return 1;

  CFArrayRef items = CFDictionaryGetValue(d, CFSTR("IOReportChannels"));
  CFIndex n = items && CFGetTypeID(items) == CFArrayGetTypeID() ? CFArrayGetCount(items) : 0;
  ir_channel_t* ch = calloc(n ? (size_t)n : 1, sizeof(ir_channel_t));
  if (!ch) {
    CFRelease(d);
    return 2;
  }

  size_t used = 0;
  for (CFIndex i = 0; i < n; i++) {
    CFDictionaryRef it = CFArrayGetValueAtIndex(items, i);
    ir_channel_t* c = &ch[used];
    cfstr(fGetGroup(it), c->group, sizeof(c->group));
    cfstr(fGetSubGroup(it), c->subgroup, sizeof(c->subgroup));
    cfstr(fGetChannelName(it), c->channel, sizeof(c->channel));
    if (only) {
      size_t cl = strlen(c->channel), ol = strlen(only);
      if (cl < ol || strcmp(c->channel + cl - ol, only) != 0) continue;
    }
    cfstr(fGetUnitLabel(it), c->unit, sizeof(c->unit));

    uint8_t fmt = fGetFormat(it);
    if (fmt == IR_FORMAT_SIMPLE) {
      c->value = fSimpleGetIntegerValue(it, 0);
    } else if (fmt == IR_FORMAT_STATE) {
      int32_t sc = fStateGetCount(it);
      if (sc > 0 && sc < 256) {
        c->states = calloc((size_t)sc, sizeof(ir_state_t));
        if (c->states) {
          c->state_count = sc;
          for (int32_t k = 0; k < sc; k++) {
            cfstr(fStateGetNameForIndex(it, k), c->states[k].name, sizeof(c->states[k].name));
            c->states[k].residency = fStateGetResidency(it, k);
          }
        }
      }
    } else {
      continue;
    }
    used++;
  }
  CFRelease(d);
  *out = ch;
  *count = used;
  return 0;
}

void ir_free(ir_channel_t* ch, size_t count) {
  if (!ch) return;
  for (size_t i = 0; i < count; i++) free(ch[i].states);
  free(ch);
}
