/**
 * Asynchronous Operations Example
 *
 * Demonstrates async store/retrieve operations
 */

#include "kv_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    int id;
    int completed;
    kv_result_t result;
} async_context_t;

void store_callback(kv_result_t result, void* user_data) {
    async_context_t* ctx = (async_context_t*)user_data;
    ctx->result = result;
    ctx->completed = 1;

    printf("Async store %d completed with result: %d\n", ctx->id, result);
}

void delete_callback(kv_result_t result, void* user_data) {
    async_context_t* ctx = (async_context_t*)user_data;
    ctx->result = result;
    ctx->completed = 1;

    printf("Async delete %d completed with result: %d\n", ctx->id, result);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <device_path>\n", argv[0]);
        return 1;
    }

    /* Initialize engine with async support */
    kv_engine_config_t config = {
        .device_path = argv[1],
        .emul_config_file = "../../lib/KVSSD/PDK/core/kvssd_emul.conf",
        .memory_pool_size = 16 * 1024 * 1024,
        .queue_depth = 128,
        .num_worker_threads = 16,  /* More threads for async */
        .enable_stats = 1
    };

    kv_engine_t* engine;
    if (kv_engine_init(&engine, &config) != KV_SUCCESS) {
        fprintf(stderr, "Failed to initialize engine\n");
        return 1;
    }

    printf("Testing asynchronous operations...\n\n");

    #define NUM_ASYNC_OPS 10
    async_context_t contexts[NUM_ASYNC_OPS];

    /* Submit multiple async store operations */
    printf("Submitting %d async store operations...\n", NUM_ASYNC_OPS);
    for (int i = 0; i < NUM_ASYNC_OPS; i++) {
        char key[32];
        char value[128];

        snprintf(key, sizeof(key), "async_key_%d", i);
        snprintf(value, sizeof(value), "async_value_%d_data", i);

        contexts[i].id = i;
        contexts[i].completed = 0;
        contexts[i].result = KV_SUCCESS;

        kv_result_t result = kv_engine_store_async(
            engine,
            key, strlen(key),
            value, strlen(value),
            store_callback,
            &contexts[i]
        );

        if (result != KV_SUCCESS) {
            fprintf(stderr, "Failed to submit async store %d: %d\n", i, result);
        }
    }

    /* Wait for all operations to complete */
    printf("Waiting for operations to complete...\n");
    int all_completed = 0;
    while (!all_completed) {
        all_completed = 1;
        for (int i = 0; i < NUM_ASYNC_OPS; i++) {
            if (!contexts[i].completed) {
                all_completed = 0;
                break;
            }
        }
        usleep(10000); /* 10ms */
    }

    printf("\nAll store operations completed!\n");

    /* Now delete them asynchronously */
    printf("\nSubmitting %d async delete operations...\n", NUM_ASYNC_OPS);
    for (int i = 0; i < NUM_ASYNC_OPS; i++) {
        char key[32];
        snprintf(key, sizeof(key), "async_key_%d", i);

        contexts[i].completed = 0;

        kv_result_t result = kv_engine_delete_async(
            engine,
            key, strlen(key),
            delete_callback,
            &contexts[i]
        );

        if (result != KV_SUCCESS) {
            fprintf(stderr, "Failed to submit async delete %d: %d\n", i, result);
        }
    }

    /* Wait for deletions */
    all_completed = 0;
    while (!all_completed) {
        all_completed = 1;
        for (int i = 0; i < NUM_ASYNC_OPS; i++) {
            if (!contexts[i].completed) {
                all_completed = 0;
                break;
            }
        }
        usleep(10000);
    }

    printf("\nAll delete operations completed!\n");

    kv_engine_cleanup(engine);
    return 0;
}
