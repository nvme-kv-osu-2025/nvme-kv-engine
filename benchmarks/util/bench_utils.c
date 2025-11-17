/**
 * Benchmark Utilities Implementation
 */

#include "bench_utils.h"
#include <stdio.h>
#include <time.h>

double get_time_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

kv_result_t init_engine(kv_engine_t** engine, const char* device_path) {
    kv_engine_config_t config = {
        .device_path = device_path,
        .emul_config_file = "/kvssd/PDK/core/kvssd_emul.conf",
        .memory_pool_size = 64 * 1024 * 1024,
        .queue_depth = 128,
        .num_worker_threads = 16,
        .enable_stats = 1
    };

    kv_result_t result = kv_engine_init(engine, &config);
    if (result != KV_SUCCESS) {
        fprintf(stderr, "Failed to initialize engine\n");
        return result;
    }

    printf("Engine initialized.\n\n");
    return KV_SUCCESS;
}
