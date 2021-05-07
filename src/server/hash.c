#include "hash.h"
#include <pthread.h>

int init_hashtable(size_t capacity) {
    if (ht != NULL) {
        free(ht->items);
        free(ht->locks);
        ht->capacity = 0;
        free(ht);
    }

    ht = calloc(1, sizeof(hashtable_t));
    if (ht == NULL && capacity != 0)
        return -1;
    ht->items = calloc(capacity, sizeof(hash_item_t));
    if (ht->items == NULL && capacity != 0) {
        free(ht);
        return -2;
    }
    ht->capacity = capacity;
    ht->locks = calloc(capacity, sizeof(pthread_mutex_t));
    if (ht->locks == NULL && capacity != 0) {
        free(ht->items);
        ht->capacity = 0;
        free(ht);
        return -3;
    }
    return 0;
}

/*
 * Hash function by
 * http://www.cse.yorku.ca/~oz/hash.html
 */
unsigned int hash(char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++) != 0)
        hash = ((hash << 5) + hash) + c;

    return hash;
}

unsigned int get_bucket(char *key) {
    unsigned int hashed = hash(key);
    return hashed % ht->capacity;
}

hash_item_t *search(char *key, size_t key_len) {
    unsigned int bucket = get_bucket(key);
//    pthread_mutex_lock(&ht->user->locks[bucket]);
    hash_item_t *current = ht->items[bucket];

    while (current != NULL) {
        if (strncmp(current->key, key, key_len) == 0) {
            break;
        }
        current = current->next;
    }
//    pthread_mutex_unlock(&ht->user->locks[bucket]);
    return current;
}

int remove_item(char *key, size_t key_len) {
    unsigned int bucket = get_bucket(key);
    pthread_mutex_lock(&ht->locks[bucket]);

    hash_item_t *item = search(key, key_len);
    if (!item) {
        pthread_mutex_unlock(&ht->locks[bucket]);
        return -1;
    }
    if (pthread_rwlock_trywrlock(&item->rwlock) != 0) {
        pthread_rwlock_unlock(&item->rwlock);
        pthread_mutex_unlock(&ht->locks[bucket]);
        return -1;
    }

    free(item->value);
    item->value = NULL;
    item->value_size = 0;
    free(item->key);
    item->key = NULL;
    if (item->next)
        item->next->prev = item->prev;
    if (item->prev)
        item->prev->next = item->next;
    if (item == ht->items[bucket]) {
        ht->items[bucket] = item->next;
    }
    //    pthread_rwlock_unlock(&item->user->rwlock);
    pthread_rwlock_destroy(&item->rwlock);
    free(item);
    pthread_mutex_unlock(&ht->locks[bucket]);
    return 0;
}

hash_item_t *get_key_entry(char *key, size_t key_len) {
    pr_debug("getting key entry\n");
    unsigned int bucket = get_bucket(key);
    pr_debug("got bucket %d\n", bucket);

    pthread_mutex_lock(&ht->locks[bucket]);
    pr_debug("got lock\n");

    hash_item_t *item_head = ht->items[bucket];
    hash_item_t *entry = search(key, key_len);
    pr_debug("found entry\n");

    if (entry == NULL) {
        entry = calloc(1, sizeof(hash_item_t));
        entry->key = malloc(key_len + 1); // maybe freed?
        memcpy(entry->key, key, key_len);
        entry->key[key_len] = '\0';
//        entry->user = calloc(1, sizeof(struct user_item));
        entry->prev = NULL;
        entry->next = NULL;
        entry->value = NULL;

        if (item_head != NULL) {
            item_head->prev = entry;
            entry->next = item_head;
        }
        ht->items[bucket] = entry;
    }
    pthread_mutex_unlock(&ht->locks[bucket]);
    pr_debug("unlocking\n");

    return entry;
}