/**
 * Benchmark Utilities
 *
 * Common utility functions for benchmarking the KV engine
 */

#ifndef BENCH_UTILS_H
#define BENCH_UTILS_H

#include "kv_engine.h"

/**
 * Get current time in seconds with high precision
 *
 * @return Current time in seconds as a double
 */
double get_time_seconds(void);

/**
 * Initialize the KV engine with standard benchmark configuration
 *
 * @param engine Pointer to receive the initialized engine
 * @param device_path Path to NVMe device
 * @return KV_SUCCESS on success, error code otherwise
 */
kv_result_t init_engine(kv_engine_t** engine, const char* device_path);

#endif /* BENCH_UTILS_H */
