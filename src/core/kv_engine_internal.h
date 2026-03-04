/**
 * Internal data structures and definitions
 * Not exposed in public API
 */

#ifndef KV_ENGINE_INTERNAL_H
#define KV_ENGINE_INTERNAL_H

#include "../utils/dma_pool.h"
#include "../utils/hashTable.h"
#include "kv_engine.h"
#include <kvs_api.h>
#include <pthread.h>

#define KV_ENGINE_RETRIEVE_SIZE 2 * 1024 * 1024 /* 2MB */

/* ============================================================================
 * Internal Structures
 * ============================================================================
 */

/**
 * Memory pool for efficient buffer allocation
 */
typedef struct {
  void *base;
  size_t size;
  size_t used;
  pthread_mutex_t lock;
} memory_pool_t;

/**
 * Thread pool for async operations
 */
typedef struct {
  pthread_t *threads;
  uint32_t num_threads;
  int shutdown;
  // TODO: Add work queue
} thread_pool_t;

/**
 * Async operation context
 */
typedef struct {
  kv_engine_t *engine;
  kv_completion_cb callback;
  void *user_data;
  void *key_buffer;
  size_t key_len;
  void *value_buffer;
  size_t value_len;
} async_context_t;

/**
 * Per-device context (device handle + keyspace handle pair)
 */
typedef struct {
  kvs_device_handle device;
  kvs_key_space_handle keyspace;
  char *device_path; /* owned copy for cleanup */
} kv_device_ctx_t;

/**
 * Main engine structure (opaque in public API)
 */
struct kv_engine {
  /* Device array — one entry per SSD */
  kv_device_ctx_t devices[KV_MAX_DEVICES];
  uint32_t num_devices;

  /* Configuration */
  kv_engine_config_t config;

  /* Memory management */
  memory_pool_t *mem_pool;
  dma_pool_t *buffer_pool;

  /* Async I/O */
  thread_pool_t *workers;

  /* Statistics */
  kv_engine_stats_t stats;
  pthread_mutex_t stats_lock;

  /* Hash table */
  hash_table_t key_table;

  /* State */
  int initialized;
};

/* ============================================================================
 * Internal Functions
 * ============================================================================
 */

/* Memory pool operations */
memory_pool_t *memory_pool_create(size_t size);
void *memory_pool_alloc(memory_pool_t *pool, size_t size);
void memory_pool_free(memory_pool_t *pool, void *ptr);
void memory_pool_destroy(memory_pool_t *pool);

/* Thread pool operations */
thread_pool_t *thread_pool_create(uint32_t num_threads);
int thread_pool_submit(thread_pool_t *pool, void *(*func)(void *), void *arg);
void thread_pool_destroy(thread_pool_t *pool);

/* Statistics helpers */
void update_stats(kv_engine_t *engine, int is_read, int is_write, int is_delete,
                  int success, size_t bytes);

/* Multi-device helpers */
kv_result_t kv_engine_resolve_device_paths(const kv_engine_config_t *config,
                                           const char **effective_paths,
                                           uint32_t *effective_count);
uint32_t kv_engine_shard_for_key(const void *key, size_t key_len,
                                 uint32_t num_devices);
kv_result_t kv_engine_open_device(kv_device_ctx_t *ctx, const char *path,
                                  uint32_t dev_index);
void kv_engine_close_device(kv_device_ctx_t *ctx);

#endif /* KV_ENGINE_INTERNAL_H */
