#include <semaphore.h>
#include <pthread.h>

#include "server_utils.h"
#include "common.h"
#include "request_dispatcher.h"
#include "hash.h"
#include "kvstore.h"

#define HT_CAPACITY 256

hashtable_t *ht;

int set_request(int socket, struct request *request) {
    size_t len = 0;
    size_t expected_len = request->msg_len;

    pr_debug("Setting %s with expected length %zu\n", request->key, expected_len - len);

    hash_item_t *item = get_key_entry(request->key, request->key_len);

    if (pthread_rwlock_trywrlock(&item->rwlock) != 0) {
        pr_debug("this one\n");
        char *trash = malloc(expected_len);
        read_payload(socket, request, expected_len, trash);
        check_payload(socket, request, expected_len);
        send_response(socket, KEY_ERROR, 0, NULL);
        free(trash);

        return -1;
    }
    item->value = calloc(1, expected_len); // maybe +1 for \0

    read_payload(socket, request, expected_len-len, item->value+len);

    if (request->connection_close || check_payload(socket, request, expected_len) < 0) {
        pthread_rwlock_unlock(&item->rwlock);
        remove_item(request->key, request->key_len);
        return -1;
    }

    item->value_size = expected_len;
    send_response(socket, OK, 0, NULL);
    pr_debug("Everything is good, sent response\n");
    pthread_rwlock_unlock(&item->rwlock);

    return 0;
}

void get_request(int fd, struct request *pRequest) {
    hash_item_t *item = search(pRequest->key, pRequest->key_len);
    if (item == NULL) {
        send_response(fd, KEY_ERROR, 0, NULL);
        return;
    }
    if (pthread_rwlock_tryrdlock(&item->rwlock) != 0) {
        send_response(fd, KEY_ERROR, 0, NULL);
        return;
    }
    send_response(fd, OK, item->value_size, item->value);
    pthread_rwlock_unlock(&item->rwlock);
}

void del_request(int fd, struct request *pRequest) {
    if (remove_item(pRequest->key, pRequest->key_len) < 0) {
        send_response(fd, KEY_ERROR, 0, NULL);
        return;
    }

    send_response(fd, OK, 0, NULL);
}

void *main_job(void *arg) {
    int method;
    struct conn_info *conn_info = arg;
    struct request *request = allocate_request();
    request->connection_close = 0;

    pr_info("Starting new session from %s:%d\n",
            inet_ntoa(conn_info->addr.sin_addr),
            ntohs(conn_info->addr.sin_port));

    do {
        method = recv_request(conn_info->socket_fd, request);
        switch (method) {
            case SET:
                set_request(conn_info->socket_fd, request);
                break;
            case GET:
                get_request(conn_info->socket_fd, request);
                break;
            case DEL:
                del_request(conn_info->socket_fd, request);
                break;
            case RST:
                init_hashtable(HT_CAPACITY);
                send_response(conn_info->socket_fd, OK, 0, NULL);
                break;
        }

        if (request->key) {
        }

    } while (!request->connection_close);

    close_connection(conn_info->socket_fd);
    free(request);
    free(conn_info);
    return (void *) NULL;
}

int main(int argc, char *argv[]) {
    int listen_sock, init;

    listen_sock = server_init(argc, argv);

    pr_debug("Initializing table\n");
    if ((init = init_hashtable(HT_CAPACITY)) < 0) {
        pr_debug("Failed to initialize ");
        if (init == -1) {
            pr_debug("at hashtable alloc\n");
            exit(EXIT_FAILURE);
        }
        else if (init == -2) {
            pr_debug("at items alloc\n");
            exit(EXIT_FAILURE);
        }
        if (init == -3) {
            pr_debug("at locks alloc\n");
            exit(EXIT_FAILURE);
        }
    }

    for (;;) {
        struct conn_info *conn_info =
                calloc(1, sizeof(struct conn_info));
        if (accept_new_connection(listen_sock, conn_info) < 0) {
            error("Cannot accept new connection");
            free(conn_info);
            continue;
        }
        pthread_t thread_id;
        printf("Before Thread\n");
        pthread_create(&thread_id, NULL, main_job, conn_info);
//        main_job(conn_info);
    }

    return 0;
}
