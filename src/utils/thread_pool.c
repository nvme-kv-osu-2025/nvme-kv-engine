/**
 * Thread Pool Implementation
 * TODO: Implement work queue and thread pool
 */

#include "kv_engine_internal.h"
#include <stdlib.h>

thread_pool_thread_t* thread_pool_thread_create_array(uint32_t num_threads) {
    thread_pool_thread_t* threads = (thread_pool_thread_t*)malloc(
        num_threads * sizeof(thread_pool_thread_t));
    if (!threads) {
        return NULL;
    }

    for (uint32_t i = 0; i < num_threads; i++) {
        threads[i].id = i;
        threads[i].busy = 0;
        threads[i].current_task = NULL;
    }

    return threads;
}

thread_pool_t* thread_pool_create(uint32_t num_threads) {
    thread_pool_t* pool = (thread_pool_t*)malloc(sizeof(thread_pool_t));
    if (!pool) {
        return NULL;
    }

    pool->total_threads = num_threads;
    pool->free_threads_count = num_threads;
    pool->busy_threads_count = 0;
    pool->shutdown = 0;
    pool->busy_threads = NULL;
    pool->free_threads = thread_pool_thread_create_array(num_threads);
    if (!pool->free_threads) {
        free(pool);
        return NULL;
    }

    return pool;
}

int thread_pool_submit(thread_pool_t* pool, void* (*func)(void*), void* arg) {
    if (!pool || !func) {
        return -1;
    }

    /* TODO: Add work to queue */
    /* For now, just execute synchronously */
    func(arg);

    return 0;
}

int thread_pool_thread_destroy(thread_pool_thread_t* thread) {
    if (!thread) {
        return -1
    }

    thread->async_context_t = NULL;

    free(thread);
    return 0;
}

int thread_pool_destroy(thread_pool_t* pool) {
    if (!pool) {
        return -1;
    }

    // stop the busy threads
    for (uint32_t i = 0; i < pool->busy_threads_count; i++) {
        if (thread_pool_thread_destroy(&pool->busy_threads[i]) != 0) {
            return -1;
        }
    }

    for (uint32_t i = 0; i < pool->free_threads_count; i++) {
        if (thread_pool_thread_destroy(&pool->free_threads[i]) != 0) {
            return -1;
        }
    }

    free(pool->busy_threads);
    free(pool->free_threads);
    free(pool);

    pool->shutdown = 1;
    return 0;

}
