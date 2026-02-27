/**
 * DMA Buffer Pool Implementation
 */

#include "dma_pool.h"
#include "dma_alloc.h"
#include <stdlib.h>

dma_pool_t *dma_pool_create(size_t buffer_size, size_t count) {
  dma_pool_t *pool = malloc(sizeof(dma_pool_t));
  if (!pool) {
    return NULL;
  }

  pool->free_list = malloc(sizeof(void *) * count);
  if (!pool->free_list) {
    free(pool);
    return NULL;
  }

  pool->buffer_size = buffer_size;
  pool->count = count;
  pool->top = -1;

  // pre-allocate all buffers and push onto the free-list
  for (size_t i = 0; i < count; i++) {
    void *buf = dma_alloc(buffer_size);
    if (!buf) {
      dma_pool_destroy(pool);
      return NULL;
    }
    pool->top++;
    pool->free_list[pool->top] = buf;
  }

  pthread_mutex_init(&pool->lock, NULL);
  return pool;
}

void *dma_pool_acquire(dma_pool_t *pool) {

  pthread_mutex_lock(&pool->lock);

  // check if pool is exhausted
  if (pool->top < 0) {
    pthread_mutex_unlock(&pool->lock);
    return NULL;
  }

  // pop buffer from free-list stack
  void *buf = pool->free_list[pool->top];
  pool->top--;
  pthread_mutex_unlock(&pool->lock);
  // return NULL if top == -1 (pool exhausted).
  return buf;
}

void dma_pool_release(dma_pool_t *pool, void *buffer) {

  // push buffer back onto the free-list stack
  pthread_mutex_lock(&pool->lock);

  if (pool->top + 1 >= pool->count) {
    // buffer doesn't belong to this pool or pool is already full
    pthread_mutex_unlock(&pool->lock);
    return;
  }
  pool->top++;
  pool->free_list[pool->top] = buffer;
  pthread_mutex_unlock(&pool->lock);
}

void dma_pool_destroy(dma_pool_t *pool) {
  if (!pool) {
    return;
  }

  if (pool->free_list) {
    for (int i = 0; i <= pool->top; i++) {
      dma_free(pool->free_list[i]);
    }
    free(pool->free_list);
  }

  pthread_mutex_destroy(&pool->lock);
  free(pool);
}
