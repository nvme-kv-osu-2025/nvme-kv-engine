/**
 * Throughput Benchmark
 *
 * Measures read/write throughput and latency
 */

#include "kv_engine.h"
#include "util/bench_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_NUM_OPS 100000
#define KEY_SIZE 16
#define VALUE_SIZE 4096

void print_usage(const char* prog) {
    fprintf(stderr, "Usage: %s <device_path> [num_ops]\n", prog);
    fprintf(stderr, "  device_path: Path to NVMe device (e.g., /dev/kvemul)\n");
    fprintf(stderr, "  num_ops:     Number of operations (default: %d)\n", DEFAULT_NUM_OPS);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* device_path = argv[1];
    int num_ops = (argc > 2) ? atoi(argv[2]) : DEFAULT_NUM_OPS;

    printf("=== NVMe-KV Engine Throughput Benchmark ===\n");
    printf("Device: %s\n", device_path);
    printf("Operations: %d\n", num_ops);
    printf("Key size: %d bytes\n", KEY_SIZE);
    printf("Value size: %d bytes\n\n", VALUE_SIZE);

    /* Initialize engine */
    kv_engine_t* engine;
    if (init_engine(&engine, device_path, NULL) != KV_SUCCESS) {
        return 1;
    }

    /* Allocate buffers */
    char* key_buffer = malloc(KEY_SIZE);
    char* value_buffer = malloc(VALUE_SIZE);
    memset(value_buffer, 'X', VALUE_SIZE);

    /* ========== WRITE BENCHMARK ========== */
    printf("Running WRITE benchmark...\n");
    kv_engine_reset_stats(engine);

    double start_time = get_time_seconds();

    for (int i = 0; i < num_ops; i++) {
        snprintf(key_buffer, KEY_SIZE, "key%012d", i);

        kv_result_t result = kv_engine_store(engine, key_buffer, KEY_SIZE,
                                            value_buffer, VALUE_SIZE);
        if (result != KV_SUCCESS) {
            fprintf(stderr, "Store failed at iteration %d: %d\n", i, result);
            break;
        }

        if ((i + 1) % 10000 == 0) {
            printf("  Progress: %d/%d\r", i + 1, num_ops);
            fflush(stdout);
        }
    }

    double write_time = get_time_seconds() - start_time;
    double write_throughput = num_ops / write_time;
    double write_latency_us = (write_time * 1000000.0) / num_ops;
    double write_bandwidth_mbps = (num_ops * VALUE_SIZE) / (write_time * 1024 * 1024);

    printf("\n  Duration: %.2f seconds\n", write_time);
    printf("  Throughput: %.2f ops/sec\n", write_throughput);
    printf("  Latency: %.2f μs\n", write_latency_us);
    printf("  Bandwidth: %.2f MB/s\n", write_bandwidth_mbps);

    /* ========== READ BENCHMARK ========== */
    printf("\nRunning READ benchmark...\n");
    kv_engine_reset_stats(engine);

    start_time = get_time_seconds();

    for (int i = 0; i < num_ops; i++) {
        snprintf(key_buffer, KEY_SIZE, "key%012d", i);

        void* retrieved = NULL;
        size_t retrieved_len = 0;

        kv_result_t result = kv_engine_retrieve(engine, key_buffer, KEY_SIZE,
                                                &retrieved, &retrieved_len);
        if (result == KV_SUCCESS) {
            free(retrieved);
        }

        if ((i + 1) % 10000 == 0) {
            printf("  Progress: %d/%d\r", i + 1, num_ops);
            fflush(stdout);
        }
    }

    double read_time = get_time_seconds() - start_time;
    double read_throughput = num_ops / read_time;
    double read_latency_us = (read_time * 1000000.0) / num_ops;
    double read_bandwidth_mbps = (num_ops * VALUE_SIZE) / (read_time * 1024 * 1024);

    printf("\n  Duration: %.2f seconds\n", read_time);
    printf("  Throughput: %.2f ops/sec\n", read_throughput);
    printf("  Latency: %.2f μs\n", read_latency_us);
    printf("  Bandwidth: %.2f MB/s\n", read_bandwidth_mbps);

    /* Print summary */
    printf("\n=== Summary ===\n");
    printf("Write: %.2f Kops/sec, %.2f MB/s\n",
           write_throughput / 1000, write_bandwidth_mbps);
    printf("Read:  %.2f Kops/sec, %.2f MB/s\n",
           read_throughput / 1000, read_bandwidth_mbps);

    /* Cleanup */
    free(key_buffer);
    free(value_buffer);
    kv_engine_cleanup(engine);

    return 0;
}
