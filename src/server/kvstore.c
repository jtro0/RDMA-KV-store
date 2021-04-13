#include <semaphore.h>
#include <assert.h>
#include <pthread.h>

#include "common.h"
#include "hash.h"
#include "kvstore.h"

#define HT_CAPACITY 256

static hashtable_t *ht;

void *main_job(void *arg) {
    int method;
    struct sockaddr_in *conn_info = arg;
    struct request *request;
    request->connection_close = 0;

    do {
        switch (method) {
            case SET:
                break;
            case GET:
                break;
            case DEL:
                break;
            case RST:
                break;
        }

        if (request->key) {
        }

    } while (!request->connection_close);

    free(request);
    free(conn_info);
    return (void *) NULL;
}

int main(int argc, char *argv[]) {
    // Initialize hashtable.
    pr_debug("Initializing table\n");
    ht = calloc(1, sizeof(hashtable_t));
    ht->items = calloc(HT_CAPACITY, sizeof(hash_item_t));
    ht->capacity = HT_CAPACITY;

    return 0;
}
