#include "kv_engine_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

kv_result_t kv_engine_resolve_device_paths(const kv_engine_config_t *config,
                                           const char **effective_paths,
                                           uint32_t *effective_count) {
  if (!config || !effective_paths || !effective_count) {
    return KV_ERR_INVALID_PARAM;
  }

  if (config->num_devices > 0) {
    if (config->num_devices > KV_MAX_DEVICES) {
      return KV_ERR_INVALID_PARAM;
    }
    for (uint32_t i = 0; i < config->num_devices; i++) {
      if (!config->device_paths[i]) {
        return KV_ERR_INVALID_PARAM;
      }
      effective_paths[i] = config->device_paths[i];
    }
    *effective_count = config->num_devices;
    return KV_SUCCESS;
  }

  if (!config->device_path) {
    return KV_ERR_INVALID_PARAM;
  }
  effective_paths[0] = config->device_path;
  *effective_count = 1;
  return KV_SUCCESS;
}

uint32_t kv_engine_shard_for_key(const void *key, size_t key_len,
                                 uint32_t num_devices) {
  const uint8_t *data = (const uint8_t *)key;
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < key_len; i++) {
    hash ^= data[i];
    hash *= 16777619u;
  }
  return hash % num_devices;
}

kv_result_t kv_engine_open_device(kv_device_ctx_t *ctx, const char *path,
                                  uint32_t dev_index) {
  kvs_result kvs_res = kvs_open_device((char *)path, &ctx->device);
  if (kvs_res != KVS_SUCCESS) {
    fprintf(stderr, "Failed to open device %s: 0x%x\n", path, kvs_res);
    return KV_ERR_DEVICE_OPEN;
  }

  char ks_name_buf[64];
  snprintf(ks_name_buf, sizeof(ks_name_buf), "nvme_kv_engine_%u", dev_index);

  kvs_res = kvs_open_key_space(ctx->device, ks_name_buf, &ctx->keyspace);
  if (kvs_res != KVS_SUCCESS) {
    kvs_key_space_name ks_name;
    ks_name.name = ks_name_buf;
    ks_name.name_len = strlen(ks_name_buf);

    kvs_option_key_space option = {KVS_KEY_ORDER_NONE};
    kvs_res = kvs_create_key_space(ctx->device, &ks_name, 0, option);
    if (kvs_res != KVS_SUCCESS) {
      fprintf(stderr, "Failed to create keyspace on %s: 0x%x\n", path, kvs_res);
      kvs_close_device(ctx->device);
      ctx->device = NULL;
      return KV_ERR_DEVICE_OPEN;
    }

    kvs_res = kvs_open_key_space(ctx->device, ks_name_buf, &ctx->keyspace);
    if (kvs_res != KVS_SUCCESS) {
      fprintf(stderr, "Failed to open keyspace on %s: 0x%x\n", path, kvs_res);
      kvs_close_device(ctx->device);
      ctx->device = NULL;
      return KV_ERR_DEVICE_OPEN;
    }
  }

  ctx->device_path = strdup(path);

  /* Initialize health tracking fields */
  atomic_store(&ctx->healthy, true);
  atomic_store(&ctx->consecutive_errors, 0);
  atomic_store(&ctx->total_errors, 0);
  atomic_store(&ctx->total_ops, 0);
  ctx->max_consecutive_errors = 10;

  return KV_SUCCESS;
}

void kv_engine_close_device(kv_device_ctx_t *ctx) {
  if (ctx->keyspace) {
    kvs_close_key_space(ctx->keyspace);
    ctx->keyspace = NULL;
  }
  if (ctx->device) {
    kvs_close_device(ctx->device);
    ctx->device = NULL;
  }
  if (ctx->device_path) {
    free(ctx->device_path);
    ctx->device_path = NULL;
  }
}
