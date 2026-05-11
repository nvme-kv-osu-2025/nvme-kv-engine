/**
 * Benchmark Utilities
 *
 * Shared helpers used by capstone-grade KV engine benchmarks:
 *   - High-resolution timing
 *   - Engine bring-up with sensible defaults (single- and multi-device)
 *   - Comma-separated device list parsing
 *   - Latency vectors with percentile computation
 *   - CSV header + row helpers for mentor-ready summaries
 */

#ifndef BENCH_UTILS_H
#define BENCH_UTILS_H

#include "kv_engine.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Timing                                                             */
/* ------------------------------------------------------------------ */

double bench_time_seconds(void);
uint64_t bench_time_ns(void);

/* Backwards compatible alias used by older benchmarks */
double get_time_seconds(void);

/* ------------------------------------------------------------------ */
/* Device list parsing                                                */
/* ------------------------------------------------------------------ */

#define BENCH_MAX_DEVICES KV_MAX_DEVICES

typedef struct {
  char *raw;                            /* owned, NUL-separated */
  const char *paths[BENCH_MAX_DEVICES]; /* points into raw */
  uint32_t count;
} bench_device_list_t;

/**
 * Parse a comma-separated list (e.g. "/dev/kvemul0,/dev/kvemul1") into a
 * bench_device_list_t. Returns 0 on success, non-zero on failure.
 * Caller must call bench_device_list_free.
 */
int bench_parse_devices(const char *csv, bench_device_list_t *out);
void bench_device_list_free(bench_device_list_t *list);

/* ------------------------------------------------------------------ */
/* Engine bring-up                                                    */
/* ------------------------------------------------------------------ */

/**
 * Capstone benchmark configuration knobs. Zeroed defaults => use
 * conservative built-ins.
 */
typedef struct {
  size_t memory_pool_bytes; /* default 64 MiB */
  uint32_t queue_depth;     /* default 128    */
  uint32_t worker_threads;  /* default 0 (sync only)  */
  uint32_t dma_pool_count;  /* default 16     */
  uint32_t enable_stats;    /* default 1      */
} bench_engine_opts_t;

/**
 * Resolve the path to the KVSSD emulator config file by checking, in order:
 *   - $KVSSD_EMU_CONFIGFILE
 *   - /user/lib/KVSSD/PDK/core/kvssd_emul.conf  (Docker layout)
 *   - /kvssd/PDK/core/kvssd_emul.conf           (legacy fallback)
 * Returns the first existing path or the last fallback if none exist.
 */
const char *bench_default_emul_config(void);

/**
 * Initialize the engine using the provided device list and option overrides.
 * Returns KV_SUCCESS on success.
 */
kv_result_t bench_init_engine_multi(kv_engine_t **engine,
                                    const bench_device_list_t *devices,
                                    const bench_engine_opts_t *opts);

/**
 * Backwards-compatible single-device initializer used by legacy benchmarks.
 */
kv_result_t init_engine(kv_engine_t **engine, const char *device_path,
                        const kv_engine_config_t *config);

const char *bench_kv_result_string(kv_result_t result);

/* ------------------------------------------------------------------ */
/* Latency vectors                                                    */
/* ------------------------------------------------------------------ */

#define BENCH_LATVEC_DEFAULT_HARD_CAP (2u * 1024u * 1024u)

typedef struct {
  uint64_t *samples;  /* nanoseconds */
  size_t count;       /* samples retained */
  size_t cap;         /* allocated capacity */
  size_t hard_cap;    /* maximum retained samples (reservoir threshold) */
  uint64_t observed;  /* total samples observed (incl. dropped) */
  uint64_t max_ns;    /* maximum latency seen */
  uint64_t total_ns;  /* sum of observed latencies */
  uint32_t rng_state; /* xorshift state for reservoir replacement */
} bench_latvec_t;

void bench_latvec_init(bench_latvec_t *lv, size_t hard_cap, uint32_t seed);
void bench_latvec_free(bench_latvec_t *lv);
void bench_latvec_record(bench_latvec_t *lv, uint64_t latency_ns);

/* Merge `src` samples into `dst` (sorted later via bench_latvec_sort). */
void bench_latvec_merge(bench_latvec_t *dst, const bench_latvec_t *src);
void bench_latvec_sort(bench_latvec_t *lv);

/* Percentile in [0,1]; lv must be sorted. Returns nanoseconds. */
uint64_t bench_latvec_percentile(const bench_latvec_t *lv, double p);
double bench_latvec_avg_us(const bench_latvec_t *lv);

/* ------------------------------------------------------------------ */
/* CSV writers                                                        */
/* ------------------------------------------------------------------ */

/**
 * Open a CSV file for appending; if the file is empty the header is written.
 * Returns NULL on failure.
 */
FILE *bench_csv_open(const char *path, const char *header);

/* Helpers for safely writing optional/quoted fields */
void bench_csv_write_str(FILE *fp, const char *value);

#ifdef __cplusplus
}
#endif

#endif /* BENCH_UTILS_H */
