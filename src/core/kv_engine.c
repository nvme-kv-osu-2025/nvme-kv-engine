/**
 * Core KV Engine Implementation
 */

#include "kv_engine.h"
#include "kv_engine_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

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
        default:
            return KV_ERR_IO;
    }
}

/* ============================================================================
 * Lifecycle Management
 * ============================================================================ */

kv_result_t kv_engine_init(kv_engine_t** engine, const kv_engine_config_t* config) {
    if (!engine || !config || !config->device_path) {
        return KV_ERR_INVALID_PARAM;
    }

    /* Allocate engine structure */
    kv_engine_t* eng = (kv_engine_t*)malloc(sizeof(kv_engine_t));
    if (!eng) {
        return KV_ERR_NO_MEMORY;
    }

    /* Copy configuration */
    eng->config = *config;
    if (config->device_path) {
        eng->config.device_path = strdup(config->device_path);
    }
    if (config->emul_config_file) {
        eng->config.emul_config_file = strdup(config->emul_config_file);
    }

    /* Open Samsung KV device */
    kvs_result kvs_res = kvs_open_device(eng->config.device_path, &eng->device);
    if (kvs_res != KVS_SUCCESS) {
        fprintf(stderr, "Failed to open device %s: 0x%x\n",
                eng->config.device_path, kvs_res);
        free(eng);
        return KV_ERR_DEVICE_OPEN;
    }

    /* Open or create keyspace */
    const char* keyspace_name = "nvme_kv_engine";
    kvs_res = kvs_open_key_space(eng->device, keyspace_name, &eng->keyspace);

    if (kvs_res != KVS_SUCCESS) {
        /* Try to create keyspace */
        kvs_key_space_name ks_name;
        ks_name.name = (char*)keyspace_name;
        ks_name.name_len = strlen(keyspace_name);

        kvs_option_key_space option = { KVS_KEY_ORDER_NONE };
        kvs_res = kvs_create_key_space(eng->device, &ks_name, 0, option);

        if (kvs_res != KVS_SUCCESS) {
            fprintf(stderr, "Failed to create keyspace: 0x%x\n", kvs_res);
            kvs_close_device(eng->device);
            free(eng);
            return KV_ERR_DEVICE_OPEN;
        }

        /* Now open it */
        kvs_res = kvs_open_key_space(eng->device, keyspace_name, &eng->keyspace);
        if (kvs_res != KVS_SUCCESS) {
            fprintf(stderr, "Failed to open newly created keyspace: 0x%x\n", kvs_res);
            kvs_close_device(eng->device);
            free(eng);
            return KV_ERR_DEVICE_OPEN;
        }
    }

    /* Initialize memory pool */
    size_t pool_size = config->memory_pool_size > 0 ?
                       config->memory_pool_size : (16 * 1024 * 1024); /* 16MB default */
    eng->mem_pool = memory_pool_create(pool_size);
    if (!eng->mem_pool) {
        kvs_close_key_space(eng->keyspace);
        kvs_close_device(eng->device);
        free(eng);
        return KV_ERR_NO_MEMORY;
    }

    /* Initialize thread pool for async ops */
    if (config->num_worker_threads > 0) {
        eng->workers = thread_pool_create(config->num_worker_threads);
        if (!eng->workers) {
            memory_pool_destroy(eng->mem_pool);
            kvs_close_key_space(eng->keyspace);
            kvs_close_device(eng->device);
            free(eng);
            return KV_ERR_NO_MEMORY;
        }
    }

    /* Initialize statistics */
    pthread_mutex_init(&eng->stats_lock, NULL);
    memset(&eng->stats, 0, sizeof(kv_engine_stats_t));

    eng->initialized = 1;
    *engine = eng;

    return KV_SUCCESS;
}

void kv_engine_cleanup(kv_engine_t* engine) {
    if (!engine) {
        return;
    }

    /* Shutdown thread pool */
    if (engine->workers) {
        thread_pool_destroy(engine->workers);
    }

    /* Cleanup memory pool */
    if (engine->mem_pool) {
        memory_pool_destroy(engine->mem_pool);
    }

    /* Close Samsung KVSSD */
    if (engine->keyspace) {
        kvs_close_key_space(engine->keyspace);
    }
    if (engine->device) {
        kvs_close_device(engine->device);
    }

    /* Free config strings */
    if (engine->config.device_path) {
        free((void*)engine->config.device_path);
    }
    if (engine->config.emul_config_file) {
        free((void*)engine->config.emul_config_file);
    }

    pthread_mutex_destroy(&engine->stats_lock);
    free(engine);
}

/* ============================================================================
 * Synchronous Operations
 * ============================================================================ */

kv_result_t kv_engine_store(kv_engine_t* engine,
                            const void* key, size_t key_len,
                            const void* value, size_t value_len) {
    if (!engine || !engine->initialized || !key || !value) {
        return KV_ERR_INVALID_PARAM;
    }

    if (key_len < 4 || key_len > 255) {
        return KV_ERR_INVALID_PARAM;
    }

    if (value_len > (2 * 1024 * 1024)) { /* 2MB max */
        return KV_ERR_VALUE_TOO_LARGE;
    }

    /* Prepare Samsung KV structures */
    kvs_key kv_key;
    kv_key.key = (void*)key;
    kv_key.length = key_len;

    kvs_value kv_value;
    kv_value.value = (void*)value;
    kv_value.length = value_len;
    kv_value.actual_value_size = value_len;
    kv_value.offset = 0;

    /* Perform store operation */
    kvs_option_store option;
    option.st_type = KVS_STORE_POST;  /* Overwrite if exists */
    kvs_result kvs_res = kvs_store_kvp(engine->keyspace, &kv_key, &kv_value, &option);

    /* Update statistics */
    update_stats(engine, 0, 1, 0, kvs_res == KVS_SUCCESS, value_len);

    return map_kvs_result(kvs_res);
}

kv_result_t kv_engine_retrieve(kv_engine_t* engine,
                               const void* key, size_t key_len,
                               void** value, size_t* value_len) {
    if (!engine || !engine->initialized || !key || !value || !value_len) {
        return KV_ERR_INVALID_PARAM;
    }

    if (key_len < 4 || key_len > 255) {
        return KV_ERR_INVALID_PARAM;
    }

    /* Prepare key */
    kvs_key kv_key;
    kv_key.key = (void*)key;
    kv_key.length = key_len;

    /* Allocate buffer for value (max 2MB) */
    size_t max_value_size = 2 * 1024 * 1024;
    void* buffer = malloc(max_value_size);
    if (!buffer) {
        return KV_ERR_NO_MEMORY;
    }

    /* Prepare value structure */
    kvs_value kv_value;
    kv_value.value = buffer;
    kv_value.length = max_value_size;
    kv_value.actual_value_size = 0;
    kv_value.offset = 0;

    /* Retrieve the value */
    kvs_option_retrieve option;
    option.kvs_retrieve_delete = false;  /* Just retrieve, don't delete */
    kvs_result kvs_res = kvs_retrieve_kvp(engine->keyspace, &kv_key, &option, &kv_value);

    if (kvs_res != KVS_SUCCESS) {
        free(buffer);
        update_stats(engine, 1, 0, 0, 0, 0);
        return map_kvs_result(kvs_res);
    }

    *value = buffer;
    *value_len = kv_value.actual_value_size;

    update_stats(engine, 1, 0, 0, 1, kv_value.actual_value_size);
    return KV_SUCCESS;
}

kv_result_t kv_engine_delete(kv_engine_t* engine,
                             const void* key, size_t key_len) {
    if (!engine || !engine->initialized || !key) {
        return KV_ERR_INVALID_PARAM;
    }

    if (key_len < 4 || key_len > 255) {
        return KV_ERR_INVALID_PARAM;
    }

    /* Prepare key */
    kvs_key kv_key;
    kv_key.key = (void*)key;
    kv_key.length = key_len;

    /* Perform delete */
    kvs_option_delete option;
    option.kvs_delete_error = false;  /* Don't error if key doesn't exist */
    kvs_result kvs_res = kvs_delete_kvp(engine->keyspace, &kv_key, &option);

    update_stats(engine, 0, 0, 1, kvs_res == KVS_SUCCESS, 0);
    return map_kvs_result(kvs_res);
}

kv_result_t kv_engine_exists(kv_engine_t* engine,
                             const void* key, size_t key_len,
                             int* exists) {
    if (!engine || !engine->initialized || !key || !exists) {
        return KV_ERR_INVALID_PARAM;
    }

    if (key_len < 4 || key_len > 255) {
        return KV_ERR_INVALID_PARAM;
    }

    /* Prepare key */
    kvs_key kv_key;
    kv_key.key = (void*)key;
    kv_key.length = key_len;

    /* Check existence */
    uint8_t result_buffer;
    kvs_exist_list exist_list;
    exist_list.num_keys = 1;
    exist_list.keys = &kv_key;
    exist_list.length = 1;
    exist_list.result_buffer = &result_buffer;

    kvs_result kvs_res = kvs_exist_kv_pairs(engine->keyspace, 1, &kv_key, &exist_list);

    if (kvs_res != KVS_SUCCESS) {
        return map_kvs_result(kvs_res);
    }

    *exists = (result_buffer != 0);
    return KV_SUCCESS;
}

/* ============================================================================
 * Statistics
 * ============================================================================ */

void update_stats(kv_engine_t* engine, int is_read, int is_write,
                  int is_delete, int success, size_t bytes) {
    pthread_mutex_lock(&engine->stats_lock);

    engine->stats.total_ops++;

    if (is_read) engine->stats.read_ops++;
    if (is_write) engine->stats.write_ops++;
    if (is_delete) engine->stats.delete_ops++;

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

kv_result_t kv_engine_get_stats(kv_engine_t* engine, kv_engine_stats_t* stats) {
    if (!engine || !stats) {
        return KV_ERR_INVALID_PARAM;
    }

    pthread_mutex_lock(&engine->stats_lock);
    *stats = engine->stats;
    pthread_mutex_unlock(&engine->stats_lock);

    return KV_SUCCESS;
}

void kv_engine_reset_stats(kv_engine_t* engine) {
    if (!engine) {
        return;
    }

    pthread_mutex_lock(&engine->stats_lock);
    memset(&engine->stats, 0, sizeof(kv_engine_stats_t));
    pthread_mutex_unlock(&engine->stats_lock);
}
