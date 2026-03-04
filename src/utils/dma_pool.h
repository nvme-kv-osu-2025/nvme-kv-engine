/**
 * DMA Buffer Pool
 *
 * Pre-allocates a fixed number of DMA-aligned buffers at init time and
 * recycles them across store/retrieve operations, avoiding per-operation
 * posix_memalign() / free() syscall overhead.
 *
 * all buffers in the pool are the same size. If the pool is exhausted,
 * callers should fall back to dma_alloc().
 */

#ifndef DMA_POOL_H
#define DMA_POOL_H

#include <pthread.h>
#include <stddef.h>

/**
 * pool of fixed size DMA aligned buffers backed by a stack free-list.
 */
typedef struct {
  void **free_list;
  void **all_buffers; // all allocated buffers (for ownership checks)
  int top;            // idx of next available buffer (-1 if empty)
  size_t buffer_size;
  size_t count;
  pthread_mutex_t lock;
} dma_pool_t;

/**
 * Create a new DMA buffer pool.
 * Pre-allocates count buffers of buffer_size bytes each.
 *
 * @param buffer_size Size of each buffer in bytes
 * @param count       Number of buffers to pre-allocate
 * @return Pointer to pool, or NULL on failure
 */
dma_pool_t *dma_pool_create(size_t buffer_size, size_t count);

/**
 * Acquire a buffer from the pool.
 * Returns NULL if the pool is exhausted — caller should fall back to dma_alloc.
 *
 * @param pool The buffer pool
 * @return Pointer to a DMA-aligned buffer, or NULL if pool is empty
 */
void *dma_pool_acquire(dma_pool_t *pool);

/**
 * Return a buffer to the pool.
 * The buffer must have been acquired from this pool
 *
 * @param pool   The buffer pool
 * @param buffer Buffer to return
 */
void dma_pool_release(dma_pool_t *pool, void *buffer);

/**
 * Check if a buffer was allocated from this pool.
 * Used to route kv_engine_free_buffer correctly.
 *
 * @param pool   The buffer pool
 * @param buffer Buffer to check
 * @return 1 if buffer belongs to pool, 0 otherwise
 */
int dma_pool_owns(dma_pool_t *pool, void *buffer);

/**
 * Destroy the pool and free all buffers.
 *
 * @param pool The buffer pool to destroy
 */
void dma_pool_destroy(dma_pool_t *pool);

#endif /* DMA_POOL_H */
