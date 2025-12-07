/**
 * Simple Memory Pool (Bump Allocator)
 * Thread-safe bump allocator for efficient small allocations
 */

#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <pthread.h>
#include <stddef.h>

typedef struct {
  void *base;           // Base address of pool
  size_t size;          // Total pool size
  size_t used;          // Currently allocated bytes
  pthread_mutex_t lock; // Thread safety
} memory_pool_t;

/**
 * Create a new memory pool
 * @param size Total size of the pool in bytes
 * @return Pointer to the pool, or NULL on failure
 */
memory_pool_t *memory_pool_create(size_t size);

/**
 * Allocate memory from the pool
 * @param pool The memory pool
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL if pool is full
 */
void *memory_pool_alloc(memory_pool_t *pool, size_t size);

/**
 * Free memory (no-op for bump allocator)
 * @param pool The memory pool
 * @param ptr Pointer to free (ignored)
 */
void memory_pool_free(memory_pool_t *pool, void *ptr);

/**
 * Destroy the entire pool
 * @param pool The memory pool to destroy
 */
void memory_pool_destroy(memory_pool_t *pool);

#endif /* MEMORY_POOL_H */
