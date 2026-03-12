/**
 * Multi-Device Example
 *
 * Demonstrates using multiple emulated NVMe KV SSDs with hash-based
 * key sharding.  Each device path creates an independent in-memory
 * emulated SSD inside the Samsung PDK emulator.
 *
 * Usage:
 *   ./multi_device_example /dev/kvemul0 /dev/kvemul1 [/dev/kvemul2 ...]
 */

#include "kv_engine.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_KEYS 12

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <device1> <device2> [device3 ...]\n", argv[0]);
    fprintf(stderr, "Example: %s /dev/kvemul0 /dev/kvemul1 /dev/kvemul2\n",
            argv[0]);
    return 1;
  }

  int num_devices = argc - 1;
  if (num_devices > KV_MAX_DEVICES) {
    fprintf(stderr, "Too many devices (max %d)\n", KV_MAX_DEVICES);
    return 1;
  }

  /* Configure engine with multiple devices */
  kv_engine_config_t config = {0};
  config.emul_config_file = "../../lib/KVSSD/PDK/core/kvssd_emul.conf";
  config.memory_pool_size = 16 * 1024 * 1024;
  config.queue_depth = 64;
  config.num_worker_threads = 0;
  config.enable_stats = 1;

  config.num_devices = num_devices;
  for (int i = 0; i < num_devices; i++) {
    config.device_paths[i] = argv[i + 1];
  }

  /* Initialize engine */
  kv_engine_t *engine;
  kv_result_t res = kv_engine_init(&engine, &config);
  if (res != KV_SUCCESS) {
    fprintf(stderr, "Failed to initialize engine: %d\n", res);
    return 1;
  }

  int failure_count = 0;
  printf("\n--- Multi-SSD smoke test (%d keys across %d devices) ---\n\n",
         NUM_KEYS, num_devices);

  /* Store deterministic key/value pairs */
  for (int i = 0; i < NUM_KEYS; i++) {
    char key[32];
    char value[64];
    snprintf(key, sizeof(key), "user:%06d", i);
    snprintf(value, sizeof(value), "data-for-user-%d", i);

    res = kv_engine_store(engine, key, strlen(key), value, strlen(value) + 1,
                          false);
    if (res != KV_SUCCESS) {
      fprintf(stderr, "  STORE FAILED for key '%s': %d\n", key, res);
      failure_count++;
    } else {
      printf("  Stored key '%s'\n", key);
    }
  }

  printf("\n--- Retrieving and validating all keys ---\n\n");

  /* Retrieve and validate values */
  int success_count = 0;
  for (int i = 0; i < NUM_KEYS; i++) {
    char key[32];
    char expected_value[64];
    snprintf(key, sizeof(key), "user:%06d", i);
    snprintf(expected_value, sizeof(expected_value), "data-for-user-%d", i);

    void *value = NULL;
    size_t value_len = 0;
    res =
        kv_engine_retrieve(engine, key, strlen(key), &value, &value_len, false);
    if (res != KV_SUCCESS) {
      fprintf(stderr, "  RETRIEVE FAILED for key '%s': %d\n", key, res);
      failure_count++;
    } else {
      if (strcmp((char *)value, expected_value) != 0) {
        fprintf(stderr, "  VALUE MISMATCH for key '%s': expected '%s', got '%s'\n",
                key, expected_value, (char *)value);
        failure_count++;
      } else {
        printf("  Retrieved key '%s' -> '%s'\n", key, (char *)value);
      }
      kv_engine_free_buffer(engine, value);
      success_count++;
    }
  }

  printf("\n--- Verifying key existence ---\n\n");
  for (int i = 0; i < NUM_KEYS; i++) {
    char key[32];
    int exists = 0;
    snprintf(key, sizeof(key), "user:%06d", i);
    res = kv_engine_exists(engine, key, strlen(key), &exists);
    if (res != KV_SUCCESS || !exists) {
      fprintf(stderr, "  EXISTS FAILED for key '%s': res=%d exists=%d\n", key,
              res, exists);
      failure_count++;
    }
  }

  printf("\n--- Deleting a subset and re-checking existence ---\n\n");
  for (int i = 0; i < NUM_KEYS; i += 3) {
    char key[32];
    int exists = 0;
    snprintf(key, sizeof(key), "user:%06d", i);

    res = kv_engine_delete(engine, key, strlen(key));
    if (res != KV_SUCCESS) {
      fprintf(stderr, "  DELETE FAILED for key '%s': %d\n", key, res);
      failure_count++;
      continue;
    }

    res = kv_engine_exists(engine, key, strlen(key), &exists);
    if (res != KV_SUCCESS || exists) {
      fprintf(stderr, "  POST-DELETE EXISTS FAILED for key '%s': res=%d exists=%d\n",
              key, res, exists);
      failure_count++;
    }
  }

  printf("\n--- Results ---\n");
  printf("  %d / %d keys retrieved successfully\n", success_count, NUM_KEYS);
  printf("  Failures observed: %d\n", failure_count);

  /* Print stats */
  kv_engine_stats_t stats;
  kv_engine_get_stats(engine, &stats);
  printf("  Total ops: %" PRIu64 "  (writes: %" PRIu64 ", reads: %" PRIu64
         ")\n",
         stats.total_ops, stats.write_ops, stats.read_ops);

  kv_engine_cleanup(engine);
  if (failure_count > 0) {
    fprintf(stderr, "\nMulti-SSD smoke test FAILED (%d failures).\n",
            failure_count);
    return 1;
  }

  printf("\nMulti-SSD smoke test PASSED.\n");
  return 0;
}
