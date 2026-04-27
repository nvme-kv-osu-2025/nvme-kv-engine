/**
 * KV Engine Stress Benchmark
 *
 * Configurable, multi-threaded benchmark for the KV engine. Supports:
 *   - single-device and multi-device (sharded) modes
 *   - read / write / mixed / delete / exists / edge workloads
 *   - configurable key & value size ranges, duration or op-count budgets,
 *     warm-up, RNG seed, and thread count
 *   - per-workload CSV row (mentor-friendly summary) plus a human-readable
 *     summary file with throughput and p50/p95/p99/p99.9 latency
 *
 * Usage examples:
 *
 *   bench_kv_stress --label single_mixed_quick \
 *       --devices /dev/kvemul0 --workload mixed --read-percent 70 \
 *       --threads 4 --duration-sec 10 \
 *       --key-min 16 --key-max 16 --value-min 4096 --value-max 4096 \
 *       --keyspace 50000 \
 *       --csv  /user/benchmarks/results/latest/kv_stress.csv \
 *       --summary /user/benchmarks/results/latest/raw/single_mixed_quick.txt
 */
#include "kv_engine.h"
#include "util/bench_utils.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#define BENCH_CSV_HEADER                                                       \
  "label,workload,read_percent,devices,threads,duration_sec,warmup_sec,"       \
  "ops_completed,ops_failed,success_rate,throughput_ops_sec,"                  \
  "throughput_mb_sec,avg_latency_us,p50_us,p95_us,p99_us,p999_us,"             \
  "max_latency_us,key_min,key_max,value_min,value_max,keyspace,seed,"          \
  "user_cpu_sec,sys_cpu_sec,max_rss_kb,notes"

typedef enum {
  WL_WRITE,
  WL_READ,
  WL_MIXED,
  WL_DELETE,
  WL_EXISTS,
  WL_EDGE,
  WL_COUNT
} workload_t;

static const char *workload_name(workload_t w) {
  switch (w) {
  case WL_WRITE:
    return "write";
  case WL_READ:
    return "read";
  case WL_MIXED:
    return "mixed";
  case WL_DELETE:
    return "delete";
  case WL_EXISTS:
    return "exists";
  case WL_EDGE:
    return "edge";
  default:
    return "unknown";
  }
}

static int parse_workload(const char *s, workload_t *out) {
  if (!s || !out)
    return -1;
  if (!strcmp(s, "write"))
    *out = WL_WRITE;
  else if (!strcmp(s, "read"))
    *out = WL_READ;
  else if (!strcmp(s, "mixed"))
    *out = WL_MIXED;
  else if (!strcmp(s, "delete"))
    *out = WL_DELETE;
  else if (!strcmp(s, "exists"))
    *out = WL_EXISTS;
  else if (!strcmp(s, "edge"))
    *out = WL_EDGE;
  else
    return -1;
  return 0;
}

typedef struct {
  const char *label;
  const char *devices_csv;
  workload_t workload;
  int read_percent;
  uint32_t threads;
  double duration_sec;
  double warmup_sec;
  uint64_t ops; /* 0 means use duration */
  uint32_t key_min;
  uint32_t key_max;
  uint32_t value_min;
  uint32_t value_max;
  uint64_t keyspace; /* unique keys to populate / sample over */
  uint32_t seed;
  uint32_t queue_depth;
  uint32_t worker_threads;
  size_t memory_pool_bytes;
  uint32_t dma_pool_count;
  const char *csv_path;
  const char *summary_path;
} cfg_t;

static void cfg_set_defaults(cfg_t *c) {
  memset(c, 0, sizeof(*c));
  c->label = "stress";
  c->workload = WL_MIXED;
  c->read_percent = 70;
  c->threads = 4;
  c->duration_sec = 10.0;
  c->warmup_sec = 2.0;
  c->ops = 0;
  c->key_min = 16;
  c->key_max = 16;
  c->value_min = 4096;
  c->value_max = 4096;
  c->keyspace = 50000;
  c->seed = 42;
  c->queue_depth = 128;
  c->worker_threads = 0;
  c->memory_pool_bytes = (size_t)64 * 1024 * 1024;
  c->dma_pool_count = 16;
}

static void usage(const char *prog) {
  fprintf(
      stderr,
      "Usage: %s [options]\n\n"
      "Workload selection:\n"
      "  --workload write|read|mixed|delete|exists|edge\n"
      "  --read-percent N        (mixed only, default 70)\n"
      "  --label NAME            label written to CSV/summary\n\n"
      "Devices and threading:\n"
      "  --devices /dev/kvemul0[,/dev/kvemul1,...]\n"
      "  --threads N             default 4\n"
      "  --duration-sec N        default 10s (ignored if --ops set)\n"
      "  --warmup-sec N          default 2s\n"
      "  --ops N                 fixed op count (per benchmark)\n\n"
      "Key/value sizes (bytes):\n"
      "  --key-min N --key-max N         (4..255)\n"
      "  --value-min N --value-max N     (1..2097152)\n"
      "  --keyspace N                    distinct keys for read/mixed/etc.\n"
      "  --seed N                        RNG seed (default 42)\n\n"
      "Engine knobs:\n"
      "  --queue-depth N --worker-threads N\n"
      "  --memory-pool-bytes N --dma-pool-count N\n\n"
      "Output:\n"
      "  --csv PATH        append a CSV row\n"
      "  --summary PATH    append a human-readable summary block\n",
      prog);
}

#define ARG_NEXT()                                                             \
  do {                                                                         \
    i++;                                                                       \
    if (i >= argc) {                                                           \
      fprintf(stderr, "Missing value for %s\n", argv[i - 1]);                  \
      return -1;                                                               \
    }                                                                          \
  } while (0)

static int parse_args(int argc, char **argv, cfg_t *c) {
  for (int i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
      usage(argv[0]);
      return 1;
    } else if (!strcmp(a, "--label")) {
      ARG_NEXT();
      c->label = argv[i];
    } else if (!strcmp(a, "--workload")) {
      ARG_NEXT();
      if (parse_workload(argv[i], &c->workload) != 0) {
        fprintf(stderr, "Unknown workload: %s\n", argv[i]);
        return -1;
      }
    } else if (!strcmp(a, "--read-percent")) {
      ARG_NEXT();
      c->read_percent = atoi(argv[i]);
    } else if (!strcmp(a, "--devices")) {
      ARG_NEXT();
      c->devices_csv = argv[i];
    } else if (!strcmp(a, "--threads")) {
      ARG_NEXT();
      c->threads = (uint32_t)atoi(argv[i]);
    } else if (!strcmp(a, "--duration-sec")) {
      ARG_NEXT();
      c->duration_sec = atof(argv[i]);
    } else if (!strcmp(a, "--warmup-sec")) {
      ARG_NEXT();
      c->warmup_sec = atof(argv[i]);
    } else if (!strcmp(a, "--ops")) {
      ARG_NEXT();
      c->ops = strtoull(argv[i], NULL, 10);
    } else if (!strcmp(a, "--key-min")) {
      ARG_NEXT();
      c->key_min = (uint32_t)atoi(argv[i]);
    } else if (!strcmp(a, "--key-max")) {
      ARG_NEXT();
      c->key_max = (uint32_t)atoi(argv[i]);
    } else if (!strcmp(a, "--value-min")) {
      ARG_NEXT();
      c->value_min = (uint32_t)atoi(argv[i]);
    } else if (!strcmp(a, "--value-max")) {
      ARG_NEXT();
      c->value_max = (uint32_t)atoi(argv[i]);
    } else if (!strcmp(a, "--keyspace")) {
      ARG_NEXT();
      c->keyspace = strtoull(argv[i], NULL, 10);
    } else if (!strcmp(a, "--seed")) {
      ARG_NEXT();
      c->seed = (uint32_t)strtoul(argv[i], NULL, 10);
    } else if (!strcmp(a, "--queue-depth")) {
      ARG_NEXT();
      c->queue_depth = (uint32_t)atoi(argv[i]);
    } else if (!strcmp(a, "--worker-threads")) {
      ARG_NEXT();
      c->worker_threads = (uint32_t)atoi(argv[i]);
    } else if (!strcmp(a, "--memory-pool-bytes")) {
      ARG_NEXT();
      c->memory_pool_bytes = strtoull(argv[i], NULL, 10);
    } else if (!strcmp(a, "--dma-pool-count")) {
      ARG_NEXT();
      c->dma_pool_count = (uint32_t)atoi(argv[i]);
    } else if (!strcmp(a, "--csv")) {
      ARG_NEXT();
      c->csv_path = argv[i];
    } else if (!strcmp(a, "--summary")) {
      ARG_NEXT();
      c->summary_path = argv[i];
    } else {
      fprintf(stderr, "Unknown argument: %s\n", a);
      usage(argv[0]);
      return -1;
    }
  }
  if (!c->devices_csv) {
    fprintf(stderr, "--devices is required\n");
    usage(argv[0]);
    return -1;
  }
  if (c->key_min < 4)
    c->key_min = 4;
  if (c->key_max > 255)
    c->key_max = 255;
  if (c->key_max < c->key_min)
    c->key_max = c->key_min;
  if (c->value_min < 1)
    c->value_min = 1;
  if (c->value_max > (2 * 1024 * 1024))
    c->value_max = 2 * 1024 * 1024;
  if (c->value_max < c->value_min)
    c->value_max = c->value_min;
  if (c->threads < 1)
    c->threads = 1;
  if (c->read_percent < 0)
    c->read_percent = 0;
  if (c->read_percent > 100)
    c->read_percent = 100;
  if (c->keyspace == 0)
    c->keyspace = 1;
  return 0;
}

/* -------------------------------------------------------------- */
/* RNG and key/value generation                                   */
/* -------------------------------------------------------------- */

typedef struct {
  uint64_t state;
} rng_t;

static void rng_seed(rng_t *r, uint64_t seed) {
  r->state = seed ? seed : 0x9E3779B97F4A7C15ULL;
}

static uint64_t rng_next(rng_t *r) {
  uint64_t x = r->state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  r->state = x;
  return x;
}

static uint32_t rng_range(rng_t *r, uint32_t lo, uint32_t hi) {
  if (hi <= lo)
    return lo;
  uint32_t span = hi - lo + 1;
  return lo + (uint32_t)(rng_next(r) % span);
}

/* Deterministic key-of-index encoder. Format: prefix-padded ASCII so the same
 * (idx, key_len) always produces the same byte sequence, enabling reads to
 * match writes. */
static void gen_key(char *buf, size_t key_len, uint64_t idx) {
  if (key_len < 4)
    key_len = 4;
  if (key_len > 255)
    key_len = 255;
  /* Always emit at least the index in hex; pad the remainder with letters. */
  char hex[32];
  int n = snprintf(hex, sizeof(hex), "%016" PRIx64, idx);
  if (n < 0)
    n = 0;
  size_t hex_len = (size_t)n;
  if (hex_len > key_len)
    hex_len = key_len;
  size_t pad = key_len - hex_len;
  for (size_t i = 0; i < pad; i++) {
    buf[i] = 'a' + (char)(i % 26);
  }
  memcpy(buf + pad, hex, hex_len);
}

static void fill_value(char *buf, size_t value_len, uint64_t seed) {
  uint64_t s = seed ^ 0xC2B2AE3D27D4EB4FULL;
  for (size_t i = 0; i < value_len; i += 8) {
    s = s * 6364136223846793005ULL + 1442695040888963407ULL;
    size_t take = value_len - i < 8 ? value_len - i : 8;
    memcpy(buf + i, &s, take);
  }
}

/* -------------------------------------------------------------- */
/* Worker context                                                 */
/* -------------------------------------------------------------- */

typedef struct {
  kv_engine_t *engine;
  const cfg_t *cfg;

  uint32_t thread_id;
  uint64_t key_offset; /* offset into key index space */
  uint64_t key_count;  /* slice of keyspace assigned to thread */
  double deadline_sec; /* when to stop (monotonic seconds) */
  uint64_t ops_budget; /* if non-zero, hard op cap for this thread */

  /* Counters and latency capture */
  uint64_t ops_done;
  uint64_t ops_fail;
  uint64_t bytes_done;
  bench_latvec_t latencies;

  uint32_t rng_seed;
} worker_ctx_t;

/* -------------------------------------------------------------- */
/* Workload primitives                                            */
/* -------------------------------------------------------------- */

static double now_seconds(void) { return bench_time_seconds(); }

static int do_write(kv_engine_t *engine, char *kbuf, char *vbuf, rng_t *rng,
                    const cfg_t *cfg, uint64_t key_idx, size_t *out_value_len) {
  uint32_t klen = rng_range(rng, cfg->key_min, cfg->key_max);
  uint32_t vlen = rng_range(rng, cfg->value_min, cfg->value_max);
  gen_key(kbuf, klen, key_idx);
  fill_value(vbuf, vlen, key_idx);
  kv_result_t res = kv_engine_store(engine, kbuf, klen, vbuf, vlen, true);
  if (out_value_len)
    *out_value_len = vlen;
  return (res == KV_SUCCESS) ? 0 : -1;
}

static int do_read(kv_engine_t *engine, char *kbuf, rng_t *rng,
                   const cfg_t *cfg, uint64_t key_idx, size_t *out_value_len) {
  uint32_t klen = rng_range(rng, cfg->key_min, cfg->key_max);
  gen_key(kbuf, klen, key_idx);
  void *value = NULL;
  size_t value_len = 0;
  kv_result_t res =
      kv_engine_retrieve(engine, kbuf, klen, &value, &value_len, false);
  if (res == KV_SUCCESS) {
    if (out_value_len)
      *out_value_len = value_len;
    kv_engine_free_buffer(engine, value);
    return 0;
  }
  if (out_value_len)
    *out_value_len = 0;
  return -1;
}

static int do_delete(kv_engine_t *engine, char *kbuf, rng_t *rng,
                     const cfg_t *cfg, uint64_t key_idx) {
  uint32_t klen = rng_range(rng, cfg->key_min, cfg->key_max);
  gen_key(kbuf, klen, key_idx);
  kv_result_t res = kv_engine_delete(engine, kbuf, klen);
  return (res == KV_SUCCESS) ? 0 : -1;
}

static int do_exists(kv_engine_t *engine, char *kbuf, rng_t *rng,
                     const cfg_t *cfg, uint64_t key_idx) {
  uint32_t klen = rng_range(rng, cfg->key_min, cfg->key_max);
  gen_key(kbuf, klen, key_idx);
  int exists = 0;
  kv_result_t res = kv_engine_exists(engine, kbuf, klen, &exists);
  return (res == KV_SUCCESS) ? 0 : -1;
}

/* -------------------------------------------------------------- */
/* Worker entry                                                   */
/* -------------------------------------------------------------- */

static void *worker_entry(void *arg) {
  worker_ctx_t *w = (worker_ctx_t *)arg;
  const cfg_t *cfg = w->cfg;

  char *kbuf = (char *)malloc(cfg->key_max + 1);
  size_t vbuf_size = cfg->value_max > 0 ? cfg->value_max : 1;
  char *vbuf = (char *)malloc(vbuf_size);
  if (!kbuf || !vbuf) {
    fprintf(stderr, "[worker %u] OOM allocating buffers\n", w->thread_id);
    free(kbuf);
    free(vbuf);
    return NULL;
  }

  rng_t rng;
  rng_seed(&rng, ((uint64_t)w->rng_seed << 32) ^ (uint64_t)(w->thread_id + 1));

  double end = w->deadline_sec;
  uint64_t local_done = 0;
  uint64_t budget = w->ops_budget;

  for (;;) {
    if (budget && local_done >= budget)
      break;
    if (now_seconds() >= end)
      break;

    /* Pick a key index. For workloads that depend on keyspace, sample
     * uniformly from the populated range. For pure write, append using
     * (thread_id, local_done) so each thread writes a unique stripe. */
    uint64_t key_idx;
    workload_t op = cfg->workload;
    if (cfg->workload == WL_MIXED) {
      uint32_t roll = (uint32_t)(rng_next(&rng) % 100);
      op = (roll < (uint32_t)cfg->read_percent) ? WL_READ : WL_WRITE;
    }

    if (op == WL_WRITE) {
      key_idx = (uint64_t)w->thread_id * (1ULL << 40) + local_done;
    } else {
      key_idx = w->key_offset + (rng_next(&rng) % w->key_count);
    }

    uint64_t t0 = bench_time_ns();
    int rc = -1;
    size_t bytes = 0;
    switch (op) {
    case WL_WRITE:
      rc = do_write(w->engine, kbuf, vbuf, &rng, cfg, key_idx, &bytes);
      break;
    case WL_READ:
      rc = do_read(w->engine, kbuf, &rng, cfg, key_idx, &bytes);
      break;
    case WL_DELETE:
      rc = do_delete(w->engine, kbuf, &rng, cfg, key_idx);
      break;
    case WL_EXISTS:
      rc = do_exists(w->engine, kbuf, &rng, cfg, key_idx);
      break;
    default:
      rc = -1;
      break;
    }
    uint64_t lat = bench_time_ns() - t0;

    bench_latvec_record(&w->latencies, lat);
    if (rc == 0) {
      w->ops_done++;
      w->bytes_done += bytes;
    } else {
      w->ops_fail++;
    }
    local_done++;
  }

  free(kbuf);
  free(vbuf);
  return NULL;
}

/* -------------------------------------------------------------- */
/* Prepopulate / cleanup                                          */
/* -------------------------------------------------------------- */

static int prepopulate(kv_engine_t *engine, const cfg_t *cfg) {
  if (cfg->keyspace == 0)
    return 0;
  char *kbuf = (char *)malloc(cfg->key_max + 1);
  size_t vbuf_size = cfg->value_max > 0 ? cfg->value_max : 1;
  char *vbuf = (char *)malloc(vbuf_size);
  if (!kbuf || !vbuf) {
    free(kbuf);
    free(vbuf);
    return -1;
  }
  rng_t rng;
  rng_seed(&rng, (uint64_t)cfg->seed ^ 0xA5A5A5A5ULL);

  fprintf(stderr, "[stress] prepopulate %" PRIu64 " keys (%s)\n", cfg->keyspace,
          cfg->label);
  double t0 = now_seconds();
  uint64_t fail = 0;
  for (uint64_t i = 0; i < cfg->keyspace; i++) {
    uint32_t klen = rng_range(&rng, cfg->key_min, cfg->key_max);
    uint32_t vlen = rng_range(&rng, cfg->value_min, cfg->value_max);
    gen_key(kbuf, klen, i);
    fill_value(vbuf, vlen, i);
    kv_result_t res = kv_engine_store(engine, kbuf, klen, vbuf, vlen, true);
    if (res != KV_SUCCESS) {
      fail++;
      if (fail < 5) {
        fprintf(stderr, "[stress] prepopulate failed at %" PRIu64 " (%s)\n", i,
                bench_kv_result_string(res));
      }
    }
  }
  double dt = now_seconds() - t0;
  fprintf(stderr,
          "[stress] prepopulate complete in %.2fs (%" PRIu64 " failures)\n", dt,
          fail);
  free(kbuf);
  free(vbuf);
  return fail == cfg->keyspace ? -1 : 0;
}

/* -------------------------------------------------------------- */
/* Aggregation, CSV, summary                                       */
/* -------------------------------------------------------------- */

typedef struct {
  uint64_t ops_done;
  uint64_t ops_fail;
  uint64_t bytes_done;
  bench_latvec_t latencies;
  double duration_sec;

  double user_cpu_sec;
  double sys_cpu_sec;
  long max_rss_kb;

  const char *notes;
} run_result_t;

static double timeval_to_sec(struct timeval tv) {
  return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static void capture_rusage(struct rusage *before, struct rusage *after,
                           run_result_t *result) {
  result->user_cpu_sec =
      timeval_to_sec(after->ru_utime) - timeval_to_sec(before->ru_utime);
  result->sys_cpu_sec =
      timeval_to_sec(after->ru_stime) - timeval_to_sec(before->ru_stime);
  result->max_rss_kb = after->ru_maxrss;
}

static void emit_csv(const cfg_t *cfg, const run_result_t *r) {
  if (!cfg->csv_path)
    return;
  FILE *fp = bench_csv_open(cfg->csv_path, BENCH_CSV_HEADER);
  if (!fp)
    return;

  double total_ops = (double)(r->ops_done + r->ops_fail);
  double success_rate = total_ops > 0 ? (double)r->ops_done / total_ops : 0.0;
  double dt = r->duration_sec > 0 ? r->duration_sec : 1e-9;
  double tput_ops = (double)r->ops_done / dt;
  double tput_mb = ((double)r->bytes_done / (1024.0 * 1024.0)) / dt;
  double avg_us = bench_latvec_avg_us(&r->latencies);
  uint64_t p50 = bench_latvec_percentile(&r->latencies, 0.50);
  uint64_t p95 = bench_latvec_percentile(&r->latencies, 0.95);
  uint64_t p99 = bench_latvec_percentile(&r->latencies, 0.99);
  uint64_t p999 = bench_latvec_percentile(&r->latencies, 0.999);
  uint64_t max_lat = r->latencies.max_ns;

  bench_csv_write_str(fp, cfg->label ? cfg->label : "");
  fputc(',', fp);
  bench_csv_write_str(fp, workload_name(cfg->workload));
  fprintf(fp, ",%d,", cfg->workload == WL_MIXED ? cfg->read_percent : -1);
  bench_csv_write_str(fp, cfg->devices_csv ? cfg->devices_csv : "");
  fprintf(fp,
          ",%u,%.3f,%.3f,%" PRIu64 ",%" PRIu64
          ",%.6f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%u,%u,%u,%u,%" PRIu64
          ",%u,%.3f,%.3f,%ld,",
          cfg->threads, r->duration_sec, cfg->warmup_sec, r->ops_done,
          r->ops_fail, success_rate, tput_ops, tput_mb, avg_us,
          (double)p50 / 1000.0, (double)p95 / 1000.0, (double)p99 / 1000.0,
          (double)p999 / 1000.0, (double)max_lat / 1000.0, cfg->key_min,
          cfg->key_max, cfg->value_min, cfg->value_max, cfg->keyspace,
          cfg->seed, r->user_cpu_sec, r->sys_cpu_sec, r->max_rss_kb);
  bench_csv_write_str(fp, r->notes ? r->notes : "");
  fputc('\n', fp);

  fflush(fp);
  fclose(fp);
}

static void emit_summary(const cfg_t *cfg, const run_result_t *r) {
  FILE *fp = stdout;
  FILE *open_fp = NULL;
  if (cfg->summary_path) {
    open_fp = fopen(cfg->summary_path, "a");
    if (open_fp)
      fp = open_fp;
  }
  double total_ops = (double)(r->ops_done + r->ops_fail);
  double success_rate = total_ops > 0 ? (double)r->ops_done / total_ops : 0.0;
  double dt = r->duration_sec > 0 ? r->duration_sec : 1e-9;
  double tput_ops = (double)r->ops_done / dt;
  double tput_mb = ((double)r->bytes_done / (1024.0 * 1024.0)) / dt;
  double avg_us = bench_latvec_avg_us(&r->latencies);

  fprintf(fp, "\n=== %s :: %s ===\n", cfg->label, workload_name(cfg->workload));
  fprintf(fp, "  devices=%s threads=%u\n",
          cfg->devices_csv ? cfg->devices_csv : "?", cfg->threads);
  fprintf(fp, "  key_size=%u..%u value_size=%u..%u keyspace=%" PRIu64 "\n",
          cfg->key_min, cfg->key_max, cfg->value_min, cfg->value_max,
          cfg->keyspace);
  fprintf(fp, "  duration=%.3fs warmup=%.3fs\n", r->duration_sec,
          cfg->warmup_sec);
  fprintf(fp, "  ops ok/fail=%" PRIu64 "/%" PRIu64 " success=%.4f\n",
          r->ops_done, r->ops_fail, success_rate);
  fprintf(fp, "  throughput=%.2f ops/s  %.3f MB/s\n", tput_ops, tput_mb);
  fprintf(
      fp,
      "  latency_us avg=%.3f p50=%.3f p95=%.3f p99=%.3f p999=%.3f max=%.3f\n",
      avg_us, (double)bench_latvec_percentile(&r->latencies, 0.50) / 1000.0,
      (double)bench_latvec_percentile(&r->latencies, 0.95) / 1000.0,
      (double)bench_latvec_percentile(&r->latencies, 0.99) / 1000.0,
      (double)bench_latvec_percentile(&r->latencies, 0.999) / 1000.0,
      (double)r->latencies.max_ns / 1000.0);
  fprintf(fp, "  cpu_user=%.3fs cpu_sys=%.3fs max_rss=%ld kB\n",
          r->user_cpu_sec, r->sys_cpu_sec, r->max_rss_kb);
  if (r->notes && *r->notes) {
    fprintf(fp, "  notes=%s\n", r->notes);
  }
  if (open_fp)
    fclose(open_fp);
}

/* -------------------------------------------------------------- */
/* Edge cases                                                      */
/* -------------------------------------------------------------- */

typedef struct {
  const char *name;
  const char *expectation;
  int passed; /* 1 = pass, 0 = fail */
  uint64_t latency_ns;
  const char *detail;
} edge_result_t;

static int run_edge_cases(kv_engine_t *engine, const cfg_t *cfg) {
  /* These cases are intentionally short and deterministic so the report can
   * confirm the engine handles boundary inputs correctly. */
  edge_result_t results[8];
  size_t nr = 0;

  char *vbuf_small = (char *)malloc(64);
  char *vbuf_big = (char *)malloc(2 * 1024 * 1024);
  if (!vbuf_small || !vbuf_big) {
    fprintf(stderr, "[edge] OOM\n");
    free(vbuf_small);
    free(vbuf_big);
    return -1;
  }
  memset(vbuf_small, 'q', 64);
  memset(vbuf_big, 'Z', 2 * 1024 * 1024);

  /* 1. Min-length key (4 bytes), small value */
  {
    edge_result_t er = {"min_key_len_4", "store+retrieve OK", 0, 0, ""};
    char key[4] = {'k', 'm', 'n', '4'};
    uint64_t t0 = bench_time_ns();
    kv_result_t res =
        kv_engine_store(engine, key, sizeof(key), vbuf_small, 64, true);
    if (res == KV_SUCCESS) {
      void *out = NULL;
      size_t out_len = 0;
      res = kv_engine_retrieve(engine, key, sizeof(key), &out, &out_len, false);
      if (res == KV_SUCCESS) {
        er.passed = 1;
        kv_engine_free_buffer(engine, out);
      } else {
        er.detail = bench_kv_result_string(res);
      }
    } else {
      er.detail = bench_kv_result_string(res);
    }
    er.latency_ns = bench_time_ns() - t0;
    results[nr++] = er;
  }

  /* 2. Max-length key (255 bytes), small value */
  {
    edge_result_t er = {"max_key_len_255", "store+retrieve OK", 0, 0, ""};
    char key[255];
    for (int i = 0; i < 255; i++)
      key[i] = 'a' + (i % 26);
    uint64_t t0 = bench_time_ns();
    kv_result_t res = kv_engine_store(engine, key, 255, vbuf_small, 64, true);
    if (res == KV_SUCCESS) {
      void *out = NULL;
      size_t out_len = 0;
      res = kv_engine_retrieve(engine, key, 255, &out, &out_len, false);
      if (res == KV_SUCCESS) {
        er.passed = 1;
        kv_engine_free_buffer(engine, out);
      } else {
        er.detail = bench_kv_result_string(res);
      }
    } else {
      er.detail = bench_kv_result_string(res);
    }
    er.latency_ns = bench_time_ns() - t0;
    results[nr++] = er;
  }

  /* 3. Small value (1 byte) */
  {
    edge_result_t er = {"value_1B", "store+retrieve OK", 0, 0, ""};
    const char *key = "edgeV01";
    char one = 'X';
    uint64_t t0 = bench_time_ns();
    kv_result_t res = kv_engine_store(engine, key, strlen(key), &one, 1, true);
    if (res == KV_SUCCESS) {
      void *out = NULL;
      size_t out_len = 0;
      res = kv_engine_retrieve(engine, key, strlen(key), &out, &out_len, false);
      if (res == KV_SUCCESS) {
        er.passed = (out_len == 1);
        if (!er.passed)
          er.detail = "unexpected length";
        kv_engine_free_buffer(engine, out);
      } else {
        er.detail = bench_kv_result_string(res);
      }
    } else {
      er.detail = bench_kv_result_string(res);
    }
    er.latency_ns = bench_time_ns() - t0;
    results[nr++] = er;
  }

  /* 4. Large value near 2 MiB (2 MiB - 4 KiB to leave headroom) */
  {
    edge_result_t er = {"value_near_max", "store+retrieve OK", 0, 0, ""};
    const char *key = "edgeBig8";
    size_t big = 2 * 1024 * 1024 - 4096;
    uint64_t t0 = bench_time_ns();
    kv_result_t res =
        kv_engine_store(engine, key, strlen(key), vbuf_big, big, true);
    if (res == KV_SUCCESS) {
      void *out = NULL;
      size_t out_len = 0;
      res = kv_engine_retrieve(engine, key, strlen(key), &out, &out_len, false);
      if (res == KV_SUCCESS) {
        er.passed = 1;
        kv_engine_free_buffer(engine, out);
      } else {
        er.detail = bench_kv_result_string(res);
      }
    } else {
      er.detail = bench_kv_result_string(res);
    }
    er.latency_ns = bench_time_ns() - t0;
    results[nr++] = er;
  }

  /* 5. Missing-key read returns KEY_NOT_FOUND */
  {
    edge_result_t er = {"missing_key_read", "expects KEY_NOT_FOUND", 0, 0, ""};
    const char *key = "edgeMiss";
    void *out = NULL;
    size_t out_len = 0;
    uint64_t t0 = bench_time_ns();
    kv_result_t res =
        kv_engine_retrieve(engine, key, strlen(key), &out, &out_len, false);
    er.latency_ns = bench_time_ns() - t0;
    if (res == KV_ERR_KEY_NOT_FOUND) {
      er.passed = 1;
    } else if (res == KV_SUCCESS) {
      er.detail = "unexpected SUCCESS";
      kv_engine_free_buffer(engine, out);
    } else {
      er.detail = bench_kv_result_string(res);
    }
    results[nr++] = er;
  }

  /* 6. Duplicate insert with overwrite=false rejects (KEY_ALREADY_EXISTS) */
  {
    edge_result_t er = {"no_overwrite_collision", "expects KEY_ALREADY_EXISTS",
                        0, 0, ""};
    const char *key = "edgeNoOv";
    char val[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    kv_engine_store(engine, key, strlen(key), val, sizeof(val), true);
    uint64_t t0 = bench_time_ns();
    kv_result_t res =
        kv_engine_store(engine, key, strlen(key), val, sizeof(val), false);
    er.latency_ns = bench_time_ns() - t0;
    if (res == KV_ERR_KEY_ALREADY_EXISTS || res == KV_ERR_KEY_EXISTS) {
      er.passed = 1;
    } else {
      er.detail = bench_kv_result_string(res);
    }
    results[nr++] = er;
  }

  /* 7. Read-after-delete returns KEY_NOT_FOUND */
  {
    edge_result_t er = {"read_after_delete",
                        "expects KEY_NOT_FOUND after delete", 0, 0, ""};
    const char *key = "edgeRAD0";
    char val[8] = {9, 8, 7, 6, 5, 4, 3, 2};
    kv_engine_store(engine, key, strlen(key), val, sizeof(val), true);
    kv_engine_delete(engine, key, strlen(key));
    void *out = NULL;
    size_t out_len = 0;
    uint64_t t0 = bench_time_ns();
    kv_result_t res =
        kv_engine_retrieve(engine, key, strlen(key), &out, &out_len, false);
    er.latency_ns = bench_time_ns() - t0;
    if (res == KV_ERR_KEY_NOT_FOUND) {
      er.passed = 1;
    } else if (res == KV_SUCCESS) {
      er.detail = "unexpected SUCCESS";
      kv_engine_free_buffer(engine, out);
    } else {
      er.detail = bench_kv_result_string(res);
    }
    results[nr++] = er;
  }

  /* 8. Invalid key length (3 bytes) is rejected */
  {
    edge_result_t er = {"key_len_below_min", "expects INVALID_PARAM", 0, 0, ""};
    const char *key = "abc";
    char val = 'A';
    uint64_t t0 = bench_time_ns();
    kv_result_t res = kv_engine_store(engine, key, 3, &val, 1, true);
    er.latency_ns = bench_time_ns() - t0;
    if (res == KV_ERR_INVALID_PARAM) {
      er.passed = 1;
    } else {
      er.detail = bench_kv_result_string(res);
    }
    results[nr++] = er;
  }

  /* Emit one CSV row per edge case. */
  if (cfg->csv_path) {
    FILE *fp = bench_csv_open(cfg->csv_path, BENCH_CSV_HEADER);
    if (fp) {
      for (size_t i = 0; i < nr; i++) {
        char wk[96];
        snprintf(wk, sizeof(wk), "edge:%s", results[i].name);
        bench_csv_write_str(fp, cfg->label ? cfg->label : "");
        fputc(',', fp);
        bench_csv_write_str(fp, wk);
        fprintf(fp, ",-1,");
        bench_csv_write_str(fp, cfg->devices_csv ? cfg->devices_csv : "");
        double lat_us = (double)results[i].latency_ns / 1000.0;
        fprintf(
            fp,
            ",1,0.000,0.000,%d,%d,%.6f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
            "%u,%u,%u,%u,1,%u,0.000,0.000,0,",
            results[i].passed, !results[i].passed,
            results[i].passed ? 1.0 : 0.0,
            /* throughput / mb/s */ 0.0, 0.0,
            /* avg/p50/p95/p99/p999/max */
            lat_us, lat_us, lat_us, lat_us, lat_us, lat_us, cfg->key_min,
            cfg->key_max, cfg->value_min, cfg->value_max, cfg->seed);
        char note[160];
        if (results[i].detail && *results[i].detail) {
          snprintf(note, sizeof(note), "%s | %s", results[i].expectation,
                   results[i].detail);
        } else {
          snprintf(note, sizeof(note), "%s",
                   results[i].expectation ? results[i].expectation : "");
        }
        bench_csv_write_str(fp, note);
        fputc('\n', fp);
      }
      fflush(fp);
      fclose(fp);
    }
  }

  if (cfg->summary_path) {
    FILE *fp = fopen(cfg->summary_path, "a");
    if (fp) {
      fprintf(fp, "\n=== %s :: edge ===\n", cfg->label);
      for (size_t i = 0; i < nr; i++) {
        fprintf(fp, "  %-26s %s  (%.3f us) %s\n", results[i].name,
                results[i].passed ? "PASS" : "FAIL",
                (double)results[i].latency_ns / 1000.0,
                results[i].detail ? results[i].detail : "");
      }
      fclose(fp);
    }
  }

  fprintf(stdout, "\n=== %s :: edge ===\n", cfg->label);
  for (size_t i = 0; i < nr; i++) {
    fprintf(stdout, "  %-26s %s  (%.3f us) %s\n", results[i].name,
            results[i].passed ? "PASS" : "FAIL",
            (double)results[i].latency_ns / 1000.0,
            results[i].detail ? results[i].detail : "");
  }

  free(vbuf_small);
  free(vbuf_big);
  return 0;
}

/* -------------------------------------------------------------- */
/* Main                                                            */
/* -------------------------------------------------------------- */

int main(int argc, char **argv) {
  cfg_t cfg;
  cfg_set_defaults(&cfg);
  int parse_rc = parse_args(argc, argv, &cfg);
  if (parse_rc < 0)
    return 2;
  if (parse_rc > 0)
    return 0;

  bench_device_list_t devices;
  if (bench_parse_devices(cfg.devices_csv, &devices) != 0) {
    fprintf(stderr, "Failed to parse --devices=%s\n", cfg.devices_csv);
    return 2;
  }
  fprintf(stderr, "[stress] %s: workload=%s devices=%u threads=%u\n", cfg.label,
          workload_name(cfg.workload), devices.count, cfg.threads);

  bench_engine_opts_t opts = {0};
  opts.memory_pool_bytes = cfg.memory_pool_bytes;
  opts.queue_depth = cfg.queue_depth;
  opts.worker_threads = cfg.worker_threads;
  opts.dma_pool_count = cfg.dma_pool_count;
  opts.enable_stats = 1;

  kv_engine_t *engine = NULL;
  if (bench_init_engine_multi(&engine, &devices, &opts) != KV_SUCCESS) {
    bench_device_list_free(&devices);
    return 1;
  }

  /* Edge workload runs deterministic checks instead of timed throughput. */
  if (cfg.workload == WL_EDGE) {
    int rc = run_edge_cases(engine, &cfg);
    kv_engine_cleanup(engine);
    bench_device_list_free(&devices);
    return rc;
  }

  if (cfg.workload == WL_READ || cfg.workload == WL_DELETE ||
      cfg.workload == WL_EXISTS || cfg.workload == WL_MIXED) {
    if (prepopulate(engine, &cfg) != 0) {
      kv_engine_cleanup(engine);
      bench_device_list_free(&devices);
      return 1;
    }
  }

  /* Warm-up phase: a short single-threaded spin to prime caches/pools. */
  if (cfg.warmup_sec > 0.0) {
    fprintf(stderr, "[stress] warm-up %.2fs\n", cfg.warmup_sec);
    rng_t rng;
    rng_seed(&rng, (uint64_t)cfg.seed ^ 0xDEADBEEFULL);
    char *kbuf = (char *)malloc(cfg.key_max + 1);
    char *vbuf = (char *)malloc(cfg.value_max ? cfg.value_max : 1);
    if (kbuf && vbuf) {
      double end = now_seconds() + cfg.warmup_sec;
      uint64_t i = 0;
      while (now_seconds() < end) {
        workload_t op = cfg.workload;
        uint64_t key_idx;
        if (op == WL_MIXED) {
          uint32_t roll = (uint32_t)(rng_next(&rng) % 100);
          op = (roll < (uint32_t)cfg.read_percent) ? WL_READ : WL_WRITE;
        }
        if (op == WL_WRITE) {
          key_idx = (1ULL << 50) + i;
        } else {
          key_idx = rng_next(&rng) % cfg.keyspace;
        }
        size_t bytes = 0;
        switch (op) {
        case WL_WRITE:
          do_write(engine, kbuf, vbuf, &rng, &cfg, key_idx, &bytes);
          break;
        case WL_READ:
          do_read(engine, kbuf, &rng, &cfg, key_idx, &bytes);
          break;
        case WL_DELETE:
          do_delete(engine, kbuf, &rng, &cfg, key_idx);
          break;
        case WL_EXISTS:
          do_exists(engine, kbuf, &rng, &cfg, key_idx);
          break;
        default:
          break;
        }
        i++;
      }
    }
    free(kbuf);
    free(vbuf);
  }

  /* Spin up workers */
  worker_ctx_t *workers =
      (worker_ctx_t *)calloc(cfg.threads, sizeof(worker_ctx_t));
  pthread_t *tids = (pthread_t *)calloc(cfg.threads, sizeof(pthread_t));
  if (!workers || !tids) {
    fprintf(stderr, "[stress] OOM allocating worker arrays\n");
    free(workers);
    free(tids);
    kv_engine_cleanup(engine);
    bench_device_list_free(&devices);
    return 1;
  }

  /* Slice the keyspace among threads for read/delete/exists workloads. */
  uint64_t slice = (cfg.keyspace + cfg.threads - 1) / cfg.threads;
  uint64_t per_thread_budget = 0;
  if (cfg.ops > 0) {
    per_thread_budget = (cfg.ops + cfg.threads - 1) / cfg.threads;
  }

  double t_start = now_seconds();
  double t_end =
      (cfg.ops > 0) ? (t_start + 24.0 * 3600.0) : (t_start + cfg.duration_sec);

  struct rusage ru_before, ru_after;
  getrusage(RUSAGE_SELF, &ru_before);

  for (uint32_t i = 0; i < cfg.threads; i++) {
    workers[i].engine = engine;
    workers[i].cfg = &cfg;
    workers[i].thread_id = i;
    workers[i].deadline_sec = t_end;
    workers[i].ops_budget = per_thread_budget;
    workers[i].rng_seed = cfg.seed + i * 2654435761u;
    /* For workloads that depend on prepopulated keys, give every thread the
     * full keyspace to sample from so reads stay realistic. */
    if (cfg.workload == WL_DELETE) {
      workers[i].key_offset = i * slice;
      workers[i].key_count =
          (i * slice + slice <= cfg.keyspace)
              ? slice
              : (cfg.keyspace > i * slice ? cfg.keyspace - i * slice : 0);
      if (workers[i].key_count == 0)
        workers[i].key_count = 1;
    } else {
      workers[i].key_offset = 0;
      workers[i].key_count = cfg.keyspace;
    }
    bench_latvec_init(&workers[i].latencies, BENCH_LATVEC_DEFAULT_HARD_CAP,
                      workers[i].rng_seed);
    int rc = pthread_create(&tids[i], NULL, worker_entry, &workers[i]);
    if (rc != 0) {
      fprintf(stderr, "[stress] pthread_create failed: %s\n", strerror(rc));
      cfg.threads = i;
      break;
    }
  }
  for (uint32_t i = 0; i < cfg.threads; i++) {
    pthread_join(tids[i], NULL);
  }
  double t_done = now_seconds();
  getrusage(RUSAGE_SELF, &ru_after);

  /* Aggregate */
  run_result_t result;
  memset(&result, 0, sizeof(result));
  bench_latvec_init(&result.latencies,
                    (size_t)BENCH_LATVEC_DEFAULT_HARD_CAP * cfg.threads,
                    cfg.seed ^ 0xCAFEBABEu);
  for (uint32_t i = 0; i < cfg.threads; i++) {
    result.ops_done += workers[i].ops_done;
    result.ops_fail += workers[i].ops_fail;
    result.bytes_done += workers[i].bytes_done;
    bench_latvec_merge(&result.latencies, &workers[i].latencies);
    bench_latvec_free(&workers[i].latencies);
  }
  bench_latvec_sort(&result.latencies);
  result.duration_sec = t_done - t_start;
  capture_rusage(&ru_before, &ru_after, &result);

  emit_csv(&cfg, &result);
  emit_summary(&cfg, &result);

  bench_latvec_free(&result.latencies);
  free(workers);
  free(tids);
  kv_engine_cleanup(engine);
  bench_device_list_free(&devices);
  return 0;
}
