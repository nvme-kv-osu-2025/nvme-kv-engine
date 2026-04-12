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
#include <stdatomic.h>

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
 * Work item for the thread pool queue
 */
typedef struct work_item {
  void *(*func)(void *);
  void *arg;
  void (*cleanup)(void *);
  struct work_item *next;
} work_item_t;

/**
 * Thread pool for async operations
 */
typedef struct {
  pthread_t *threads;
  uint32_t num_threads;
  int shutdown;

  /* Bounded work queue (singly-linked list) */
  work_item_t *queue_head;
  work_item_t *queue_tail;
  uint32_t queue_size;
  uint32_t queue_capacity;

  /* Synchronization */
  pthread_mutex_t queue_lock;
  pthread_cond_t queue_not_empty;
  pthread_cond_t queue_not_full;
} thread_pool_t;

/**
 * Operation type for async dispatch
 */
typedef enum {
  ASYNC_OP_STORE,
  ASYNC_OP_RETRIEVE,
  ASYNC_OP_DELETE
} async_op_type_t;

/**
 * Async operation context
 */
typedef struct {
  kv_engine_t *engine;
  kv_completion_cb callback;
  kv_retrieve_cb retrieve_callback;
  void *user_data;
  void *key_buffer;
  size_t key_len;
  void *value_buffer;
  size_t value_len;
  bool overwrite;
  async_op_type_t op_type;
} async_context_t;

/**
 * Per-device context (device handle + keyspace handle pair)
 */
typedef struct {
  kvs_device_handle device;
  kvs_key_space_handle keyspace;
  char *device_path; /* owned copy for cleanup */

  /* Health tracking (updated atomically on every operation)
   *
   * Using _Atomic instead of a mutex because these fields will be
   * individually updated on every op and don't require multi-field
   * consistency. */
  _Atomic bool healthy;
  _Atomic uint64_t consecutive_errors; /* resets to 0 on success */
  _Atomic uint64_t total_errors;
  _Atomic uint64_t total_ops;
  uint32_t
      max_consecutive_errors; /* threshold for marking unhealthy; default 10 */
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

  /* Hash table lock (uthash is not thread-safe) */
  pthread_mutex_t hash_lock;

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
thread_pool_t *thread_pool_create(uint32_t num_threads, uint32_t queue_depth);
int thread_pool_submit(thread_pool_t *pool, void *(*func)(void *), void *arg,
                       void (*cleanup)(void *));
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
