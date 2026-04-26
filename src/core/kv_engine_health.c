/**
 * Device health monitoring and background probe
 *
 * Implements kv_engine_get_device_health(), kv_engine_healthy_device_count(),
 * and the background probe thread that periodically retries unhealthy devices
 * and re-marks them healthy after a configurable number of consecutive
 * successful probes.
 */

#include "kv_engine_internal.h"
#include <kvs_api.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * Background Probe Thread
 * ============================================================================
 */

static void *health_probe_thread(void *arg) {
  health_probe_t *probe = (health_probe_t *)arg;
  kv_engine_t *engine = probe->engine;

  /* per-device recovery counters (local to this thread, no sharing needed) */
  uint32_t recovery_counts[KV_MAX_DEVICES] = {0};

  while (atomic_load(&probe->running)) {
    /* sleep for probe_interval_sec, but wake immediately if destroy() signals */
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += probe->probe_interval_sec;

    pthread_mutex_lock(&probe->mutex);
    pthread_cond_timedwait(&probe->cond, &probe->mutex, &deadline);
    pthread_mutex_unlock(&probe->mutex);

    if (!atomic_load(&probe->running)) {
      break;
    }

    /* device recovery loop */
    for (uint32_t i = 0; i < engine->num_devices; i++) {
      kv_device_ctx_t *dev = &engine->devices[i];
      if (atomic_load(&dev->healthy)) {
        continue;
      }

      uint32_t utilization;
      kvs_result res = kvs_get_device_utilization(dev->device, &utilization);
      if (res == KVS_SUCCESS) {
        recovery_counts[i]++;
        if (recovery_counts[i] >= probe->recovery_threshold) {
          atomic_store(&dev->healthy, true);
          atomic_store(&dev->consecutive_errors, 0);
          recovery_counts[i] = 0;
        }
      } else {
        recovery_counts[i] = 0;
      }
    }
    
  }

  return NULL;
}

/* ============================================================================
 * Probe Lifecycle
 * ============================================================================
 */

health_probe_t *health_probe_create(kv_engine_t *engine) {
  health_probe_t *probe = calloc(1, sizeof(health_probe_t));
  if (!probe) {
    return NULL;
  }

  probe->engine = engine;
  probe->probe_interval_sec = 5;
  probe->recovery_threshold = 3;
  atomic_store(&probe->running, true);

  pthread_mutex_init(&probe->mutex, NULL);
  pthread_cond_init(&probe->cond, NULL);

  if (pthread_create(&probe->thread, NULL, health_probe_thread, probe) != 0) {
    pthread_mutex_destroy(&probe->mutex);
    pthread_cond_destroy(&probe->cond);
    free(probe);
    return NULL;
  }

  return probe;
}

void health_probe_destroy(health_probe_t *probe) {
  if (!probe) {
    return;
  }

  /* Signal the thread to wake and exit */
  atomic_store(&probe->running, false);
  pthread_mutex_lock(&probe->mutex);
  pthread_cond_signal(&probe->cond);
  pthread_mutex_unlock(&probe->mutex);

  pthread_join(probe->thread, NULL);
  pthread_mutex_destroy(&probe->mutex);
  pthread_cond_destroy(&probe->cond);
  free(probe);
}

/* ============================================================================
 * Public Health API
 * ============================================================================
 */

kv_result_t kv_engine_get_device_health(kv_engine_t *engine,
                                         uint32_t device_index,
                                         kv_device_health_t *health) {
  if (!engine || !engine->initialized || !health) {
    return KV_ERR_INVALID_PARAM;
  }
  if (device_index >= engine->num_devices) {
    return KV_ERR_INVALID_PARAM;
  }

  kv_device_ctx_t *dev = &engine->devices[device_index];

  memset(health, 0, sizeof(*health));
  health->device_index = device_index;
  health->healthy = atomic_load(&dev->healthy);
  health->consecutive_errors = atomic_load(&dev->consecutive_errors);
  health->total_errors = atomic_load(&dev->total_errors);
  health->total_ops = atomic_load(&dev->total_ops);

  if (dev->device_path) {
    snprintf(health->device_path, sizeof(health->device_path), "%s",
             dev->device_path);
  }

  /* Query live capacity and utilization from the device */
  kvs_get_device_capacity(dev->device, &health->capacity_bytes);
  kvs_get_device_utilization(dev->device, &health->utilization_pct);

  return KV_SUCCESS;
}

uint32_t kv_engine_healthy_device_count(kv_engine_t *engine) {
  if (!engine || !engine->initialized) {
    return 0;
  }

  uint32_t count = 0;
  for (uint32_t i = 0; i < engine->num_devices; i++) {
    if (atomic_load(&engine->devices[i].healthy)) {
      count++;
    }
  }
  return count;
}

kv_result_t kv_engine_add_device(kv_engine_t *engine,
                                  const char *device_path) {
  if (!engine || !engine->initialized || !device_path) {
    return KV_ERR_INVALID_PARAM;
  }
  /* Not yet implemented — Phase 2F */
  return KV_ERR_INVALID_PARAM;
}
