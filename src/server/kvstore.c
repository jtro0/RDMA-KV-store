#include <semaphore.h>
#include <pthread.h>
#include <sys/time.h>

#include "server_utils.h"
#include "common.h"
#include "request_dispatcher.h"
#include "hash.h"
#include "kvstore.h"

#define HT_CAPACITY 1024

hashtable_t *ht;

int set_request(struct client_info *client, struct request *request, struct response *response) {
    size_t len = 0;
    size_t expected_len = request->msg_len;

    pr_debug("Setting %s with expected length %zu\n", request->key, expected_len - len);

    hash_item_t *item = get_key_entry(request->key, request->key_len);

    if (pthread_rwlock_trywrlock(&item->rwlock) != 0) {
//        char *trash = malloc(expected_len);
//        read_payload(client, request, expected_len, trash);
//        check_payload(client->tcp_client->socket_fd, request, expected_len);
        response->code = KEY_ERROR;
//        free(trash);

        return -1;
    }
    item->value = malloc(MSG_SIZE); // maybe +1 for \0

    if (client->is_test) {
        read_payload(client, request, expected_len - len, item->value + len);

        if (request->connection_close || check_payload(client->tcp_client->socket_fd, request, expected_len) < 0) {
            pthread_rwlock_unlock(&item->rwlock);
            remove_item(request->key, request->key_len);
            return -1;
        }
    } else {
        memcpy(item->value, request->msg, request->msg_len);

        item->value_size = expected_len;
        response->code = OK;
        pr_debug("Everything is good, sent response\n");
        pthread_rwlock_unlock(&item->rwlock);
        pr_debug("pthread\n");

    }
    return 0;
}

void get_request(struct client_info *client, struct request *pRequest, struct response *response) {
    hash_item_t *item = search(pRequest->key, pRequest->key_len);
    if (item == NULL) {
        response->code = KEY_ERROR;
        return;
    }
    if (pthread_rwlock_tryrdlock(&item->rwlock) != 0) {
        response->code = KEY_ERROR;
        return;
    }
    response->code = OK;
    memcpy(response->msg, item->value, item->value_size);
    response->msg_len = item->value_size;

    pthread_rwlock_unlock(&item->rwlock);
}

void del_request(struct client_info *client, struct request *pRequest, struct response *response) {
    if (remove_item(pRequest->key, pRequest->key_len) < 0) {
        response->code = KEY_ERROR;
        return;
    }
    response->code = OK;
}

void *main_job(void *arg) {
    int method;
    struct client_info *client = arg;
    client->request->connection_close = 0;
//    pr_info("Starting new session from %s:%d\n",
//            inet_ntoa(client->addr.sin_addr),
//            ntohs(client->addr.sin_port));
    do {
        prepare_for_next_request(client);
        struct request *request = recv_request(client);
        if (request == NULL) {
            return 0;
        }
        if (request->method != UNK)
            ready_for_next_request(client);

        bzero(client->response, sizeof(struct response));
        if (client->type == UD)
            pr_info("client %d: request count %d\n", client->client_nr, client->ud_client->ud_server->request_count);
        else
            pr_info("client %d: request count %d\n", client->client_nr, client->request_count);

        switch (request->method) {
            case SET:
                pr_info("set\n");
                set_request(client, request, client->response);
                break;
            case GET:
                pr_info("get\n");
                get_request(client, request, client->response);
                break;
            case DEL:
                pr_info("del\n");
                del_request(client, request, client->response);
                break;
            case RST:
                pr_info("rst\n");
                init_hashtable(HT_CAPACITY);
                client->response->code = OK;
                break;
        }

        send_response_to_client(client);
    } while (!is_connection_closed(client));

    close_connection(client->tcp_client->socket_fd);
    free(client->request);
    free(client);
    return (void *) NULL;
}

int main(int argc, char *argv[]) {
    int init;

    struct server_info *server_connection = server_init(argc, argv);

    pr_debug("Initializing table\n");
    if ((init = init_hashtable(HT_CAPACITY)) < 0) {
        pr_debug("Failed to initialize ");
        if (init == -1) {
            pr_debug("at hashtable alloc\n");
            exit(EXIT_FAILURE);
        } else if (init == -2) {
            pr_debug("at items alloc\n");
            exit(EXIT_FAILURE);
        }
        if (init == -3) {
            pr_debug("at locks alloc\n");
            exit(EXIT_FAILURE);
        }
    }

    int client_nr = 0;
    for (;;) {
//    struct client_info *new_client =
//            calloc(1, sizeof(struct client_info));
        pr_info("New client\n");
        struct client_info *client = malloc(sizeof(struct client_info));
//        client = malloc(sizeof(struct client_info));
        // TODO: put this in connection specific (UD doesnt need)
        client->request = calloc(REQUEST_BACKLOG, sizeof(struct request));
        client->response = malloc(sizeof(struct response));
        client->request_count = 0;
        client->type = server_connection->type;
        client->is_test = server_connection->is_test;
        client->client_nr = client_nr;
        client_nr++;
        pr_info("Accepting new connection\n");
        if (accept_new_connection(server_connection, client) < 0) {
            pr_info("no new connection");
//            exit(EXIT_FAILURE);
            continue;
//            return 0;
        }
        pthread_t thread_id;
        printf("Before Thread\n");
        pthread_create(&thread_id, NULL, main_job, client);
//        main_job(client);
    }
//    void *ret;
//    if (pthread_join(thread_id, &ret) != 0) {
//        pr_info("pthread join failed");
//    }

    return 0;
}
