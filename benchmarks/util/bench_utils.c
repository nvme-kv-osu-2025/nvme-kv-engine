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

static const char* kv_result_to_string(kv_result_t result) {
    switch (result) {
        case KV_SUCCESS: return "KV_SUCCESS";
        case KV_ERR_INVALID_PARAM: return "KV_ERR_INVALID_PARAM";
        case KV_ERR_NO_MEMORY: return "KV_ERR_NO_MEMORY";
        case KV_ERR_DEVICE_OPEN: return "KV_ERR_DEVICE_OPEN";
        case KV_ERR_KEY_NOT_FOUND: return "KV_ERR_KEY_NOT_FOUND";
        case KV_ERR_KEY_EXISTS: return "KV_ERR_KEY_EXISTS";
        case KV_ERR_VALUE_TOO_LARGE: return "KV_ERR_VALUE_TOO_LARGE";
        case KV_ERR_TIMEOUT: return "KV_ERR_TIMEOUT";
        case KV_ERR_IO: return "KV_ERR_IO";
        case KV_ERR_NOT_INITIALIZED: return "KV_ERR_NOT_INITIALIZED";
        default: return "UNKNOWN_ERROR";
    }
}

kv_result_t init_engine(kv_engine_t** engine, const char* device_path, const kv_engine_config_t* config) {
    kv_engine_config_t default_config = {
        .device_path = device_path,
        .emul_config_file = "/kvssd/PDK/core/kvssd_emul.conf",
        .memory_pool_size = 64 * 1024 * 1024,
        .queue_depth = 128,
        .num_worker_threads = 16,
        .enable_stats = 1
    };

    // Use provided config or fall back to defaults
    const kv_engine_config_t* active_config = config ? config : &default_config;

    kv_result_t result = kv_engine_init(engine, active_config);
    if (result != KV_SUCCESS) {
        fprintf(stderr, "Failed to initialize engine: %s (%d)\n",
                kv_result_to_string(result), result);
        return result;
    }

    printf("Engine initialized.\n\n");
    return KV_SUCCESS;
}
