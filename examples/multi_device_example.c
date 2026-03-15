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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_KEYS 120

static uint32_t shard_for_key(const void *key, size_t key_len,
                              uint32_t num_devices) {
  const uint8_t *data = (const uint8_t *)key;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < key_len; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash % num_devices;
}

static int expected_version_for_key(int key_index) {
  return (key_index % 10 == 1) ? 2 : 1;
}

static int run_distribution_check(int num_devices) {
  int per_device_counts[KV_MAX_DEVICES] = {0};
  for (int i = 0; i < NUM_KEYS; i++) {
    char key[32];
    uint32_t dev_idx;
    snprintf(key, sizeof(key), "user:%06d", i);
    dev_idx = shard_for_key(key, strlen(key), (uint32_t)num_devices);
    per_device_counts[dev_idx]++;
  }

  printf("\n--- Checking sharding distribution ---\n");
  for (int d = 0; d < num_devices; d++) {
    printf("  device[%d]: %d keys\n", d, per_device_counts[d]);
    if (per_device_counts[d] == 0) {
      fprintf(stderr, "  Distribution check FAILED: device[%d] has no keys\n",
              d);
      return 1;
    }
  }
  return 0;
}

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

  if (run_distribution_check(num_devices) != 0) {
    kv_engine_cleanup(engine);
    return 1;
  }

  int failure_count = 0;
  printf("\n--- Multi-SSD smoke test (%d keys across %d devices) ---\n\n",
         NUM_KEYS, num_devices);

  /* Phase 1: store deterministic key/value pairs */
  for (int i = 0; i < NUM_KEYS; i++) {
    char key[32];
    char value[64];
    snprintf(key, sizeof(key), "user:%06d", i);
    snprintf(value, sizeof(value), "data-for-user-%d-v1", i);

    res = kv_engine_store(engine, key, strlen(key), value, strlen(value) + 1,
                          false);
    if (res != KV_SUCCESS) {
      fprintf(stderr, "  STORE FAILED for key '%s': %d\n", key, res);
      failure_count++;
    } else {
      printf("  Stored key '%s'\n", key);
    }
  }

  /* Phase 2: duplicate inserts with overwrite=false should fail */
  printf("\n--- Checking duplicate-key rejection (no overwrite) ---\n\n");
  for (int i = 0; i < NUM_KEYS; i += 10) {
    char key[32];
    char dup_value[64];
    snprintf(key, sizeof(key), "user:%06d", i);
    snprintf(dup_value, sizeof(dup_value), "duplicate-write-%d", i);

    res = kv_engine_store(engine, key, strlen(key), dup_value,
                          strlen(dup_value) + 1, false);
    if (res != KV_ERR_KEY_ALREADY_EXISTS) {
      fprintf(stderr,
              "  DUPLICATE REJECTION FAILED for key '%s': expected %d got %d\n",
              key, KV_ERR_KEY_ALREADY_EXISTS, res);
      failure_count++;
    }
  }

  /* Phase 3: overwrite a subset with overwrite=true */
  printf("\n--- Overwriting subset of keys ---\n\n");
  for (int i = 1; i < NUM_KEYS; i += 10) {
    char key[32];
    char overwrite_value[64];
    snprintf(key, sizeof(key), "user:%06d", i);
    snprintf(overwrite_value, sizeof(overwrite_value), "data-for-user-%d-v2",
             i);

    res = kv_engine_store(engine, key, strlen(key), overwrite_value,
                          strlen(overwrite_value) + 1, true);
    if (res != KV_SUCCESS) {
      fprintf(stderr, "  OVERWRITE FAILED for key '%s': %d\n", key, res);
      failure_count++;
    }
  }

  printf("\n--- Retrieving and validating all keys ---\n\n");

  /* Phase 4: retrieve and validate values */
  int success_count = 0;
  for (int i = 0; i < NUM_KEYS; i++) {
    char key[32];
    char expected_value[64];
    snprintf(key, sizeof(key), "user:%06d", i);
    snprintf(expected_value, sizeof(expected_value), "data-for-user-%d-v%d", i,
             expected_version_for_key(i));

    void *value = NULL;
    size_t value_len = 0;
    res =
        kv_engine_retrieve(engine, key, strlen(key), &value, &value_len, false);
    if (res != KV_SUCCESS) {
      fprintf(stderr, "  RETRIEVE FAILED for key '%s': %d\n", key, res);
      failure_count++;
    } else {
      if (strcmp((char *)value, expected_value) != 0) {
        fprintf(stderr,
                "  VALUE MISMATCH for key '%s': expected '%s', got '%s'\n", key,
                expected_value, (char *)value);
        failure_count++;
      } else {
        printf("  Retrieved key '%s' -> '%s'\n", key, (char *)value);
        success_count++;
      }
      kv_engine_free_buffer(engine, value);
    }
  }

  printf("\n--- Verifying key existence ---\n\n");
  /* Phase 5: exists() for all keys */
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
  int deleted_count = 0;
  /* Phase 6: delete and confirm absence */
  for (int i = 0; i < NUM_KEYS; i += 3) {
    char key[32];
    void *value = NULL;
    size_t value_len = 0;
    int exists = 0;
    snprintf(key, sizeof(key), "user:%06d", i);

    res = kv_engine_delete(engine, key, strlen(key));
    if (res != KV_SUCCESS) {
      fprintf(stderr, "  DELETE FAILED for key '%s': %d\n", key, res);
      failure_count++;
      continue;
    }
    deleted_count++;

    res = kv_engine_exists(engine, key, strlen(key), &exists);
    if (res != KV_SUCCESS || exists) {
      fprintf(stderr,
              "  POST-DELETE EXISTS FAILED for key '%s': res=%d exists=%d\n",
              key, res, exists);
      failure_count++;
    }

    res =
        kv_engine_retrieve(engine, key, strlen(key), &value, &value_len, false);
    if (res != KV_ERR_KEY_NOT_FOUND) {
      fprintf(
          stderr,
          "  POST-DELETE RETRIEVE FAILED for key '%s': expected %d got %d\n",
          key, KV_ERR_KEY_NOT_FOUND, res);
      failure_count++;
      if (res == KV_SUCCESS && value) {
        kv_engine_free_buffer(engine, value);
      }
    }
  }

  printf("\n--- Results ---\n");
  printf("  %d / %d keys retrieved successfully\n", success_count, NUM_KEYS);
  printf("  Failures observed: %d\n", failure_count);

  /* Print stats */
  kv_engine_stats_t stats;
  kv_engine_get_stats(engine, &stats);
  printf("  Total ops: %" PRIu64 "  (writes: %" PRIu64 ", reads: %" PRIu64
         ", deletes: %" PRIu64 ", failed: %" PRIu64 ")\n",
         stats.total_ops, stats.write_ops, stats.read_ops, stats.delete_ops,
         stats.failed_ops);

  {
    uint64_t duplicate_attempts = (NUM_KEYS + 9) / 10;
    uint64_t overwrite_attempts = (NUM_KEYS + 8) / 10;
    uint64_t expected_write_ops =
        NUM_KEYS + duplicate_attempts + overwrite_attempts;
    uint64_t expected_read_ops = NUM_KEYS + (uint64_t)deleted_count;
    uint64_t expected_delete_ops = (uint64_t)deleted_count;
    uint64_t expected_total_ops =
        expected_write_ops + expected_read_ops + expected_delete_ops;
    uint64_t minimum_expected_failures =
        duplicate_attempts + (uint64_t)deleted_count;

    if (stats.write_ops != expected_write_ops ||
        stats.read_ops != expected_read_ops ||
        stats.delete_ops != expected_delete_ops ||
        stats.total_ops != expected_total_ops ||
        stats.failed_ops < minimum_expected_failures) {
      fprintf(stderr,
              "  STATS CHECK FAILED: expected total=%" PRIu64 " writes=%" PRIu64
              " reads=%" PRIu64 " deletes=%" PRIu64 " failed>=%" PRIu64
              ", got total=%" PRIu64 " writes=%" PRIu64 " reads=%" PRIu64
              " deletes=%" PRIu64 " failed=%" PRIu64 "\n",
              expected_total_ops, expected_write_ops, expected_read_ops,
              expected_delete_ops, minimum_expected_failures, stats.total_ops,
              stats.write_ops, stats.read_ops, stats.delete_ops,
              stats.failed_ops);
      failure_count++;
    }
  }

  kv_engine_cleanup(engine);
  if (failure_count > 0) {
    fprintf(stderr, "\nMulti-SSD test FAILED (%d failures).\n", failure_count);
    return 1;
  }

  printf("\nMulti-SSD test PASSED.\n");
  return 0;
}
