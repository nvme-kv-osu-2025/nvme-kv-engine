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

void print_results(double start_time, int write_success, int write_fail) {
    double write_time = get_time_seconds() - start_time;
    double write_throughput = (write_success + write_fail) / write_time;
    double write_latency_us = (write_time * 1000000.0) / (write_success + write_fail);
    double write_bandwidth_mbps = ((write_success + write_fail) * VALUE_SIZE) / (write_time * 1024 * 1024);
    double write_success_rate = (double)write_success / (double)(write_success + write_fail);
    
    printf("\n  Duration: %.2f seconds\n", write_time);
    printf("  Throughput: %.2f ops/sec\n", write_throughput);
    printf("  Latency: %.2f μs\n", write_latency_us);
    printf("  Bandwidth: %.2f MB/s\n", write_bandwidth_mbps);
    printf("  Success: %d, Failures: %d, Success rate: %.2f%%\n", write_success, write_fail, write_success_rate * 100.0);

    /* CSV-friendly line for automation */
    // printf("CSV,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
    //     num_ops,
    //     write_throughput, write_latency_us, write_bandwidth_mbps, write_success_rate * 100.0,
    //     read_throughput, read_latency_us, read_bandwidth_mbps, read_success_rate * 100.0);

}

int throughput_testing(kv_engine_t* engine, int num_ops) {
    printf("\n=== Testing with %d operations ===\n", num_ops);

    /* Allocate buffers */
    char* key_buffer = malloc(KEY_SIZE);
    char* value_buffer = malloc(VALUE_SIZE);
    if (!key_buffer || !value_buffer) {
        fprintf(stderr, "Failed to allocate buffers\n");
        free(key_buffer);
        free(value_buffer);
        return -1;
    }
    memset(value_buffer, 'X', VALUE_SIZE);

    /* ========== WRITE BENCHMARK ========== */
    printf("Running WRITE benchmark...\n");
    kv_engine_reset_stats(engine);

    int write_success = 0, write_fail = 0;

    double start_time = get_time_seconds();

    for (int i = 0; i < num_ops; i++) {
        snprintf(key_buffer, KEY_SIZE, "key%012d", i);

        kv_result_t result = kv_engine_store(engine, key_buffer, KEY_SIZE,
                                            value_buffer, VALUE_SIZE);
        if (result != KV_SUCCESS) {
            write_fail++;
            fprintf(stderr, "Store failed at iteration %d: %d\n", i, result);
        } else {
            write_success++;
        }

        if ((i + 1) % 10000 == 0) {
            printf("  Progress: %d/%d\r", i + 1, num_ops);
            fflush(stdout);
        }
    }

    print_results(start_time, write_success, write_fail);

    /* ========== READ BENCHMARK ========== */
    printf("\nRunning READ benchmark...\n");
    kv_engine_reset_stats(engine);

    int read_success = 0, read_fail = 0;

    start_time = get_time_seconds();

    for (int i = 0; i < num_ops; i++) {
        snprintf(key_buffer, KEY_SIZE, "key%012d", i);

        void* retrieved = NULL;
        size_t retrieved_len = 0;

        kv_result_t result = kv_engine_retrieve(engine, key_buffer, KEY_SIZE, &retrieved, &retrieved_len);
        if (result == KV_SUCCESS) {
            read_success++;
            free(retrieved);
        } else {
            read_fail++;
        }

        if ((i + 1) % 10000 == 0) {
            printf("  Progress: %d/%d\r", i + 1, num_ops);
            fflush(stdout);
        }
    }

    print_results(start_time, read_success, read_fail);    

    /* Cleanup */
    free(key_buffer);
    free(value_buffer);

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* device_path = argv[1];

    /* Initialize engine */
    kv_engine_t* engine;
    if (init_engine(&engine, device_path, NULL) != KV_SUCCESS) {
        return 1;
    }

    int testing_sizes[] = {2000, 64000, 128000, 256000, 512000, 1024000};
    int num_tests = sizeof(testing_sizes) / sizeof(testing_sizes[0]);

    for (int i = 0; i < num_tests; i++) {
        if (throughput_testing(engine, testing_sizes[i]) != 0) {
            fprintf(stderr, "Test with %d ops failed\n", testing_sizes[i]);
        }
    }

    /* Cleanup */
    kv_engine_cleanup(engine);

    return 0;

}
