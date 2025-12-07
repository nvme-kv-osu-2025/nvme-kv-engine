/**
 * Simple Memory Pool Implementation (Bump Allocator)
 * Thread-safe bump allocator for efficient small allocations
 */

#include "memory_pool.h"
#include <stdlib.h>
#include <stdint.h>

#define ALIGNMENT 8
#define ALIGN_UP(n, align) (((n) + (align) - 1) & ~((align) - 1))

memory_pool_t* memory_pool_create(size_t size) {
    memory_pool_t* pool = (memory_pool_t*)malloc(sizeof(memory_pool_t));
    if (!pool) {
        return NULL;
    }

    pool->base = malloc(size);
    if (!pool->base) {
        free(pool);
        return NULL;
    }

    pool->size = size;
    pool->used = 0;
    pthread_mutex_init(&pool->lock, NULL);

    return pool;
}

void* memory_pool_alloc(memory_pool_t* pool, size_t size) {
    if (!pool) {
        return NULL;
    }

    pthread_mutex_lock(&pool->lock);

    /* Align current offset and requested size */
    size_t aligned_offset = ALIGN_UP(pool->used, ALIGNMENT);
    size_t aligned_size = ALIGN_UP(size, ALIGNMENT);

    /* Check if we have enough space */
    if (aligned_offset + aligned_size > pool->size) {
        pthread_mutex_unlock(&pool->lock);
        return NULL;  /* Pool exhausted */
    }

    /* Bump allocate */
    void* ptr = (char*)pool->base + aligned_offset;
    pool->used = aligned_offset + aligned_size;

    pthread_mutex_unlock(&pool->lock);
    return ptr;
}

void memory_pool_free(memory_pool_t* pool, void* ptr) {
    /* No-op: bump allocator doesn't support individual frees */
    (void)pool;
    (void)ptr;
}

void memory_pool_destroy(memory_pool_t* pool) {
    if (!pool) {
        return;
    }

    if (pool->base) {
        free(pool->base);
    }

    pthread_mutex_destroy(&pool->lock);
    free(pool);
}
