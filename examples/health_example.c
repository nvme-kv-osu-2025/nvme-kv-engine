/**
 * Health Monitoring Example
 *
 * Demonstrates the device health API: querying per-device health,
 * counting healthy devices, and hot-adding a device before first write.
 *
 * Usage:
 *   ./health_example /dev/kvemul0 /dev/kvemul1
 */

#include "kv_engine.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_health(kv_engine_t *engine, uint32_t num_devices) {
  printf("  healthy devices: %u / %u\n",
         kv_engine_healthy_device_count(engine), num_devices);

  for (uint32_t i = 0; i < num_devices; i++) {
    kv_device_health_t h;
    kv_result_t res = kv_engine_get_device_health(engine, i, &h);
    if (res != KV_SUCCESS) {
      fprintf(stderr, "  [device %u] get_device_health failed: %d\n", i, res);
      continue;
    }
    printf("  [device %u] path=%-16s healthy=%-5s ops=%-6" PRIu64
           " errors=%-4" PRIu64 " capacity=%" PRIu64 "  utilization=%u\n",
           i, h.device_path, h.healthy ? "true" : "false", h.total_ops,
           h.total_errors, h.capacity_bytes, h.utilization_pct);
  }
}

int main(int argc, char **argv) {
  if (argc < 3) {
    fprintf(stderr, "Usage: %s <device0> <device1> [device2 ...]\n", argv[0]);
    fprintf(stderr, "Example: %s /dev/kvemul0 /dev/kvemul1\n", argv[0]);
    return 1;
  }

  uint32_t num_devices = (uint32_t)(argc - 1);
  if (num_devices > KV_MAX_DEVICES) {
    fprintf(stderr, "Too many devices (max %d)\n", KV_MAX_DEVICES);
    return 1;
  }

  /* -------------------------------------------------------------------------
   * Section 1: basic health query on a freshly initialized engine
   * ---------------------------------------------------------------------- */
  printf("--- Section 1: initial health state ---\n");

  kv_engine_config_t config = {
      .emul_config_file = "../../lib/KVSSD/PDK/core/kvssd_emul.conf",
      .memory_pool_size = 16 * 1024 * 1024,
      .queue_depth = 64,
      .num_worker_threads = 0,
      .enable_stats = 1,
      .num_devices = num_devices,
  };
  for (uint32_t i = 0; i < num_devices; i++) {
    config.device_paths[i] = argv[i + 1];
  }

  kv_engine_t *engine;
  kv_result_t res = kv_engine_init(&engine, &config);
  if (res != KV_SUCCESS) {
    fprintf(stderr, "Engine init failed: %d\n", res);
    return 1;
  }

  print_health(engine, num_devices);

  /* -------------------------------------------------------------------------
   * Section 2: health after 5 stores (ops should increase, errors stay 0)
   * ---------------------------------------------------------------------- */
  printf("\n--- Section 2: health after 5 stores ---\n");

  for (int i = 0; i < 5; i++) {
    char key[32], value[64];
    snprintf(key, sizeof(key), "health:key:%04d", i);
    snprintf(value, sizeof(value), "value-%d", i);
    res = kv_engine_store(engine, key, strlen(key), value, strlen(value), false);
    if (res != KV_SUCCESS) {
      fprintf(stderr, "Store %d failed: %d\n", i, res);
    }
  }

  print_health(engine, num_devices);

  /* -------------------------------------------------------------------------
   * Section 3: retrieving non-existent keys should NOT degrade health
   * (KEY_NOT_EXIST is an application error, not a device error)
   * ---------------------------------------------------------------------- */
  printf("\n--- Section 3: health after 15 misses ---\n");

  for (int i = 0; i < 15; i++) {
    char key[32];
    void *val;
    size_t len;
    snprintf(key, sizeof(key), "missing:key:%04d", i);
    kv_engine_retrieve(engine, key, strlen(key), &val, &len, false);
  }

  print_health(engine, num_devices);

  for (uint32_t i = 0; i < num_devices; i++) {
    kv_device_health_t h;
    kv_engine_get_device_health(engine, i, &h);
    if (!h.healthy) {
      fprintf(stderr, "FAIL: device %u marked unhealthy after cache misses\n", i);
      kv_engine_cleanup(engine);
      return 1;
    }
  }
  printf("  PASS: all devices still healthy after misses\n");

  /* -------------------------------------------------------------------------
   * Section 4: hot-add rejected after writes
   * ---------------------------------------------------------------------- */
  printf("\n--- Section 4: hot-add rejected after writes ---\n");

  res = kv_engine_add_device(engine, argv[1]);
  if (res == KV_ERR_INVALID_PARAM) {
    printf("  PASS: add_device correctly rejected after writes (err=%d)\n", res);
  } else {
    fprintf(stderr, "  FAIL: expected KV_ERR_INVALID_PARAM, got %d\n", res);
    kv_engine_cleanup(engine);
    return 1;
  }

  /* -------------------------------------------------------------------------
   * Section 5: probe thread shutdown - cleanup must not hang
   * ---------------------------------------------------------------------- */
  printf("\n--- Section 5: engine cleanup (probe thread shutdown) ---\n");
  kv_engine_cleanup(engine);
  printf("  PASS: cleanup returned cleanly\n");

  /* -------------------------------------------------------------------------
   * Section 6: hot-add before first write succeeds
   * ---------------------------------------------------------------------- */
  printf("\n--- Section 6: hot-add before first write ---\n");

  kv_engine_config_t single_config = {
      .device_path = argv[1],
      .emul_config_file = "../../lib/KVSSD/PDK/core/kvssd_emul.conf",
      .memory_pool_size = 16 * 1024 * 1024,
      .queue_depth = 64,
      .num_worker_threads = 0,
      .enable_stats = 1,
  };

  kv_engine_t *engine2;
  res = kv_engine_init(&engine2, &single_config);
  if (res != KV_SUCCESS) {
    fprintf(stderr, "Single-device init failed: %d\n", res);
    return 1;
  }

  printf("  started with 1 device, healthy_count=%u\n",
         kv_engine_healthy_device_count(engine2));

  res = kv_engine_add_device(engine2, argv[2]);
  if (res == KV_SUCCESS) {
    printf("  PASS: add_device succeeded before any writes, healthy_count=%u\n",
           kv_engine_healthy_device_count(engine2));
  } else {
    fprintf(stderr, "  FAIL: add_device returned %d\n", res);
    kv_engine_cleanup(engine2);
    return 1;
  }

  kv_engine_cleanup(engine2);
  printf("  cleanup ok\n");

  printf("\n=== All health checks passed ===\n");
  return 0;
}
