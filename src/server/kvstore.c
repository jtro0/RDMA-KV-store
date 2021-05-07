#include <semaphore.h>
#include <pthread.h>
#include <sys/time.h>

#include "server_utils.h"
#include "common.h"
#include "request_dispatcher.h"
#include "hash.h"
#include "kvstore.h"

#define HT_CAPACITY 256

hashtable_t *ht;

int set_request(struct client_info *client, struct request *request, struct response *response) {
    size_t len = 0;
    size_t expected_len = request->msg_len;

    pr_debug("Setting %s with expected length %zu\n", request->key, expected_len - len);

    hash_item_t *item = get_key_entry(request->key, request->key_len);

    if (pthread_rwlock_trywrlock(&item->rwlock) != 0) {
        char *trash = malloc(expected_len);
        read_payload(client, request, expected_len, trash);
        check_payload(client->tcp_client->socket_fd, request, expected_len);
        //send_response(client, KEY_ERROR, 0, NULL);
        response->code = KEY_ERROR;
        free(trash);

        return -1;
    }
    item->value = calloc(1, expected_len); // maybe +1 for \0

    if (client->is_test) {
        read_payload(client, request, expected_len - len, item->value + len);

        if (request->connection_close || check_payload(client->tcp_client->socket_fd, request, expected_len) < 0) {
            pthread_rwlock_unlock(&item->rwlock);
            remove_item(request->key, request->key_len);
            return -1;
        }
    }
    else {
        memcpy(item->value, request->msg, request->msg_len);

        item->value_size = expected_len;
        response->code = OK;
//        send_response(client, OK, 0, NULL);
        pr_debug("Everything is good, sent response\n");
        pthread_rwlock_unlock(&item->rwlock);
    }
    return 0;
}

void get_request(struct client_info *client, struct request *pRequest, struct response *response) {
    hash_item_t *item = search(pRequest->key, pRequest->key_len);
    if (item == NULL) {
//        send_response(client, KEY_ERROR, 0, NULL);
        response->code = KEY_ERROR;
        return;
    }
    if (pthread_rwlock_tryrdlock(&item->rwlock) != 0) {
//        send_response(client, KEY_ERROR, 0, NULL);
        response->code = KEY_ERROR;
        return;
    }
//    send_response(client, OK, item->value_size, item->value);
    response->code = OK;
    memcpy(response->msg, item->value, item->value_size);
    response->msg_len = item->value_size;

    pthread_rwlock_unlock(&item->rwlock);
}

void del_request(struct client_info *client, struct request *pRequest, struct response *response) {
    if (remove_item(pRequest->key, pRequest->key_len) < 0) {
//        send_response(client, KEY_ERROR, 0, NULL);
        response->code = KEY_ERROR;
        return;
    }

//    send_response(client, OK, 0, NULL);
    response->code = OK;
}

void *main_job(void *arg) {
    int method;
    struct client_info *client = arg;
    client->request->connection_close = 0;
//    pr_info("Starting new session from %s:%d\n",
//            inet_ntoa(client->addr.sin_addr),
//            ntohs(client->addr.sin_port));

//    struct timeval start, end;
W
    do {
//        gettimeofday(&start, NULL);

        ready_for_next_request(client);
        struct request* request = recv_request(client);
        client->request_count = (client->request_count+1)%REQUEST_BACKLOG;

//        bzero(client->request, sizeof(struct request));
        bzero(client->response, sizeof(struct response));
        pr_info("request count %d\n", client->request_count);
        switch (request->method) {
            case SET:
                pr_info("set\n");
//                gettimeofday(&start, NULL);
                set_request(client, request, client->response);
//                gettimeofday(&end, NULL);
                break;
            case GET:
                pr_info("get\n");
//                gettimeofday(&start, NULL);
                get_request(client, request, client->response);
//                gettimeofday(&end, NULL);

                break;
            case DEL:
                pr_info("del\n");
                del_request(client, request, client->response);
                break;
            case RST:
                pr_info("rst\n");
                init_hashtable(HT_CAPACITY);
//                send_response(client, OK, 0, NULL);
                client->response->code = OK;
                break;
        }

        send_response_to_client(client);

//        gettimeofday(&end, NULL);
//
//        double time_taken_usec = ((end.tv_sec - start.tv_sec)*1000000.0+end.tv_usec) - start.tv_usec;
//        printf("Time in microseconds: %f microseconds\n", time_taken_usec);
//        struct timeval *time_taken = malloc(sizeof(struct timeval));
//        timersub(&end, &start, time_taken);
//
//        double time_taken_sec = time_taken->tv_sec + time_taken->tv_usec/1000000.0;
//        printf("Time taken in seconds: %f seconds\n", time_taken_sec);
    } while (!client->request->connection_close);

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

//    for (;;) {
//    struct client_info *new_client =
//            calloc(1, sizeof(struct client_info));
    server_connection->client->type = server_connection->type;
    server_connection->client->is_test = server_connection->is_test;
    if (accept_new_connection(server_connection, server_connection->client) < 0) {
//            continue;
        pr_info("no new connection");
        return 0;
    }
    pthread_t thread_id;
    printf("Before Thread\n");
    pthread_create(&thread_id, NULL, main_job, server_connection->client);
//        main_job(server_info);
//    }
    void *ret;
    if (pthread_join(thread_id, &ret) != 0) {
        pr_info("pthread join failed");
    }


    return 0;
}
