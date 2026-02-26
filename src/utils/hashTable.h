#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include "../../lib/uthash.h"
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct hash_entry {
  char key[256];     // key data (255 bytes + null terminator)
  uint32_t key_len;  // actual key length
  UT_hash_handle hh; // makes structure hashable
};

typedef struct {
  struct hash_entry *head;
  pthread_mutex_t lock;
} hash_table_t;

// Creates an empty hash table with a mutex.
static inline int create_table(hash_table_t *table) {
  if (!table) {
    return -1;
  }
  table->head = NULL;
  return pthread_mutex_init(&table->lock, NULL);
}

// Adds a key to the hash table if missing.
static inline void add_key(hash_table_t *table, const void *key,
                           uint32_t key_len) {
  if (!table || !key || key_len == 0 || key_len > 255) {
    return;
  }

  pthread_mutex_lock(&table->lock);

  struct hash_entry *entry = NULL;
  HASH_FIND(hh, table->head, key, key_len, entry);
  if (entry) {
    pthread_mutex_unlock(&table->lock);
    return;
  }

  entry = (struct hash_entry *)malloc(sizeof(struct hash_entry));
  if (!entry) {
    pthread_mutex_unlock(&table->lock);
    return;
  }

  memcpy(entry->key, key, key_len);
  entry->key_len = key_len;
  HASH_ADD_KEYPTR(hh, table->head, entry->key, key_len, entry);

  pthread_mutex_unlock(&table->lock);
}

// Checks if a key exists in the table
static inline uint8_t key_in_table(hash_table_t *table, const void *key,
                                   uint32_t key_len) {
  if (!table || !key || key_len == 0 || key_len > 255) {
    return 0;
  }

  pthread_mutex_lock(&table->lock);

  struct hash_entry *entry = NULL;
  HASH_FIND(hh, table->head, key, key_len, entry);

  uint8_t found = (entry != NULL) ? 1 : 0;
  pthread_mutex_unlock(&table->lock);
  return found;
}

// Deletes a key from the hash table
static inline void delete_key(hash_table_t *table, const void *key,
                              uint32_t key_len) {
  if (!table || !key || key_len == 0 || key_len > 255) {
    return;
  }

  pthread_mutex_lock(&table->lock);

  struct hash_entry *entry = NULL;
  HASH_FIND(hh, table->head, key, key_len, entry);
  if (entry) {
    HASH_DEL(table->head, entry);
    free(entry);
  }

  pthread_mutex_unlock(&table->lock);
}

// Frees all entries in the hash table
static inline void free_table(hash_table_t *table) {
  if (!table) {
    return;
  }

  pthread_mutex_lock(&table->lock);

  struct hash_entry *current;
  struct hash_entry *tmp;

  HASH_ITER(hh, table->head, current, tmp) {
    HASH_DEL(table->head, current);
    free(current);
  }

  pthread_mutex_unlock(&table->lock);
  pthread_mutex_destroy(&table->lock);
}

#endif // HASH_TABLE_H
