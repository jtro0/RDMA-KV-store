#ifndef RDMA_KV_STORE_HASH_H
#define RDMA_KV_STORE_HASH_H

#include <stdlib.h>
#include <string.h>
#include "kvstore.h"

typedef struct hash_item_t {
    struct hash_item_t *next, *prev;    // Next item in the bucket
    char *key;      // items'key
    char *value;        // items's value
    size_t value_size;  // items's value length
    pthread_rwlock_t  rwlock; // item lock
} hash_item_t;

typedef struct {
    unsigned int capacity;  // Number of buckets
    hash_item_t **items;    // Hashtable buckets as single linked list
    pthread_mutex_t *locks; // Linked list lock
} hashtable_t;

extern hashtable_t *ht;

int init_hashtable(size_t capacity);

unsigned int hash(char *str);

unsigned int get_bucket(char *key);

hash_item_t *search(char *key, size_t key_len);

int remove_item(char *key, size_t key_len);

hash_item_t *get_key_entry(char *key, size_t key_len);


#endif //RDMA_KV_STORE_HASH_H
