/**
 * Benchmark Utilities Implementation
 */

#include "bench_utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Timing                                                             */
/* ------------------------------------------------------------------ */

uint64_t bench_time_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

double bench_time_seconds(void) {
  return (double)bench_time_ns() / 1e9;
}

double get_time_seconds(void) { return bench_time_seconds(); }

/* ------------------------------------------------------------------ */
/* Device list parsing                                                */
/* ------------------------------------------------------------------ */

int bench_parse_devices(const char *csv, bench_device_list_t *out) {
  if (!out) {
    return -1;
  }
  memset(out, 0, sizeof(*out));
  if (!csv || !*csv) {
    return -1;
  }
  out->raw = strdup(csv);
  if (!out->raw) {
    return -1;
  }

  char *cursor = out->raw;
  while (cursor && *cursor) {
    while (*cursor == ' ' || *cursor == '\t') {
      cursor++;
    }
    if (!*cursor) {
      break;
    }
    if (out->count >= BENCH_MAX_DEVICES) {
      bench_device_list_free(out);
      return -1;
    }
    out->paths[out->count++] = cursor;
    char *comma = strchr(cursor, ',');
    if (!comma) {
      break;
    }
    *comma = '\0';
    cursor = comma + 1;
  }

  if (out->count == 0) {
    bench_device_list_free(out);
    return -1;
  }
  return 0;
}

void bench_device_list_free(bench_device_list_t *list) {
  if (!list) {
    return;
  }
  if (list->raw) {
    free(list->raw);
  }
  memset(list, 0, sizeof(*list));
}

/* ------------------------------------------------------------------ */
/* Engine bring-up                                                    */
/* ------------------------------------------------------------------ */

const char *bench_kv_result_string(kv_result_t result) {
  switch (result) {
  case KV_SUCCESS:
    return "KV_SUCCESS";
  case KV_ERR_INVALID_PARAM:
    return "KV_ERR_INVALID_PARAM";
  case KV_ERR_NO_MEMORY:
    return "KV_ERR_NO_MEMORY";
  case KV_ERR_DEVICE_OPEN:
    return "KV_ERR_DEVICE_OPEN";
  case KV_ERR_KEY_NOT_FOUND:
    return "KV_ERR_KEY_NOT_FOUND";
  case KV_ERR_KEY_EXISTS:
    return "KV_ERR_KEY_EXISTS";
  case KV_ERR_VALUE_TOO_LARGE:
    return "KV_ERR_VALUE_TOO_LARGE";
  case KV_ERR_TIMEOUT:
    return "KV_ERR_TIMEOUT";
  case KV_ERR_IO:
    return "KV_ERR_IO";
  case KV_ERR_NOT_INITIALIZED:
    return "KV_ERR_NOT_INITIALIZED";
  case KV_ERR_KEY_ALREADY_EXISTS:
    return "KV_ERR_KEY_ALREADY_EXISTS";
  default:
    return "UNKNOWN_ERROR";
  }
}

const char *bench_default_emul_config(void) {
  static const char *fallback_paths[] = {
      "/user/lib/KVSSD/PDK/core/kvssd_emul.conf",
      "/kvssd/PDK/core/kvssd_emul.conf",
      NULL,
  };
  const char *env = getenv("KVSSD_EMU_CONFIGFILE");
  if (env && *env) {
    return env;
  }
  for (size_t i = 0; fallback_paths[i] != NULL; i++) {
    struct stat st;
    if (stat(fallback_paths[i], &st) == 0) {
      return fallback_paths[i];
    }
  }
  return fallback_paths[0];
}

static void bench_apply_defaults(bench_engine_opts_t *opts) {
  if (!opts->memory_pool_bytes) {
    opts->memory_pool_bytes = (size_t)64 * 1024 * 1024;
  }
  if (!opts->queue_depth) {
    opts->queue_depth = 128;
  }
  if (!opts->dma_pool_count) {
    opts->dma_pool_count = 16;
  }
  if (!opts->enable_stats) {
    opts->enable_stats = 1;
  }
  /* worker_threads remains 0 by default (sync workload) */
}

kv_result_t bench_init_engine_multi(kv_engine_t **engine,
                                    const bench_device_list_t *devices,
                                    const bench_engine_opts_t *opts_in) {
  if (!engine || !devices || devices->count == 0) {
    return KV_ERR_INVALID_PARAM;
  }

  bench_engine_opts_t opts = opts_in ? *opts_in : (bench_engine_opts_t){0};
  bench_apply_defaults(&opts);

  kv_engine_config_t cfg;
  memset(&cfg, 0, sizeof(cfg));

  if (devices->count == 1) {
    cfg.device_path = devices->paths[0];
  } else {
    cfg.num_devices = devices->count;
    for (uint32_t i = 0; i < devices->count; i++) {
      cfg.device_paths[i] = devices->paths[i];
    }
  }
  cfg.emul_config_file = bench_default_emul_config();
  cfg.memory_pool_size = opts.memory_pool_bytes;
  cfg.queue_depth = opts.queue_depth;
  cfg.num_worker_threads = opts.worker_threads;
  cfg.enable_stats = opts.enable_stats;
  cfg.dma_pool_count = opts.dma_pool_count;

  kv_result_t res = kv_engine_init(engine, &cfg);
  if (res != KV_SUCCESS) {
    fprintf(stderr, "[bench] kv_engine_init failed: %s (%d)\n",
            bench_kv_result_string(res), res);
  }
  return res;
}

kv_result_t init_engine(kv_engine_t **engine, const char *device_path,
                        const kv_engine_config_t *config) {
  if (!engine || !device_path) {
    return KV_ERR_INVALID_PARAM;
  }
  if (config) {
    return kv_engine_init(engine, config);
  }
  bench_device_list_t single;
  memset(&single, 0, sizeof(single));
  single.raw = strdup(device_path);
  if (!single.raw) {
    return KV_ERR_NO_MEMORY;
  }
  single.paths[0] = single.raw;
  single.count = 1;
  bench_engine_opts_t opts = {0};
  kv_result_t res = bench_init_engine_multi(engine, &single, &opts);
  bench_device_list_free(&single);
  return res;
}

/* ------------------------------------------------------------------ */
/* Latency vectors                                                    */
/* ------------------------------------------------------------------ */

static uint32_t bench_xorshift32(uint32_t *state) {
  uint32_t x = *state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *state = x ? x : 1;
  return *state;
}

void bench_latvec_init(bench_latvec_t *lv, size_t hard_cap, uint32_t seed) {
  if (!lv) {
    return;
  }
  memset(lv, 0, sizeof(*lv));
  lv->hard_cap = hard_cap > 0 ? hard_cap : BENCH_LATVEC_DEFAULT_HARD_CAP;
  lv->rng_state = seed ? seed : 0x9E3779B9u;
  lv->cap = 1024;
  lv->samples = (uint64_t *)malloc(lv->cap * sizeof(uint64_t));
  if (!lv->samples) {
    lv->cap = 0;
    lv->hard_cap = 0;
  }
}

void bench_latvec_free(bench_latvec_t *lv) {
  if (!lv) {
    return;
  }
  free(lv->samples);
  memset(lv, 0, sizeof(*lv));
}

static int bench_latvec_grow(bench_latvec_t *lv) {
  size_t new_cap = lv->cap * 2;
  if (new_cap > lv->hard_cap) {
    new_cap = lv->hard_cap;
  }
  if (new_cap <= lv->cap) {
    return -1;
  }
  uint64_t *p = (uint64_t *)realloc(lv->samples, new_cap * sizeof(uint64_t));
  if (!p) {
    return -1;
  }
  lv->samples = p;
  lv->cap = new_cap;
  return 0;
}

void bench_latvec_record(bench_latvec_t *lv, uint64_t latency_ns) {
  if (!lv || !lv->samples) {
    return;
  }
  lv->observed++;
  lv->total_ns += latency_ns;
  if (latency_ns > lv->max_ns) {
    lv->max_ns = latency_ns;
  }
  if (lv->count < lv->cap) {
    lv->samples[lv->count++] = latency_ns;
    return;
  }
  if (lv->cap < lv->hard_cap) {
    if (bench_latvec_grow(lv) == 0) {
      lv->samples[lv->count++] = latency_ns;
      return;
    }
  }
  /* Reservoir replacement once we hit the hard cap */
  uint32_t r = bench_xorshift32(&lv->rng_state);
  uint64_t idx = ((uint64_t)r) % lv->observed;
  if (idx < lv->count) {
    lv->samples[idx] = latency_ns;
  }
}

void bench_latvec_merge(bench_latvec_t *dst, const bench_latvec_t *src) {
  if (!dst || !src) {
    return;
  }
  uint64_t retained_total = 0;
  for (size_t i = 0; i < src->count; i++) {
    retained_total += src->samples[i];
    bench_latvec_record(dst, src->samples[i]);
  }
  /* record() already added the retained samples and incremented observed by
   * src->count. Reconcile so dst totals reflect every event that src observed,
   * including any reservoir-dropped samples. */
  if (src->total_ns >= retained_total) {
    dst->total_ns += (src->total_ns - retained_total);
  }
  if (src->observed >= src->count) {
    dst->observed += (src->observed - src->count);
  }
  if (src->max_ns > dst->max_ns) {
    dst->max_ns = src->max_ns;
  }
}

static int bench_u64_cmp(const void *a, const void *b) {
  uint64_t x = *(const uint64_t *)a;
  uint64_t y = *(const uint64_t *)b;
  return (x > y) - (x < y);
}

void bench_latvec_sort(bench_latvec_t *lv) {
  if (!lv || lv->count < 2) {
    return;
  }
  qsort(lv->samples, lv->count, sizeof(uint64_t), bench_u64_cmp);
}

uint64_t bench_latvec_percentile(const bench_latvec_t *lv, double p) {
  if (!lv || lv->count == 0) {
    return 0;
  }
  if (p < 0.0) p = 0.0;
  if (p > 1.0) p = 1.0;
  size_t idx = (size_t)((double)(lv->count - 1) * p + 0.5);
  if (idx >= lv->count) {
    idx = lv->count - 1;
  }
  return lv->samples[idx];
}

double bench_latvec_avg_us(const bench_latvec_t *lv) {
  if (!lv || lv->observed == 0) {
    return 0.0;
  }
  return ((double)lv->total_ns / 1000.0) / (double)lv->observed;
}

/* ------------------------------------------------------------------ */
/* CSV helpers                                                        */
/* ------------------------------------------------------------------ */

FILE *bench_csv_open(const char *path, const char *header) {
  if (!path) {
    return NULL;
  }
  struct stat st;
  int empty = 1;
  if (stat(path, &st) == 0 && st.st_size > 0) {
    empty = 0;
  }
  FILE *fp = fopen(path, "a");
  if (!fp) {
    fprintf(stderr, "[bench] failed to open CSV %s: %s\n", path,
            strerror(errno));
    return NULL;
  }
  if (empty && header && *header) {
    fprintf(fp, "%s\n", header);
    fflush(fp);
  }
  return fp;
}

void bench_csv_write_str(FILE *fp, const char *value) {
  if (!fp) {
    return;
  }
  if (!value) {
    return;
  }
  /* Quote the value if it contains a comma or quote, escaping embedded quotes. */
  int needs_quote = 0;
  for (const char *p = value; *p; p++) {
    if (*p == ',' || *p == '"' || *p == '\n') {
      needs_quote = 1;
      break;
    }
  }
  if (!needs_quote) {
    fprintf(fp, "%s", value);
    return;
  }
  fputc('"', fp);
  for (const char *p = value; *p; p++) {
    if (*p == '"') {
      fputc('"', fp);
    }
    fputc(*p, fp);
  }
  fputc('"', fp);
}
