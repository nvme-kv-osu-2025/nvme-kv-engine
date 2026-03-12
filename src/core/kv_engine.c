/**
 * Core KV Engine Implementation
 */

#include "kv_engine.h"
#include "../utils/dma_alloc.h"
#include "kv_engine_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================
 */

static kv_result_t map_kvs_result(kvs_result kvs_res) {
  switch (kvs_res) {
  case KVS_SUCCESS:
    return KV_SUCCESS;
  case KVS_ERR_PARAM_INVALID:
    return KV_ERR_INVALID_PARAM;
  case KVS_ERR_SYS_IO:
    return KV_ERR_IO;
  case KVS_ERR_KEY_NOT_EXIST:
    return KV_ERR_KEY_NOT_FOUND;
  case KVS_ERR_VALUE_UPDATE_NOT_ALLOWED:
    return KV_ERR_KEY_ALREADY_EXISTS;
  default:
    return KV_ERR_IO;
  }
}

/* ============================================================================
 * Lifecycle Management
 * ============================================================================
 */

kv_result_t kv_engine_init(kv_engine_t **engine,
                           const kv_engine_config_t *config) {
  if (!engine || !config) {
    return KV_ERR_INVALID_PARAM;
  }

  /* Allocate engine structure */
  kv_engine_t *eng = (kv_engine_t *)malloc(sizeof(kv_engine_t));
  if (!eng) {
    return KV_ERR_NO_MEMORY;
  }
  memset(eng, 0, sizeof(kv_engine_t));

  /* Copy configuration */
  eng->config = *config;
  if (config->device_path) {
    eng->config.device_path = strdup(config->device_path);
  }
  if (config->emul_config_file) {
    eng->config.emul_config_file = strdup(config->emul_config_file);
  }

  /* Resolve device paths (single or multi-device) */
  const char *effective_paths[KV_MAX_DEVICES];
  uint32_t effective_count = 0;
  kv_result_t res =
      kv_engine_resolve_device_paths(config, effective_paths, &effective_count);
  if (res != KV_SUCCESS) {
    free(eng);
    return res;
  }

  /* Open all devices */
  for (uint32_t i = 0; i < effective_count; i++) {
    res = kv_engine_open_device(&eng->devices[i], effective_paths[i], i);
    if (res != KV_SUCCESS) {
      for (uint32_t j = 0; j < i; j++) {
        kv_engine_close_device(&eng->devices[j]);
      }
      free(eng);
      return res;
    }
    eng->num_devices++;
  }

  /* Initialize memory pool */
  size_t pool_size = config->memory_pool_size > 0
                         ? config->memory_pool_size
                         : (16 * 1024 * 1024); /* 16MB default */
  eng->mem_pool = memory_pool_create(pool_size);
  if (!eng->mem_pool) {
    for (uint32_t i = 0; i < eng->num_devices; i++) {
      kv_engine_close_device(&eng->devices[i]);
    }
    free(eng);
    return KV_ERR_NO_MEMORY;
  }

  /* Initialize thread pool for async ops */
  if (config->num_worker_threads > 0) {
    eng->workers =
        thread_pool_create(config->num_worker_threads, config->queue_depth);
    if (!eng->workers) {
      memory_pool_destroy(eng->mem_pool);
      for (uint32_t i = 0; i < eng->num_devices; i++) {
        kv_engine_close_device(&eng->devices[i]);
      }
      free(eng);
      return KV_ERR_NO_MEMORY;
    }
  }

  /* Initialize statistics */
  pthread_mutex_init(&eng->stats_lock, NULL);
  memset(&eng->stats, 0, sizeof(kv_engine_stats_t));

  /* Initialize DMA buffer pool (optional, 0 disables it) */
  eng->buffer_pool = NULL;
  if (config->dma_pool_count > 0) {
    eng->buffer_pool =
        dma_pool_create(KV_ENGINE_RETRIEVE_SIZE, config->dma_pool_count);
    /* Non-fatal: engine continues without pooling if creation fails */
  }

  /* Initialize hash table */
  if (create_table(&eng->key_table) != 0) {
    if (eng->buffer_pool) {
      dma_pool_destroy(eng->buffer_pool);
    }
    if (eng->workers) {
      thread_pool_destroy(eng->workers);
    }
    memory_pool_destroy(eng->mem_pool);
    for (uint32_t i = 0; i < eng->num_devices; i++) {
      kv_engine_close_device(&eng->devices[i]);
    }
    free((void *)eng->config.device_path);
    free((void *)eng->config.emul_config_file);
    pthread_mutex_destroy(&eng->stats_lock);
    free(eng);
    return KV_ERR_NO_MEMORY;
  }

  eng->initialized = 1;
  *engine = eng;

  return KV_SUCCESS;
}

void kv_engine_cleanup(kv_engine_t *engine) {
  if (!engine) {
    return;
  }

  /* Shutdown thread pool */
  if (engine->workers) {
    thread_pool_destroy(engine->workers);
  }

  /* Cleanup DMA buffer pool */
  if (engine->buffer_pool) {
    dma_pool_destroy(engine->buffer_pool);
  }

  /* Cleanup memory pool */
  if (engine->mem_pool) {
    memory_pool_destroy(engine->mem_pool);
  }

  /* Close all devices */
  for (uint32_t i = 0; i < engine->num_devices; i++) {
    kv_engine_close_device(&engine->devices[i]);
  }

  free_table(&engine->key_table);

  /* Free config strings */
  if (engine->config.device_path) {
    free((void *)engine->config.device_path);
  }
  if (engine->config.emul_config_file) {
    free((void *)engine->config.emul_config_file);
  }

  pthread_mutex_destroy(&engine->stats_lock);
  pthread_mutex_destroy(&engine->hash_lock);
  free(engine);
}

/* ============================================================================
 * Synchronous Operations
 * ============================================================================
 */

kv_result_t kv_engine_store(kv_engine_t *engine, const void *key,
                            size_t key_len, const void *value, size_t value_len,
                            bool overwrite) {

  if (!engine || !engine->initialized || !key || !value) {
    return KV_ERR_INVALID_PARAM;
  }

  if (key_len < 4 || key_len > 255) {
    return KV_ERR_INVALID_PARAM;
  }

  /* Shard key to a device */
  uint32_t dev_idx = kv_engine_shard_for_key(key, key_len, engine->num_devices);
  kvs_key_space_handle keyspace = engine->devices[dev_idx].keyspace;

  /* Prepare Samsung KV structures */
  kvs_key kv_key;
  kv_key.key = (void *)key;
  kv_key.length = key_len;

  /* handle alignment for DMA */
  void *value_ptr = (void *)value;
  void *aligned_buf = NULL;

  if (!IS_DMA_ALIGNED(value)) {
    aligned_buf = dma_alloc(value_len);
    if (!aligned_buf) {
      return KV_ERR_NO_MEMORY;
    }
    memcpy(aligned_buf, value, value_len);
    value_ptr = aligned_buf;
  }

  kvs_value kv_value;
  kv_value.value = value_ptr;
  kv_value.length = value_len;
  kv_value.actual_value_size = value_len;
  kv_value.offset = 0;

  pthread_mutex_lock(&engine->hash_lock);
  if (!key_in_table(&engine->key_table, key, key_len)) {
    add_key(&engine->key_table, key, key_len);
  }
  pthread_mutex_unlock(&engine->hash_lock);

  /* Perform store operation */
  kvs_option_store option;
  option.st_type = overwrite ? KVS_STORE_POST : KVS_STORE_NOOVERWRITE;
  kvs_result kvs_res = kvs_store_kvp(keyspace, &kv_key, &kv_value, &option);

  if (aligned_buf) {
    dma_free(aligned_buf);
  }

  update_stats(engine, 0, 1, 0, kvs_res == KVS_SUCCESS, value_len);
  return map_kvs_result(kvs_res);
}

kv_result_t kv_engine_retrieve(kv_engine_t *engine, const void *key,
                               size_t key_len, void **value, size_t *value_len,
                               bool delete_value) {
  if (!engine || !engine->initialized || !key || !key_len || !value ||
      !value_len) {
    return KV_ERR_INVALID_PARAM;
  }

  if (key_len < 4 || key_len > 255) {
    return KV_ERR_INVALID_PARAM;
  }

  /* Shard key to a device */
  uint32_t dev_idx = kv_engine_shard_for_key(key, key_len, engine->num_devices);
  kvs_key_space_handle keyspace = engine->devices[dev_idx].keyspace;

  /* Prepare key */
  kvs_key kv_key;
  kv_key.key = (void *)key;
  kv_key.length = key_len;

  /* Initial key retrieve buffer */
  bool from_pool = false;
  void *buffer = NULL;

  if (engine->buffer_pool) {
    buffer = dma_pool_acquire(engine->buffer_pool);
    from_pool = (buffer != NULL);
  }
  if (!buffer) {
    buffer = dma_alloc(KV_ENGINE_RETRIEVE_SIZE);
  }

  if (!buffer) {
    return KV_ERR_NO_MEMORY;
  }

  kvs_value kv_value;
  kv_value.value = buffer;
  kv_value.length = KV_ENGINE_RETRIEVE_SIZE;
  kv_value.actual_value_size = 0;
  kv_value.offset = 0;

  kvs_option_retrieve option;
  option.kvs_retrieve_delete = delete_value;
  kvs_result kvs_res = kvs_retrieve_kvp(keyspace, &kv_key, &option, &kv_value);

  if (kvs_res == KVS_ERR_BUFFER_SMALL) {
    if (from_pool) {
      dma_pool_release(engine->buffer_pool, buffer);
      from_pool = false;
    } else {
      dma_free(buffer);
    }
    buffer = dma_alloc(kv_value.actual_value_size);

    if (!buffer) {
      return KV_ERR_NO_MEMORY;
    }

    kv_value.value = buffer;
    kv_value.length = kv_value.actual_value_size;
    kv_value.offset = 0;
    kvs_res = kvs_retrieve_kvp(keyspace, &kv_key, &option, &kv_value);
  }

  if (delete_value && kvs_res == KVS_SUCCESS) {
    pthread_mutex_lock(&engine->hash_lock);
    delete_key(&engine->key_table, key, key_len);
    pthread_mutex_unlock(&engine->hash_lock);
  }

  if (kvs_res != KVS_SUCCESS) {
    if (from_pool) {
      dma_pool_release(engine->buffer_pool, buffer);
    } else {
      dma_free(buffer);
    }
    update_stats(engine, 1, 0, 0, 0, 0);
    return map_kvs_result(kvs_res);
  }

  *value = kv_value.value;
  *value_len = kv_value.length;

  update_stats(engine, 1, 0, 0, 1, kv_value.actual_value_size);
  return KV_SUCCESS;
}

kv_result_t kv_engine_delete(kv_engine_t *engine, const void *key,
                             size_t key_len) {
  if (!engine || !engine->initialized || !key) {
    return KV_ERR_INVALID_PARAM;
  }

  if (key_len < 4 || key_len > 255) {
    return KV_ERR_INVALID_PARAM;
  }

  /* Shard key to a device */
  uint32_t dev_idx = kv_engine_shard_for_key(key, key_len, engine->num_devices);
  kvs_key_space_handle keyspace = engine->devices[dev_idx].keyspace;

  kvs_key kv_key;
  kv_key.key = (void *)key;
  kv_key.length = key_len;

  kvs_option_delete option;
  option.kvs_delete_error = false;
  kvs_result kvs_res = kvs_delete_kvp(keyspace, &kv_key, &option);

  pthread_mutex_lock(&engine->hash_lock);
  delete_key(&engine->key_table, key, key_len);
  pthread_mutex_unlock(&engine->hash_lock);

  update_stats(engine, 0, 0, 1, kvs_res == KVS_SUCCESS, 0);
  return map_kvs_result(kvs_res);
}

// TODO: discuss if checking both hash table and device is desired behavior
kv_result_t kv_engine_exists(kv_engine_t *engine, const void *key,
                             size_t key_len, int *exists) {
  if (!engine || !engine->initialized || !key || !exists) {
    return KV_ERR_INVALID_PARAM;
  }

  if (key_len < 4 || key_len > 255) {
    return KV_ERR_INVALID_PARAM;
  }

  /* Shard key to a device */
  uint32_t dev_idx = kv_engine_shard_for_key(key, key_len, engine->num_devices);
  kvs_key_space_handle keyspace = engine->devices[dev_idx].keyspace;

  pthread_mutex_lock(&engine->hash_lock);
  uint8_t hash_value_check = key_in_table(&engine->key_table, key, key_len);
  pthread_mutex_unlock(&engine->hash_lock);

  kvs_key kv_key;
  kv_key.key = (void *)key;
  kv_key.length = key_len;

  uint8_t result_buffer;
  kvs_exist_list exist_list;
  exist_list.num_keys = 1;
  exist_list.keys = &kv_key;
  exist_list.length = 1;
  exist_list.result_buffer = &result_buffer;

  kvs_result kvs_res = kvs_exist_kv_pairs(keyspace, 1, &kv_key, &exist_list);

  if (kvs_res != KVS_SUCCESS) {
    return map_kvs_result(kvs_res);
  }

  *exists = (result_buffer != 0) && hash_value_check;
  return KV_SUCCESS;
}

/* ============================================================================
 * Statistics
 * ============================================================================
 */

void update_stats(kv_engine_t *engine, int is_read, int is_write, int is_delete,
                  int success, size_t bytes) {
  pthread_mutex_lock(&engine->stats_lock);

  engine->stats.total_ops++;

  if (is_read)
    engine->stats.read_ops++;
  if (is_write)
    engine->stats.write_ops++;
  if (is_delete)
    engine->stats.delete_ops++;

  if (!success) {
    engine->stats.failed_ops++;
  }

  if (is_read && success) {
    engine->stats.bytes_read += bytes;
  } else if (is_write && success) {
    engine->stats.bytes_written += bytes;
  }

  pthread_mutex_unlock(&engine->stats_lock);
}

kv_result_t kv_engine_get_stats(kv_engine_t *engine, kv_engine_stats_t *stats) {
  if (!engine || !stats) {
    return KV_ERR_INVALID_PARAM;
  }

  pthread_mutex_lock(&engine->stats_lock);
  *stats = engine->stats;
  pthread_mutex_unlock(&engine->stats_lock);

  return KV_SUCCESS;
}

void kv_engine_reset_stats(kv_engine_t *engine) {
  if (!engine) {
    return;
  }

  pthread_mutex_lock(&engine->stats_lock);
  memset(&engine->stats, 0, sizeof(kv_engine_stats_t));
  pthread_mutex_unlock(&engine->stats_lock);
}

void *kv_engine_alloc_buffer(kv_engine_t *engine, size_t size) {
  if (engine->buffer_pool && size <= engine->buffer_pool->buffer_size) {
    void *buf = dma_pool_acquire(engine->buffer_pool);
    if (buf) {
      return buf;
    }
  }
  return dma_alloc(size);
}

void kv_engine_free_buffer(kv_engine_t *engine, void *buffer) {
  if (engine->buffer_pool && dma_pool_owns(engine->buffer_pool, buffer)) {
    dma_pool_release(engine->buffer_pool, buffer);
    return;
  }
  dma_free(buffer);
}
