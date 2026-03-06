/**
 * Thread Pool Implementation
 *
 * Bounded work queue with pre-created worker threads.
 * Workers sleep on a condition variable when idle and wake
 * when work is enqueued. Graceful shutdown drains the queue
 * before joining threads.
 */

#include "kv_engine_internal.h"
#include <stdlib.h>

/* Worker thread entry point */
static void *thread_pool_worker(void *arg) {
  thread_pool_t *pool = (thread_pool_t *)arg;

  while (1) {
    pthread_mutex_lock(&pool->queue_lock);

    /* Wait until there's work or we're shutting down */
    while (pool->queue_size == 0 && !pool->shutdown) {
      pthread_cond_wait(&pool->queue_not_empty, &pool->queue_lock);
    }

    /* If shutting down and queue is empty, exit */
    if (pool->shutdown && pool->queue_size == 0) {
      pthread_mutex_unlock(&pool->queue_lock);
      break;
    }

    /* Dequeue work item from head */
    work_item_t *item = pool->queue_head;
    pool->queue_head = item->next;
    if (pool->queue_head == NULL) {
      pool->queue_tail = NULL;
    }
    pool->queue_size--;

    /* Signal submitters blocked on a full queue */
    pthread_cond_signal(&pool->queue_not_full);

    pthread_mutex_unlock(&pool->queue_lock);

    /* Execute work outside the lock */
    item->func(item->arg);
    free(item);
  }

  return NULL;
}

thread_pool_t *thread_pool_create(uint32_t num_threads, uint32_t queue_depth) {
  if (num_threads == 0) {
    return NULL;
  }

  thread_pool_t *pool = (thread_pool_t *)malloc(sizeof(thread_pool_t));
  if (!pool) {
    return NULL;
  }

  pool->threads = (pthread_t *)calloc(num_threads, sizeof(pthread_t));
  if (!pool->threads) {
    free(pool);
    return NULL;
  }

  pool->num_threads = num_threads;
  pool->shutdown = 0;
  pool->queue_head = NULL;
  pool->queue_tail = NULL;
  pool->queue_size = 0;
  pool->queue_capacity = (queue_depth > 0) ? queue_depth : 128;

  pthread_mutex_init(&pool->queue_lock, NULL);
  pthread_cond_init(&pool->queue_not_empty, NULL);
  pthread_cond_init(&pool->queue_not_full, NULL);

  /* Create worker threads */
  for (uint32_t i = 0; i < num_threads; i++) {
    if (pthread_create(&pool->threads[i], NULL, thread_pool_worker, pool) !=
        0) {
      /* Partial failure: shut down already-created threads */
      pool->shutdown = 1;
      pthread_cond_broadcast(&pool->queue_not_empty);
      for (uint32_t j = 0; j < i; j++) {
        pthread_join(pool->threads[j], NULL);
      }
      pthread_mutex_destroy(&pool->queue_lock);
      pthread_cond_destroy(&pool->queue_not_empty);
      pthread_cond_destroy(&pool->queue_not_full);
      free(pool->threads);
      free(pool);
      return NULL;
    }
  }

  return pool;
}

int thread_pool_submit(thread_pool_t *pool, void *(*func)(void *), void *arg,
                       void (*cleanup)(void *)) {
  if (!pool || !func) {
    return -1;
  }

  work_item_t *item = (work_item_t *)malloc(sizeof(work_item_t));
  if (!item) {
    return -1;
  }

  item->func = func;
  item->arg = arg;
  item->cleanup = cleanup;
  item->next = NULL;

  pthread_mutex_lock(&pool->queue_lock);

  /* Reject if shutting down */
  if (pool->shutdown) {
    pthread_mutex_unlock(&pool->queue_lock);
    free(item);
    return -1;
  }

  /* Block if queue is full (backpressure) */
  while (pool->queue_size >= pool->queue_capacity && !pool->shutdown) {
    pthread_cond_wait(&pool->queue_not_full, &pool->queue_lock);
  }

  /* Re-check shutdown after waking */
  if (pool->shutdown) {
    pthread_mutex_unlock(&pool->queue_lock);
    free(item);
    return -1;
  }

  /* Enqueue at tail */
  if (pool->queue_tail) {
    pool->queue_tail->next = item;
  } else {
    pool->queue_head = item;
  }
  pool->queue_tail = item;
  pool->queue_size++;

  /* Wake one worker */
  pthread_cond_signal(&pool->queue_not_empty);

  pthread_mutex_unlock(&pool->queue_lock);
  return 0;
}

void thread_pool_destroy(thread_pool_t *pool) {
  if (!pool) {
    return;
  }

  pthread_mutex_lock(&pool->queue_lock);
  pool->shutdown = 1;
  pthread_cond_broadcast(&pool->queue_not_empty);
  pthread_cond_broadcast(&pool->queue_not_full);
  pthread_mutex_unlock(&pool->queue_lock);

  /* Join all workers (they drain the queue before exiting) */
  for (uint32_t i = 0; i < pool->num_threads; i++) {
    pthread_join(pool->threads[i], NULL);
  }

  /* Defensive: free any leftover items after join */
  work_item_t *item = pool->queue_head;
  while (item) {
    work_item_t *next = item->next;
    if (item->cleanup) {
      item->cleanup(item->arg);
    } else {
      free(item->arg);
    }
    free(item);
    item = next;
  }

  pthread_mutex_destroy(&pool->queue_lock);
  pthread_cond_destroy(&pool->queue_not_empty);
  pthread_cond_destroy(&pool->queue_not_full);
  free(pool->threads);
  free(pool);
}
