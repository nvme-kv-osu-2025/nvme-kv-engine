/**
 * Simple Cache Example
 *
 * Demonstrates using the KV engine as a cache
 */

#include "kv_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define NUM_OPERATIONS 1000

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <device_path>\n", argv[0]);
        return 1;
    }

    /* Initialize engine */
    kv_engine_config_t config = {
        .device_path = argv[1],
        .emul_config_file = "../../lib/KVSSD/PDK/core/kvssd_emul.conf",
        .memory_pool_size = 32 * 1024 * 1024,
        .queue_depth = 128,
        .num_worker_threads = 8,
        .enable_stats = 1
    };

    kv_engine_t* engine;
    if (kv_engine_init(&engine, &config) != KV_SUCCESS) {
        fprintf(stderr, "Failed to initialize engine\n");
        return 1;
    }

    printf("Running cache benchmark with %d operations...\n", NUM_OPERATIONS);

    clock_t start = clock();

    /* Populate cache */
    for (int i = 0; i < NUM_OPERATIONS; i++) {
        char key[32];
        char value[128];

        snprintf(key, sizeof(key), "cache_key_%06d", i);
        snprintf(value, sizeof(value), "cache_value_%06d_data", i);

        kv_result_t result = kv_engine_store(engine, key, strlen(key),
                                            value, strlen(value));
        if (result != KV_SUCCESS) {
            fprintf(stderr, "Store failed at iteration %d: %d\n", i, result);
            break;
        }

        if ((i + 1) % 100 == 0) {
            printf("Stored %d entries\r", i + 1);
            fflush(stdout);
        }
    }

    clock_t end = clock();
    double write_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("\nWrite phase completed in %.2f seconds\n", write_time);
    printf("Write throughput: %.2f ops/sec\n", NUM_OPERATIONS / write_time);

    /* Read back from cache */
    start = clock();
    int hits = 0;

    for (int i = 0; i < NUM_OPERATIONS; i++) {
        char key[32];
        snprintf(key, sizeof(key), "cache_key_%06d", i);

        void* value = NULL;
        size_t value_len = 0;

        kv_result_t result = kv_engine_retrieve(engine, key, strlen(key),
                                                &value, &value_len);
        if (result == KV_SUCCESS) {
            hits++;
            free(value);
        }

        if ((i + 1) % 100 == 0) {
            printf("Read %d entries\r", i + 1);
            fflush(stdout);
        }
    }

    end = clock();
    double read_time = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("\nRead phase completed in %.2f seconds\n", read_time);
    printf("Read throughput: %.2f ops/sec\n", NUM_OPERATIONS / read_time);
    printf("Cache hit rate: %.2f%%\n", (hits * 100.0) / NUM_OPERATIONS);

    /* Print final statistics */
    kv_engine_stats_t stats;
    kv_engine_get_stats(engine, &stats);

    printf("\n=== Final Statistics ===\n");
    printf("Total operations: %lu\n", stats.total_ops);
    printf("Write ops: %lu (%.2f MB)\n", stats.write_ops,
           stats.bytes_written / (1024.0 * 1024.0));
    printf("Read ops: %lu (%.2f MB)\n", stats.read_ops,
           stats.bytes_read / (1024.0 * 1024.0));
    printf("Failed ops: %lu\n", stats.failed_ops);

    kv_engine_cleanup(engine);
    return 0;
}
