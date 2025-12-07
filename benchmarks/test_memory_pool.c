/**
 * Memory Pool Benchmark
 * Compares allocation performance: malloc vs memory pool
 */

#include "../src/utils/memory_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#define NUM_ALLOCS 10000
#define ALIGNMENT 8  /* Expected alignment in bytes */

typedef struct {
    size_t min_size;
    size_t max_size;
    const char* name;
} test_config_t;

/* Get current time in nanoseconds */
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Generate random size in range [min, max] */
static size_t random_size(size_t min, size_t max) {
    return min + (rand() % (max - min + 1));
}

/* Check if pointer is properly aligned */
static bool is_aligned(void* ptr, size_t alignment) {
    return ((uintptr_t)ptr % alignment) == 0;
}

/* Run benchmark for a specific size range */
static void run_benchmark(test_config_t config) {
    void* ptrs[NUM_ALLOCS];
    uint64_t start, end;
    double malloc_time_ms, pool_time_ms, speedup;
    size_t total_requested = 0;
    bool alignment_ok = true;

    printf("\n%s\n", config.name);
    printf("================================================================================\n");
    printf("Allocations: %d | Size range: %zu-%zu bytes\n\n",
           NUM_ALLOCS, config.min_size, config.max_size);

    /* ===== Benchmark 1: malloc (with cleanup) ===== */
    srand(42);
    start = get_time_ns();

    for (int i = 0; i < NUM_ALLOCS; i++) {
        size_t size = random_size(config.min_size, config.max_size);
        total_requested += size;
        ptrs[i] = malloc(size);
        if (!ptrs[i]) {
            fprintf(stderr, "malloc failed at iteration %d\n", i);
            return;
        }
    }

    for (int i = 0; i < NUM_ALLOCS; i++) {
        free(ptrs[i]);
    }

    end = get_time_ns();
    malloc_time_ms = (end - start) / 1000000.0;

    /* ===== Benchmark 2: Memory Pool (with cleanup) ===== */
    /* Calculate actual aligned size needed */
    size_t pool_size = 0;
    srand(42);
    for (int i = 0; i < NUM_ALLOCS; i++) {
        size_t size = random_size(config.min_size, config.max_size);
        /* Each allocation is aligned to 8 bytes */
        size_t aligned = ((size + 7) & ~7);
        pool_size += aligned;
    }
    /* Add 5% buffer for alignment of offsets */
    pool_size += pool_size / 20;

    memory_pool_t* pool = memory_pool_create(pool_size);
    if (!pool) {
        fprintf(stderr, "Failed to create memory pool\n");
        return;
    }

    srand(42);
    start = get_time_ns();

    for (int i = 0; i < NUM_ALLOCS; i++) {
        size_t size = random_size(config.min_size, config.max_size);
        ptrs[i] = memory_pool_alloc(pool, size);
        if (!ptrs[i]) {
            fprintf(stderr, "Pool allocation failed at iteration %d (requested: %zu bytes)\n", i, size);
            fprintf(stderr, "Pool stats: %zu/%zu bytes used (%.1f%% full)\n",
                    pool->used, pool->size, (pool->used * 100.0) / pool->size);
            memory_pool_destroy(pool);
            return;
        }
        /* Check alignment */
        if (!is_aligned(ptrs[i], ALIGNMENT)) {
            alignment_ok = false;
        }
    }

    size_t pool_used = pool->used;
    memory_pool_destroy(pool);

    end = get_time_ns();
    pool_time_ms = (end - start) / 1000000.0;

    /* ===== Results ===== */
    speedup = malloc_time_ms / pool_time_ms;

    printf("Performance:\n");
    printf("  malloc:       %.3f ms\n", malloc_time_ms);
    printf("  memory pool:  %.3f ms\n", pool_time_ms);
    printf("  Speedup:      %.2fx\n", speedup);

    printf("\nMemory Usage:\n");
    printf("  Requested:    %zu bytes (%.2f KB)\n",
           total_requested, total_requested / 1024.0);
    printf("  Pool size:    %zu bytes (%.2f KB)\n",
           pool_size, pool_size / 1024.0);
    printf("  Pool used:    %zu bytes (%.2f KB)\n",
           pool_used, pool_used / 1024.0);
    printf("  Utilization:  %.1f%%\n",
           (pool_used * 100.0) / pool_size);
    printf("  Overhead:     %zu bytes (%.1f%%)\n",
           pool_used - total_requested,
           ((pool_used - total_requested) * 100.0) / total_requested);

    printf("\nAlignment Check:\n");
    printf("  %zu-byte alignment: %s\n",
           ALIGNMENT, alignment_ok ? "PASS" : "FAIL");

    if (speedup > 1.0) {
        printf("\n\u2713 Memory pool is %.1f%% faster than malloc\n",
               (speedup - 1.0) * 100.0);
    } else {
        printf("\n\u2717 malloc is %.1f%% faster than memory pool\n",
               (1.0 / speedup - 1.0) * 100.0);
    }
}

int main(void) {
    test_config_t tests[] = {
        {8, 32, "Test 1: Small Allocations (8-32 bytes)"},
        {64, 256, "Test 2: Medium Allocations (64-256 bytes)"},
        {512, 2048, "Test 3: Large Allocations (512-2048 bytes)"},
        {8, 2048, "Test 4: Mixed Size Allocations (8-2048 bytes)"}
    };

    printf("Memory Pool Benchmark Suite\n");
    printf("================================================================================\n");

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        run_benchmark(tests[i]);
    }

    printf("\n");
    return 0;
}
