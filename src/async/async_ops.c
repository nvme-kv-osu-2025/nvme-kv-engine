/**
 * Asynchronous Operations Implementation
 *
 * Each async function copies key/value data into an async_context_t,
 * submits it to the thread pool, and returns immediately. A worker
 * thread later executes the corresponding sync operation and invokes
 * the completion callback.
 */

#include "../utils/dma_alloc.h"
#include "kv_engine.h"
#include "kv_engine_internal.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Internal Helpers
 * ============================================================================
 */

<<<<<<< HEAD
kv_result_t kv_engine_store_async(kv_engine_t *engine, const void *key,
                                  size_t key_len, const void *value,
                                  size_t value_len, kv_completion_cb callback,
                                  void *user_data, bool overwrite) {
  /* TODO: Implement async store */
  /* For now, call synchronous version and invoke callback */
  // kv_result_t result = kv_engine_store(engine, key, key_len, value,
  // value_len, false);

  // if (callback) {
  //     callback(result, user_data);
  // }

  return KV_SUCCESS;
}

kv_result_t kv_engine_retrieve_async(kv_engine_t *engine, const void *key,
                                     size_t key_len, kv_completion_cb callback,
                                     void *user_data) {
  /* TODO: Implement async retrieve */
  return KV_ERR_NOT_INITIALIZED;
}

kv_result_t kv_engine_delete_async(kv_engine_t *engine, const void *key,
                                   size_t key_len, kv_completion_cb callback,
                                   void *user_data) {
  /* TODO: Implement async delete */
  // kv_result_t result = kv_engine_delete(engine, key, key_len);

  // if (callback) {
  //     callback(result, user_data);
  // }

=======
static async_context_t *
async_context_create(kv_engine_t *engine, async_op_type_t op_type,
                     const void *key, size_t key_len, const void *value,
                     size_t value_len, kv_completion_cb callback,
                     void *user_data, bool overwrite) {
  async_context_t *ctx = (async_context_t *)malloc(sizeof(async_context_t));
  if (!ctx) {
    return NULL;
  }

  ctx->engine = engine;
  ctx->op_type = op_type;
  ctx->callback = callback;
  ctx->user_data = user_data;
  ctx->overwrite = overwrite;
  ctx->key_len = key_len;
  ctx->value_len = value_len;
  ctx->value_buffer = NULL;

  /* Copy key data — caller's buffer may go out of scope */
  ctx->key_buffer = malloc(key_len);
  if (!ctx->key_buffer) {
    free(ctx);
    return NULL;
  }
  memcpy(ctx->key_buffer, key, key_len);

  /* Copy value data for store operations */
  if (value && value_len > 0) {
    ctx->value_buffer = malloc(value_len);
    if (!ctx->value_buffer) {
      free(ctx->key_buffer);
      free(ctx);
      return NULL;
    }
    memcpy(ctx->value_buffer, value, value_len);
  }

  return ctx;
}

static void async_context_free(void *arg) {
  async_context_t *ctx = (async_context_t *)arg;
  if (!ctx) {
    return;
  }
  free(ctx->key_buffer);
  free(ctx->value_buffer);
  free(ctx);
}

/* Worker function executed on a thread pool thread */
static void *async_worker_func(void *arg) {
  async_context_t *ctx = (async_context_t *)arg;
  kv_result_t result;

  switch (ctx->op_type) {
  case ASYNC_OP_STORE:
    result = kv_engine_store(ctx->engine, ctx->key_buffer, ctx->key_len,
                             ctx->value_buffer, ctx->value_len, ctx->overwrite);
    break;

  case ASYNC_OP_RETRIEVE: {
    void *value = NULL;
    size_t value_len = 0;
    result = kv_engine_retrieve(ctx->engine, ctx->key_buffer, ctx->key_len,
                                &value, &value_len, false);
    if (result == KV_SUCCESS && value) {
      dma_free(value);
    }
    break;
  }

  case ASYNC_OP_DELETE:
    result = kv_engine_delete(ctx->engine, ctx->key_buffer, ctx->key_len);
    break;

  default:
    result = KV_ERR_INVALID_PARAM;
    break;
  }

  /* Invoke completion callback */
  if (ctx->callback) {
    ctx->callback(result, ctx->user_data);
  }

  async_context_free(ctx);
  return NULL;
}

/* ============================================================================
 * Public Async API
 * ============================================================================
 */

kv_result_t kv_engine_store_async(kv_engine_t *engine, const void *key,
                                  size_t key_len, const void *value,
                                  size_t value_len, kv_completion_cb callback,
                                  void *user_data, bool overwrite) {
  if (!engine || !engine->initialized || !key || !value) {
    return KV_ERR_INVALID_PARAM;
  }
  if (key_len < 4 || key_len > 255) {
    return KV_ERR_INVALID_PARAM;
  }
  if (!engine->workers) {
    return KV_ERR_NOT_INITIALIZED;
  }

  async_context_t *ctx =
      async_context_create(engine, ASYNC_OP_STORE, key, key_len, value,
                           value_len, callback, user_data, overwrite);
  if (!ctx) {
    return KV_ERR_NO_MEMORY;
  }

  if (thread_pool_submit(engine->workers, async_worker_func, ctx,
                         async_context_free) != 0) {
    async_context_free(ctx);
    return KV_ERR_IO;
  }

  return KV_SUCCESS;
}

kv_result_t kv_engine_retrieve_async(kv_engine_t *engine, const void *key,
                                     size_t key_len, kv_completion_cb callback,
                                     void *user_data) {
  if (!engine || !engine->initialized || !key) {
    return KV_ERR_INVALID_PARAM;
  }
  if (key_len < 4 || key_len > 255) {
    return KV_ERR_INVALID_PARAM;
  }
  if (!engine->workers) {
    return KV_ERR_NOT_INITIALIZED;
  }

  async_context_t *ctx =
      async_context_create(engine, ASYNC_OP_RETRIEVE, key, key_len, NULL, 0,
                           callback, user_data, false);
  if (!ctx) {
    return KV_ERR_NO_MEMORY;
  }

  if (thread_pool_submit(engine->workers, async_worker_func, ctx,
                         async_context_free) != 0) {
    async_context_free(ctx);
    return KV_ERR_IO;
  }

  return KV_SUCCESS;
}

kv_result_t kv_engine_delete_async(kv_engine_t *engine, const void *key,
                                   size_t key_len, kv_completion_cb callback,
                                   void *user_data) {
  if (!engine || !engine->initialized || !key) {
    return KV_ERR_INVALID_PARAM;
  }
  if (key_len < 4 || key_len > 255) {
    return KV_ERR_INVALID_PARAM;
  }
  if (!engine->workers) {
    return KV_ERR_NOT_INITIALIZED;
  }

  async_context_t *ctx =
      async_context_create(engine, ASYNC_OP_DELETE, key, key_len, NULL, 0,
                           callback, user_data, false);
  if (!ctx) {
    return KV_ERR_NO_MEMORY;
  }

  if (thread_pool_submit(engine->workers, async_worker_func, ctx,
                         async_context_free) != 0) {
    async_context_free(ctx);
    return KV_ERR_IO;
  }

>>>>>>> 215e214 (Implement thread pool and async operations)
  return KV_SUCCESS;
}
