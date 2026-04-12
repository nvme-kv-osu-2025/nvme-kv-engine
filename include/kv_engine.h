/**
 * NVMe Key-Value Storage Engine
 *
 * High-performance key-value storage engine leveraging NVMe KV Command Set
 * to eliminate block-storage translation overhead.
 *
 * @file kv_engine.h
 * @brief Main API header for the NVMe-KV storage engine
 */

#ifndef KV_ENGINE_H
#define KV_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ============================================================================
 * Constants
 * ============================================================================
 */

#define KV_MAX_DEVICES 8 /**< Maximum number of devices per engine instance */

/* ============================================================================
 * Type Definitions
 * ============================================================================
 */

/**
 * Opaque handle to the KV engine instance
 */
typedef struct kv_engine kv_engine_t;

/**
 * Result codes for KV operations
 */
typedef enum {
  KV_SUCCESS = 0,
  KV_ERR_INVALID_PARAM = -1,
  KV_ERR_NO_MEMORY = -2,
  KV_ERR_DEVICE_OPEN = -3,
  KV_ERR_KEY_NOT_FOUND = -4,
  KV_ERR_KEY_EXISTS = -5,
  KV_ERR_VALUE_TOO_LARGE = -6,
  KV_ERR_TIMEOUT = -7,
  KV_ERR_IO = -8,
  KV_ERR_NOT_INITIALIZED = -9,
  KV_ERR_KEY_ALREADY_EXISTS = -10,
  KV_ERR_DEVICE_FULL = -11,
  KV_ERR_DEVICE_NOT_FOUND = -12,
  KV_ERR_KEY_LENGTH = -13,
  KV_ERR_VALUE_LENGTH = -14,
  KV_ERR_DEVICE_DEGRADED = -15,
  KV_ERR_ALL_DEVICES_FAILED = -16
} kv_result_t;

/**
 * Completion callback for async operations (store, delete)
 *
 * @param result Operation result code
 * @param user_data User-provided context pointer
 */
typedef void (*kv_completion_cb)(kv_result_t result, void *user_data);

/**
 * Completion callback for async retrieve operations
 *
 * @param result Operation result code
 * @param value Retrieved value buffer (caller must free with
 * kv_engine_free_buffer), NULL on failure
 * @param value_len Length of retrieved value, 0 on failure
 * @param user_data User-provided context pointer
 */
typedef void (*kv_retrieve_cb)(kv_result_t result, void *value,
                               size_t value_len, void *user_data);

/**
 * Configuration options for engine initialization
 */
typedef struct {
  const char *device_path; /**< Path to NVMe device (single-device mode) */
  const char
      *emul_config_file;   /**< Path to emulator config (if using emulator) */
  size_t memory_pool_size; /**< Size of memory pool in bytes */
  uint32_t queue_depth;    /**< I/O queue depth */
  uint32_t num_worker_threads; /**< Number of async worker threads */
  uint32_t enable_stats;       /**< Enable performance statistics (0 or 1) */

  /* Multi-device support: set num_devices > 0 to use multiple SSDs.
   * When num_devices == 0, the engine falls back to device_path above. */
  const char *device_paths[KV_MAX_DEVICES]; /**< Array of device paths */
  uint32_t num_devices; /**< Number of devices (0 = single-device mode) */

  /* DMA buffer pool: set dma_pool_count > 0 to enable pooling.
   * Each buffer is KV_ENGINE_RETRIEVE_SIZE (2MB). 0 = disabled. */
  uint32_t dma_pool_count;
} kv_engine_config_t;

/**
 * Performance statistics
 */
typedef struct {
  uint64_t total_ops;     /**< Total operations performed */
  uint64_t read_ops;      /**< Read operations */
  uint64_t write_ops;     /**< Write operations */
  uint64_t delete_ops;    /**< Delete operations */
  uint64_t failed_ops;    /**< Failed operations */
  double avg_latency_us;  /**< Average latency in microseconds */
  uint64_t bytes_written; /**< Total bytes written */
  uint64_t bytes_read;    /**< Total bytes read */
} kv_engine_stats_t;

/**
 * Per-device health snapshot (for multi-device mode)
 *
 * Populated by kv_engine_get_device_health(). Reflects the state of one
 * physical device at the moment of the call.
 */
typedef struct {
  uint32_t device_index;        /**< Index into the engine's device array */
  bool healthy;                 /**< false once consecutive_errors exceeds threshold, or on unplug */
  uint64_t capacity_bytes;      /**< Total raw capacity reported by device */
  uint32_t utilization_pct;     /**< 0-10000 (divide by 100 for percent; e.g. 4250 = 42.50%) */
  uint64_t consecutive_errors;  /**< Device-level errors since last success; resets to 0 on success */
  uint64_t total_errors;        /**< Cumulative device-level error count since engine init */
  uint64_t total_ops;           /**< Cumulative operations attempted on this device */
  char device_path[256];        /**< Null-terminated path (e.g. "/dev/kvemul0") */
} kv_device_health_t;


/* ============================================================================
 * Lifecycle Management
 * ============================================================================
 */

/**
 * Initialize a new KV engine instance
 *
 * @param engine Pointer to receive the engine handle
 * @param config Configuration options
 * @return KV_SUCCESS on success, error code otherwise
 */
kv_result_t kv_engine_init(kv_engine_t **engine,
                           const kv_engine_config_t *config);

/**
 * Cleanup and destroy a KV engine instance
 *
 * @param engine Engine handle to destroy
 */
void kv_engine_cleanup(kv_engine_t *engine);

/* ============================================================================
 * Synchronous Operations
 * ============================================================================
 */

/**
 * Store a key-value pair (synchronous)
 *
 * @param engine Engine handle
 * @param key Key buffer
 * @param key_len Key length (4-255 bytes)
 * @param value Value buffer
 * @param value_len Value length (up to 2MB)
 * @param overwrite If true, overwrite existing key; if false, return
 * KV_ERR_KEY_ALREADY_EXISTS
 * @return KV_SUCCESS on success, error code otherwise
 */
kv_result_t kv_engine_store(kv_engine_t *engine, const void *key,
                            size_t key_len, const void *value, size_t value_len,
                            bool overwrite);

/**
 * Retrieve a value by key (synchronous)
 *
 * @param engine Engine handle
 * @param key Key buffer
 * @param key_len Key length
 * @param value Pointer to receive value buffer (caller must free with
 * kv_engine_free_buffer)
 * @param value_len Pointer to receive value length
 * @param delete_value Flag to indicate if the key-value pair should be deleted
 * after retrieval (1 = delete, 0 = keep)
 * @return KV_SUCCESS on success, error code otherwise
 */
kv_result_t kv_engine_retrieve(kv_engine_t *engine, const void *key,
                               size_t key_len, void **value, size_t *value_len,
                               bool delete_value);
/**
 * Delete a key-value pair (synchronous)
 *
 * @param engine Engine handle
 * @param key Key buffer
 * @param key_len Key length
 * @return KV_SUCCESS on success, error code otherwise
 */
kv_result_t kv_engine_delete(kv_engine_t *engine, const void *key,
                             size_t key_len);

/**
 * Check if a key exists (synchronous)
 *
 * @param engine Engine handle
 * @param key Key buffer
 * @param key_len Key length
 * @param exists Pointer to receive existence flag (1=exists, 0=not exists)
 * @return KV_SUCCESS on success, error code otherwise
 */
kv_result_t kv_engine_exists(kv_engine_t *engine, const void *key,
                             size_t key_len, int *exists);

/* ============================================================================
 * Asynchronous Operations
 * ============================================================================
 */

/**
 * Store a key-value pair (asynchronous)
 *
 * @param engine Engine handle
 * @param key Key buffer
 * @param key_len Key length
 * @param value Value buffer
 * @param value_len Value length
 * @param callback Completion callback
 * @param user_data User context for callback
 * @param overwrite If true, overwrite existing key; if false, return
 * KV_ERR_KEY_ALREADY_EXISTS
 * @return KV_SUCCESS if submitted, error code otherwise
 */
kv_result_t kv_engine_store_async(kv_engine_t *engine, const void *key,
                                  size_t key_len, const void *value,
                                  size_t value_len, kv_completion_cb callback,
                                  void *user_data, bool overwrite);

/**
 * Retrieve a value by key (asynchronous)
 *
 * @param engine Engine handle
 * @param key Key buffer
 * @param key_len Key length
 * @param callback Retrieve completion callback (receives value and length)
 * @param user_data User context for callback
 * @return KV_SUCCESS if submitted, error code otherwise
 */
kv_result_t kv_engine_retrieve_async(kv_engine_t *engine, const void *key,
                                     size_t key_len, kv_retrieve_cb callback,
                                     void *user_data);

/**
 * Delete a key-value pair (asynchronous)
 *
 * @param engine Engine handle
 * @param key Key buffer
 * @param key_len Key length
 * @param callback Completion callback
 * @param user_data User context for callback
 * @return KV_SUCCESS if submitted, error code otherwise
 */
kv_result_t kv_engine_delete_async(kv_engine_t *engine, const void *key,
                                   size_t key_len, kv_completion_cb callback,
                                   void *user_data);

/* ============================================================================
 * Statistics and Monitoring
 * ============================================================================
 */

/**
 * Get current performance statistics
 *
 * @param engine Engine handle
 * @param stats Pointer to receive statistics
 * @return KV_SUCCESS on success, error code otherwise
 */
kv_result_t kv_engine_get_stats(kv_engine_t *engine, kv_engine_stats_t *stats);

/**
 * Reset performance statistics
 *
 * @param engine Engine handle
 */
void kv_engine_reset_stats(kv_engine_t *engine);

/* ============================================================================
 * Health Monitoring
 * ============================================================================
 */

/**
 * Get a health snapshot for a single device
 *
 * Reads capacity and utilization from the Samsung KVS API and copies the
 * current atomic health counters into @p health.
 *
 * @param engine       Engine handle
 * @param device_index Index of the device (0 to num_devices-1)
 * @param health       Output struct to populate
 * @return KV_SUCCESS, KV_ERR_INVALID_PARAM if device_index is out of range
 */
kv_result_t kv_engine_get_device_health(kv_engine_t *engine,
                                        uint32_t device_index,
                                        kv_device_health_t *health);

/**
 * Return the number of currently healthy devices
 *
 * Iterates the device array and counts entries where healthy == true.
 * Returns 0 if the engine is NULL or uninitialized.
 *
 * @param engine Engine handle
 * @return Count of healthy devices (0..num_devices)
 */
uint32_t kv_engine_healthy_device_count(kv_engine_t *engine);

/**
 * Add a device to the engine (hot-add)
 *
 * ONLY valid before any keys have been written (write_ops == 0).
 * Changing the device count after writes corrupts hash-mod-N routing:
 * hash(key) % (N+1) produces a different index for ~80% of existing keys,
 * making that data appear missing.
 *
 * NOTE: if we wanted to support hot-add after writes, we would need to use a 
 * different hash sharding scheme (e.g. consistent hashing) that doesn't change
 * existing key mappings when N changes, then migrate existing keys to their
 * new locations.
 *
 * @param engine      Engine handle
 * @param device_path Path to the new NVMe KV device (e.g. "/dev/kvemul4")
 * @return KV_SUCCESS on success
 *         KV_ERR_INVALID_PARAM if called after writes have started,
 *           or if num_devices == KV_MAX_DEVICES
 */
kv_result_t kv_engine_add_device(kv_engine_t *engine,
                                 const char *device_path);

/* ============================================================================
 * Buffer Management
 * ============================================================================
 */

/**
 * Allocate a DMA-aligned buffer for optimal store performance
 *
 * @param engine Engine handle
 * @param size Number of bytes to allocate
 * @return Pointer to aligned buffer, or NULL on failure
 */
void *kv_engine_alloc_buffer(kv_engine_t *engine, size_t size);

/**
 * Free a buffer allocated with kv_engine_alloc_buffer
 *
 * @param engine Engine handle
 * @param buffer Buffer to free (NULL is safe)
 */
void kv_engine_free_buffer(kv_engine_t *engine, void *buffer);

#endif /* KV_ENGINE_H */
