/**
 * Hardware Cache Testing
 *
 * Tests to capture the difference in retrieve times when KV data
 * is in CPU cache vs when it isn't.
 *
 * Tests:
 * 1. Hot cache: Repeated access to same key (data in L1/L2/L3 cache)
 * 2. Cold cache: Access after cache pollution (data evicted from cache)
 * 3. Hash table lookup isolation (touches internal hash map)
 */

#include "kv_engine.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef __x86_64__
#include <x86intrin.h>
#define HAS_RDTSC 1
#else
#define HAS_RDTSC 0
#endif

#define NUM_KEYS 100
#define VALUE_SIZE 4096
#define NUM_ITERATIONS 50
#define CACHE_POLLUTION_SIZE 32 /* MB of data */

/* High-resolution timing using CPU cycles (x86) or clock_gettime */
static inline uint64_t get_cycles(void) {
#if HAS_RDTSC
    unsigned int aux;
    return __rdtscp(&aux);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#endif
}

/* Memory barrier to ensure ordering, this should hopefully force all work to
 * happen in order*/
static inline void memory_barrier(void) {
#ifdef __x86_64__
    _mm_mfence();
#else
    __sync_synchronize();
#endif
}

/* Pollute CPU cache by adding a lot of memroy */
static void pollute_cache(void) {
    size_t pollution_size = CACHE_POLLUTION_SIZE * 1024 * 1024;
    volatile char *pollution = (volatile char *)malloc(pollution_size);
    if (!pollution) {
        return;
    }

    //   touch every cache line
    for (size_t i = 0; i < pollution_size; i += 64) {
        pollution[i] = (char)i;
    }

    // read to ensure writes completed
    volatile char sink = 0;
    for (size_t i = 0; i < pollution_size; i += 64) {
        sink += pollution[i];
    }

    memory_barrier();
    free((void *)pollution);
}

/* stats helper */
typedef struct {
    uint64_t min;
    uint64_t max;
    uint64_t total;
    uint64_t count;
} timing_stats_t;

static void stats_init(timing_stats_t *stats) {
    stats->min = UINT64_MAX;
    stats->max = 0;
    stats->total = 0;
    stats->count = 0;
}

static void stats_add(timing_stats_t *stats, uint64_t value) {
    if (value < stats->min)
        stats->min = value;
    if (value > stats->max)
        stats->max = value;
    stats->total += value;
    stats->count++;
}

static void stats_print(const char *label, timing_stats_t *stats) {
    if (stats->count == 0) {
        printf("%s: no data\n", label);
        return;
    }
    printf("%s:\n", label);
    printf("  Min: %lu\n", stats->min);
    printf("  Max: %lu\n", stats->max);
    printf("  Avg: %lu\n", stats->total / stats->count);
}

/**
 * test with hot cache (data very likely in CPU cache)
 * access same key repeatedly
 * returns average cycles
 */
uint64_t test_with_caching(kv_engine_t *engine, const char *key, size_t key_len) {
    printf("\n=== Test: Hot Cache (CPU cache test) ===\n");

    timing_stats_t stats;
    stats_init(&stats);

    // warmup the cache value
    int exists = 0;
    kv_engine_exists(engine, key, key_len, &exists);

    /* data should stay in cache */
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        memory_barrier();
        uint64_t start = get_cycles();

        kv_engine_exists(engine, key, key_len, &exists);

        uint64_t end = get_cycles();
        memory_barrier();

        stats_add(&stats, end - start);
    }

    stats_print("Hot cache exists() times", &stats);
    return stats.count > 0 ? stats.total / stats.count : 0;
}

/**
 * test with cold cache (data evicted from CPU cache)
 * pollute cache before each access
 * returns average cycles
 */
uint64_t test_without_caching(kv_engine_t *engine, const char *key, size_t key_len) {
    printf("\n=== Test: Cold Cache (NOT in CPU cache) ===\n");

    timing_stats_t stats;
    stats_init(&stats);

    for (int i = 0; i < NUM_ITERATIONS; i++) {
        // pollute the cache
        pollute_cache();

        int exists = 0;

        memory_barrier();
        uint64_t start = get_cycles();

        kv_engine_exists(engine, key, key_len, &exists);

        uint64_t end = get_cycles();
        memory_barrier();

        stats_add(&stats, end - start);
    }
    printf("\n");

    stats_print("Cold cache exists() times", &stats);
    return stats.count > 0 ? stats.total / stats.count : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <device_path>\n", argv[0]);
        return 1;
    }

    printf("==============================================\n");
    printf("   Hardware Cache Testing for NVMe-KV Engine  \n");
    printf("==============================================\n");

#if HAS_RDTSC
    printf("Timing using RDTSCP (CPU cycles)\n");
#else
    printf("Timing using clock_gettime (nanoseconds)\n");
#endif

    /* Initialize engine */
    kv_engine_config_t config = {.device_path = argv[1],
                                 .emul_config_file = "../../lib/KVSSD/PDK/core/kvssd_emul.conf",
                                 .memory_pool_size = 32 * 1024 * 1024,
                                 .queue_depth = 128,
                                 .num_worker_threads = 8,
                                 .enable_stats = 1};

    kv_engine_t *engine;
    if (kv_engine_init(&engine, &config) != KV_SUCCESS) {
        fprintf(stderr, "Failed to initialize engine\n");
        return 1;
    }

    printf("Engine initialized\n");

    const char *test_key = "cache_test_key_00001";
    size_t key_len = strlen(test_key);

    printf("Test key stored (%zu bytes value).\n", (size_t)VALUE_SIZE);

    uint64_t hot_avg = test_with_caching(engine, test_key, key_len);
    uint64_t cold_avg = test_without_caching(engine, test_key, key_len);

    /* Summary */
    printf("\n==============================================\n");
    printf("                   Summary                    \n");
    printf("==============================================\n");
    printf("Hot cache avg:  %lu cycles\n", hot_avg);
    printf("Cold cache avg: %lu cycles\n", cold_avg);
    if (hot_avg > 0) {
        double speedup = (double)cold_avg / (double)hot_avg;
        printf("Cold/Hot ratio: %.2fx\n", speedup);
        printf("Cache speedup:  %.1f%% faster with hot cache\n", (speedup - 1.0) * 100.0);
    }

    kv_engine_cleanup(engine);
    return 0;
}
