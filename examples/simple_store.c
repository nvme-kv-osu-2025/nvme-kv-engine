/**
 * Simple Store/Retrieve Example
 *
 * Demonstrates basic synchronous operations
 */

#include "kv_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <device_path>\n", argv[0]);
        fprintf(stderr, "Example: %s /dev/kvemul\n", argv[0]);
        return 1;
    }

    /* Initialize engine */
    kv_engine_config_t config = {
        .device_path = argv[1],
        .emul_config_file = "../../lib/KVSSD/PDK/core/kvssd_emul.conf",
        .memory_pool_size = 16 * 1024 * 1024,  /* 16MB */
        .queue_depth = 64,
        .num_worker_threads = 0,  /* No async threads for now */
        .enable_stats = 1
    };

    kv_engine_t* engine;
    kv_result_t result = kv_engine_init(&engine, &config);
    if (result != KV_SUCCESS) {
        fprintf(stderr, "Failed to initialize engine: %d\n", result);
        return 1;
    }

    printf("Engine initialized successfully!\n");

    /* Store some data */
    const char* key = "user:12345";
    const char* value = "John Doe - john@example.com";

    printf("\nStoring: key='%s', value='%s'\n", key, value);
    result = kv_engine_store(engine, key, strlen(key), value, strlen(value));
    if (result != KV_SUCCESS) {
        fprintf(stderr, "Store failed: %d\n", result);
        goto cleanup;
    }
    printf("Store successful!\n");

    /* Retrieve the data */
    void* retrieved_value = NULL;
    size_t retrieved_len = 0;

    printf("\nRetrieving key='%s'\n", key);
    result = kv_engine_retrieve(engine, key, strlen(key),
                                &retrieved_value, &retrieved_len);
    if (result != KV_SUCCESS) {
        fprintf(stderr, "Retrieve failed: %d\n", result);
        goto cleanup;
    }

    printf("Retrieved: '%.*s'\n", (int)retrieved_len, (char*)retrieved_value);
    free(retrieved_value);

    /* Check existence */
    int exists = 0;
    result = kv_engine_exists(engine, key, strlen(key), &exists);
    printf("Key exists: %s\n", exists ? "yes" : "no");

    /* Delete the key */
    printf("\nDeleting key='%s'\n", key);
    result = kv_engine_delete(engine, key, strlen(key));
    if (result != KV_SUCCESS) {
        fprintf(stderr, "Delete failed: %d\n", result);
        goto cleanup;
    }
    printf("Delete successful!\n");

    /* Verify deletion */
    result = kv_engine_exists(engine, key, strlen(key), &exists);
    printf("Key exists after delete: %s\n", exists ? "yes" : "no");

    /* Print statistics */
    kv_engine_stats_t stats;
    kv_engine_get_stats(engine, &stats);
    printf("\n=== Statistics ===\n");
    printf("Total operations: %lu\n", stats.total_ops);
    printf("Read ops: %lu\n", stats.read_ops);
    printf("Write ops: %lu\n", stats.write_ops);
    printf("Delete ops: %lu\n", stats.delete_ops);
    printf("Failed ops: %lu\n", stats.failed_ops);
    printf("Bytes written: %lu\n", stats.bytes_written);
    printf("Bytes read: %lu\n", stats.bytes_read);

cleanup:
    kv_engine_cleanup(engine);
    printf("\nEngine cleaned up.\n");
    return 0;
}
