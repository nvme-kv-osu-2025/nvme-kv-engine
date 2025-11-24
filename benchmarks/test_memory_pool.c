/**
 * Memory Pool Benchmark
 * Compares allocation performance: malloc vs memory pool
 */

#include "../src/utils/memory_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

#define NUM_ALLOCS 10000
#define MIN_SIZE 64
#define MAX_SIZE 256

/* Get current time in nanoseconds */
static uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Generate random size in range [MIN_SIZE, MAX_SIZE] */
static size_t random_size(void) {
    return MIN_SIZE + (rand() % (MAX_SIZE - MIN_SIZE + 1));
}

int main(void) {
    void* ptrs[NUM_ALLOCS];
    uint64_t start, end;
    double malloc_time_ms, pool_time_ms, speedup;

    printf("Memory Pool Benchmark\n");
    printf("=====================\n");
    printf("Allocations: %d\n", NUM_ALLOCS);
    printf("Size range: %d-%d bytes\n\n", MIN_SIZE, MAX_SIZE);

    srand(42);  /* Fixed seed for reproducibility */

    /* ===== Benchmark 1: malloc ===== */
    start = get_time_ns();

    for (int i = 0; i < NUM_ALLOCS; i++) {
        size_t size = random_size();
        ptrs[i] = malloc(size);
        if (!ptrs[i]) {
            fprintf(stderr, "malloc failed at iteration %d\n", i);
            return 1;
        }
    }

    for (int i = 0; i < NUM_ALLOCS; i++) {
        free(ptrs[i]);
    }

    end = get_time_ns();
    malloc_time_ms = (end - start) / 1000000.0;

    /* ===== Benchmark 2: Memory Pool ===== */
    /* Calculate total memory needed */
    size_t total_size = 0;
    srand(42);  /* Reset seed for same allocation pattern */
    for (int i = 0; i < NUM_ALLOCS; i++) {
        total_size += random_size();
    }

    memory_pool_t* pool = memory_pool_create(total_size);
    if (!pool) {
        fprintf(stderr, "Failed to create memory pool\n");
        return 1;
    }

    srand(42);  /* Reset seed again */
    start = get_time_ns();

    for (int i = 0; i < NUM_ALLOCS; i++) {
        size_t size = random_size();
        ptrs[i] = memory_pool_alloc(pool, size);
        if (!ptrs[i]) {
            fprintf(stderr, "Pool allocation failed at iteration %d\n", i);
            return 1;
        }
    }

    /* No individual frees needed - that's the point! */
    memory_pool_destroy(pool);

    end = get_time_ns();
    pool_time_ms = (end - start) / 1000000.0;

    /* ===== Results ===== */
    speedup = malloc_time_ms / pool_time_ms;

    printf("Results:\n");
    printf("--------\n");
    printf("malloc:       %.3f ms\n", malloc_time_ms);
    printf("memory pool:  %.3f ms\n", pool_time_ms);
    printf("Speedup:      %.2fx faster\n\n", speedup);

    if (speedup > 1.0) {
        printf("Memory pool is %.1f%% faster than malloc\n",
               (speedup - 1.0) * 100.0);
    } else {
        printf("malloc is %.1f%% faster than memory pool\n",
               (1.0 / speedup - 1.0) * 100.0);
    }

    return 0;
}
