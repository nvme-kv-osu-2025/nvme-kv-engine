#ifndef HASH_TABLE_H
#define HASH_TABLE_H

#include "../../lib/uthash.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct hash_entry {
  char key[256];     // key data (255 bytes + null terminator)
  uint32_t key_len;  // actual key length
  UT_hash_handle hh; // makes structure hashable
};

// Creates an empty hash table, returns null as initial table as UThash
// automatically handles table creation with first operation
static inline struct hash_entry *create_table(void) { return NULL; }

// Adds a key to the hash table
static inline void add_key(struct hash_entry **table, const void *key,
                           uint32_t key_len) {
  struct hash_entry *entry =
      (struct hash_entry *)malloc(sizeof(struct hash_entry));

  memcpy(entry->key, key, key_len);
  entry->key_len = key_len;
  HASH_ADD_KEYPTR(hh, *table, entry->key, key_len, entry);
}

// Checks if a key exists in the table
static inline uint8_t key_in_table(struct hash_entry **table, const void *key,
                                   uint32_t key_len) {
  struct hash_entry *entry;

  HASH_FIND(hh, *table, key, key_len, entry);
  if (entry) {
    return 1;
  }
  return 0;
}

// Deletes a key from the hash table
static inline void delete_key(struct hash_entry **table, const void *key,
                              uint32_t key_len) {
  struct hash_entry *entry;

  HASH_FIND(hh, *table, key, key_len, entry);
  if (entry) {
    HASH_DEL(*table, entry);
    free(entry);
  }
}

// Frees all entries in the hash table
static inline void free_table(struct hash_entry **table) {
  struct hash_entry *current;
  struct hash_entry *tmp;

  HASH_ITER(hh, *table, current, tmp) {
    HASH_DEL(*table, current);
    free(current);
  }
}

#endif // HASH_TABLE_H
