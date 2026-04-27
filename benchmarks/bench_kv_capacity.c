/**
 * KV Engine Max Key Capacity Benchmark
 *
 * Sweeps a (key_size, value_size) matrix and inserts unique keys until either
 * the engine reports a sustained failure, or a configurable cap is reached.
 *
 * For each combination:
 *   - re-initializes the engine so the emulator state is fresh
 *   - inserts deterministic keys via kv_engine_store
 *   - records keys_stored, elapsed_sec, ops_per_sec, total bytes, last error
 *
 * Output:
 *   - one CSV row per (key_size, value_size) combination
 *   - human-readable summary text appended to --summary path
 *
 * Example:
 *   bench_kv_capacity \
 *       --devices /dev/kvemul0 \
 *       --key-sizes 16,64,255 \
 *       --value-sizes 512,4096,65536 \
 *       --max-keys 50000 --max-seconds 30 \
 *       --csv     /user/benchmarks/results/latest/kv_capacity.csv \
 *       --summary /user/benchmarks/results/latest/raw/capacity.txt
 */
#include "kv_engine.h"
#include "util/bench_utils.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#define BENCH_CAPACITY_HEADER                                                  \
  "label,devices,key_size,value_size,max_keys,max_seconds,keys_stored,"        \
  "first_failure_at,last_error,elapsed_sec,ops_per_sec,total_bytes,"           \
  "user_cpu_sec,sys_cpu_sec,max_rss_kb,notes"

#define MAX_SIZES 16

typedef struct {
  const char *label;
  const char *devices_csv;
  uint32_t key_sizes[MAX_SIZES];
  uint32_t key_sizes_count;
  uint32_t value_sizes[MAX_SIZES];
  uint32_t value_sizes_count;
  uint64_t max_keys;
  double max_seconds;
  bool stop_on_failure;
  uint32_t failure_threshold; /* number of consecutive failures to stop */
  uint32_t seed;
  size_t memory_pool_bytes;
  uint32_t queue_depth;
  uint32_t dma_pool_count;
  const char *csv_path;
  const char *summary_path;
} cfg_t;

static void cfg_set_defaults(cfg_t *c) {
  memset(c, 0, sizeof(*c));
  c->label = "capacity";
  c->key_sizes[0] = 16;
  c->key_sizes[1] = 64;
  c->key_sizes[2] = 255;
  c->key_sizes_count = 3;
  c->value_sizes[0] = 512;
  c->value_sizes[1] = 4096;
  c->value_sizes[2] = 65536;
  c->value_sizes_count = 3;
  c->max_keys = 100000;
  c->max_seconds = 60.0;
  c->stop_on_failure = true;
  c->failure_threshold = 5;
  c->seed = 1337;
  c->memory_pool_bytes = (size_t)64 * 1024 * 1024;
  c->queue_depth = 128;
  c->dma_pool_count = 16;
}

static int parse_uint_list(const char *csv, uint32_t *out, uint32_t *count) {
  if (!csv || !*csv)
    return -1;
  char *buf = strdup(csv);
  if (!buf)
    return -1;
  *count = 0;
  char *cur = buf;
  while (cur && *cur) {
    while (*cur == ' ' || *cur == '\t')
      cur++;
    if (!*cur)
      break;
    if (*count >= MAX_SIZES) {
      free(buf);
      return -1;
    }
    out[(*count)++] = (uint32_t)strtoul(cur, NULL, 10);
    char *comma = strchr(cur, ',');
    if (!comma)
      break;
    cur = comma + 1;
  }
  free(buf);
  return *count > 0 ? 0 : -1;
}

static void usage(const char *prog) {
  fprintf(
      stderr,
      "Usage: %s [options]\n"
      "  --label NAME\n"
      "  --devices /dev/kvemul0[,...]\n"
      "  --key-sizes  16,64,255\n"
      "  --value-sizes 512,4096,65536,1048576,2097152\n"
      "  --max-keys N            cap inserts per combination\n"
      "  --max-seconds N         time cap per combination\n"
      "  --stop-on-failure 0|1   stop combo on sustained failure (default 1)\n"
      "  --failure-threshold N   consecutive failures before stop (default 5)\n"
      "  --seed N\n"
      "  --memory-pool-bytes N --queue-depth N --dma-pool-count N\n"
      "  --csv PATH --summary PATH\n",
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
    } else if (!strcmp(a, "--devices")) {
      ARG_NEXT();
      c->devices_csv = argv[i];
    } else if (!strcmp(a, "--key-sizes")) {
      ARG_NEXT();
      if (parse_uint_list(argv[i], c->key_sizes, &c->key_sizes_count) != 0) {
        fprintf(stderr, "Invalid --key-sizes\n");
        return -1;
      }
    } else if (!strcmp(a, "--value-sizes")) {
      ARG_NEXT();
      if (parse_uint_list(argv[i], c->value_sizes, &c->value_sizes_count) !=
          0) {
        fprintf(stderr, "Invalid --value-sizes\n");
        return -1;
      }
    } else if (!strcmp(a, "--max-keys")) {
      ARG_NEXT();
      c->max_keys = strtoull(argv[i], NULL, 10);
    } else if (!strcmp(a, "--max-seconds")) {
      ARG_NEXT();
      c->max_seconds = atof(argv[i]);
    } else if (!strcmp(a, "--stop-on-failure")) {
      ARG_NEXT();
      c->stop_on_failure = atoi(argv[i]) != 0;
    } else if (!strcmp(a, "--failure-threshold")) {
      ARG_NEXT();
      c->failure_threshold = (uint32_t)atoi(argv[i]);
    } else if (!strcmp(a, "--seed")) {
      ARG_NEXT();
      c->seed = (uint32_t)strtoul(argv[i], NULL, 10);
    } else if (!strcmp(a, "--memory-pool-bytes")) {
      ARG_NEXT();
      c->memory_pool_bytes = strtoull(argv[i], NULL, 10);
    } else if (!strcmp(a, "--queue-depth")) {
      ARG_NEXT();
      c->queue_depth = (uint32_t)atoi(argv[i]);
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
    return -1;
  }
  for (uint32_t i = 0; i < c->key_sizes_count; i++) {
    if (c->key_sizes[i] < 4)
      c->key_sizes[i] = 4;
    if (c->key_sizes[i] > 255)
      c->key_sizes[i] = 255;
  }
  for (uint32_t i = 0; i < c->value_sizes_count; i++) {
    if (c->value_sizes[i] < 1)
      c->value_sizes[i] = 1;
    if (c->value_sizes[i] > 2 * 1024 * 1024)
      c->value_sizes[i] = 2 * 1024 * 1024;
  }
  if (c->max_keys == 0)
    c->max_keys = 1;
  if (c->max_seconds <= 0.0)
    c->max_seconds = 1.0;
  if (c->failure_threshold == 0)
    c->failure_threshold = 1;
  return 0;
}

/* -------------------------------------------------------------- */
/* Key/value generation                                            */
/* -------------------------------------------------------------- */

static void make_key(char *buf, uint32_t key_len, uint64_t idx, uint32_t k_size,
                     uint32_t v_size, uint32_t seed) {
  /* Use fixed prefix derived from (k_size, v_size, seed) so collisions across
   * combos are impossible. */
  char prefix[32];
  int n = snprintf(prefix, sizeof(prefix), "k%03u_v%07u_s%08x_", k_size, v_size,
                   seed);
  if (n < 0)
    n = 0;
  size_t plen = (size_t)n;
  if (plen > key_len)
    plen = key_len;
  memcpy(buf, prefix, plen);
  /* Remainder is hex of idx, then padded with letters. */
  char hex[32];
  int hn = snprintf(hex, sizeof(hex), "%016" PRIx64, idx);
  if (hn < 0)
    hn = 0;
  size_t hlen = (size_t)hn;
  if (plen + hlen > key_len)
    hlen = key_len - plen;
  memcpy(buf + plen, hex, hlen);
  for (size_t i = plen + hlen; i < key_len; i++) {
    buf[i] = 'a' + ((char)(i % 26));
  }
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
/* Per-combo run                                                   */
/* -------------------------------------------------------------- */

typedef struct {
  uint32_t key_size;
  uint32_t value_size;
  uint64_t keys_stored;
  uint64_t first_failure_at; /* op index of first failure (0 if none) */
  kv_result_t last_error;
  bool reached_cap;
  bool reached_time_limit;
  bool stopped_on_failure;
  double elapsed_sec;
  double ops_per_sec;
  uint64_t total_bytes;
  double user_cpu_sec;
  double sys_cpu_sec;
  long max_rss_kb;
} combo_result_t;

static double timeval_to_sec(struct timeval tv) {
  return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

static int run_combo(const cfg_t *cfg, const bench_device_list_t *devices,
                     uint32_t key_size, uint32_t value_size,
                     combo_result_t *out) {
  bench_engine_opts_t opts = {0};
  opts.memory_pool_bytes = cfg->memory_pool_bytes;
  opts.queue_depth = cfg->queue_depth;
  opts.dma_pool_count = cfg->dma_pool_count;
  opts.enable_stats = 1;

  kv_engine_t *engine = NULL;
  if (bench_init_engine_multi(&engine, devices, &opts) != KV_SUCCESS) {
    fprintf(stderr, "[capacity] init failed for k=%u v=%u\n", key_size,
            value_size);
    out->last_error = KV_ERR_DEVICE_OPEN;
    return -1;
  }

  char *kbuf = (char *)malloc(key_size + 1);
  char *vbuf = (char *)malloc(value_size > 0 ? value_size : 1);
  if (!kbuf || !vbuf) {
    free(kbuf);
    free(vbuf);
    kv_engine_cleanup(engine);
    out->last_error = KV_ERR_NO_MEMORY;
    return -1;
  }
  fill_value(vbuf, value_size,
             ((uint64_t)cfg->seed << 16) ^ ((uint64_t)key_size << 24) ^
                 value_size);

  struct rusage ru_before, ru_after;
  getrusage(RUSAGE_SELF, &ru_before);

  uint32_t consecutive_failures = 0;
  double t_start = bench_time_seconds();
  double t_deadline = t_start + cfg->max_seconds;

  uint64_t i = 0;
  for (; i < cfg->max_keys; i++) {
    make_key(kbuf, key_size, i, key_size, value_size, cfg->seed);
    kv_result_t res =
        kv_engine_store(engine, kbuf, key_size, vbuf, value_size, true);
    if (res == KV_SUCCESS) {
      out->keys_stored++;
      out->total_bytes += (uint64_t)key_size + (uint64_t)value_size;
      consecutive_failures = 0;
    } else {
      out->last_error = res;
      if (out->first_failure_at == 0)
        out->first_failure_at = i + 1;
      consecutive_failures++;
      if (cfg->stop_on_failure &&
          consecutive_failures >= cfg->failure_threshold) {
        out->stopped_on_failure = true;
        i++;
        break;
      }
    }
    if ((i & 0x3FF) == 0x3FF && bench_time_seconds() >= t_deadline) {
      out->reached_time_limit = true;
      i++;
      break;
    }
  }

  out->elapsed_sec = bench_time_seconds() - t_start;
  if (out->elapsed_sec > 0.0) {
    out->ops_per_sec = (double)i / out->elapsed_sec;
  }
  if (i >= cfg->max_keys)
    out->reached_cap = true;

  getrusage(RUSAGE_SELF, &ru_after);
  out->user_cpu_sec =
      timeval_to_sec(ru_after.ru_utime) - timeval_to_sec(ru_before.ru_utime);
  out->sys_cpu_sec =
      timeval_to_sec(ru_after.ru_stime) - timeval_to_sec(ru_before.ru_stime);
  out->max_rss_kb = ru_after.ru_maxrss;

  free(kbuf);
  free(vbuf);
  kv_engine_cleanup(engine);
  return 0;
}

/* -------------------------------------------------------------- */
/* CSV / summary                                                   */
/* -------------------------------------------------------------- */

static void emit_csv(const cfg_t *cfg, const combo_result_t *r) {
  if (!cfg->csv_path)
    return;
  FILE *fp = bench_csv_open(cfg->csv_path, BENCH_CAPACITY_HEADER);
  if (!fp)
    return;

  bench_csv_write_str(fp, cfg->label ? cfg->label : "");
  fputc(',', fp);
  bench_csv_write_str(fp, cfg->devices_csv ? cfg->devices_csv : "");
  fprintf(fp,
          ",%u,%u,%" PRIu64 ",%.3f,%" PRIu64 ",%" PRIu64
          ",%s,%.4f,%.2f,%" PRIu64 ",%.3f,%.3f,%ld,",
          r->key_size, r->value_size, cfg->max_keys, cfg->max_seconds,
          r->keys_stored, r->first_failure_at,
          bench_kv_result_string(r->last_error), r->elapsed_sec, r->ops_per_sec,
          r->total_bytes, r->user_cpu_sec, r->sys_cpu_sec, r->max_rss_kb);

  char note[160];
  snprintf(note, sizeof(note), "%s%s%s", r->reached_cap ? "cap_reached " : "",
           r->reached_time_limit ? "time_limit " : "",
           r->stopped_on_failure ? "stopped_on_failure" : "");
  bench_csv_write_str(fp, note);
  fputc('\n', fp);

  fflush(fp);
  fclose(fp);
}

static void emit_summary(const cfg_t *cfg, const combo_result_t *r) {
  FILE *fp = stdout;
  FILE *open_fp = NULL;
  if (cfg->summary_path) {
    open_fp = fopen(cfg->summary_path, "a");
    if (open_fp)
      fp = open_fp;
  }
  fprintf(fp,
          "  k=%4u v=%9u keys=%10" PRIu64
          " elapsed=%7.3fs %9.1f ops/s bytes=%12" PRIu64 "  err=%s%s\n",
          r->key_size, r->value_size, r->keys_stored, r->elapsed_sec,
          r->ops_per_sec, r->total_bytes, bench_kv_result_string(r->last_error),
          r->stopped_on_failure
              ? " (stopped on failure)"
              : (r->reached_cap
                     ? " (cap reached)"
                     : (r->reached_time_limit ? " (time limit)" : "")));
  if (open_fp)
    fclose(open_fp);
}

/* -------------------------------------------------------------- */
/* Main                                                            */
/* -------------------------------------------------------------- */

int main(int argc, char **argv) {
  cfg_t cfg;
  cfg_set_defaults(&cfg);
  int rc = parse_args(argc, argv, &cfg);
  if (rc < 0)
    return 2;
  if (rc > 0)
    return 0;

  bench_device_list_t devices;
  if (bench_parse_devices(cfg.devices_csv, &devices) != 0) {
    fprintf(stderr, "Failed to parse --devices=%s\n", cfg.devices_csv);
    return 2;
  }

  fprintf(stdout,
          "[capacity] %s devices=%u key_sizes=%u value_sizes=%u "
          "max_keys=%" PRIu64 " max_seconds=%.1f\n",
          cfg.label, devices.count, cfg.key_sizes_count, cfg.value_sizes_count,
          cfg.max_keys, cfg.max_seconds);

  if (cfg.summary_path) {
    FILE *fp = fopen(cfg.summary_path, "a");
    if (fp) {
      fprintf(fp, "\n=== %s :: capacity matrix ===\n", cfg.label);
      fprintf(fp, "  devices=%s max_keys=%" PRIu64 " max_seconds=%.1f\n",
              cfg.devices_csv, cfg.max_keys, cfg.max_seconds);
      fclose(fp);
    }
  }

  for (uint32_t ki = 0; ki < cfg.key_sizes_count; ki++) {
    for (uint32_t vi = 0; vi < cfg.value_sizes_count; vi++) {
      combo_result_t result;
      memset(&result, 0, sizeof(result));
      result.key_size = cfg.key_sizes[ki];
      result.value_size = cfg.value_sizes[vi];
      result.last_error = KV_SUCCESS;

      run_combo(&cfg, &devices, cfg.key_sizes[ki], cfg.value_sizes[vi],
                &result);
      emit_csv(&cfg, &result);
      emit_summary(&cfg, &result);
    }
  }

  bench_device_list_free(&devices);
  return 0;
}
