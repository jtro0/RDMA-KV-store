//
// Created by jtroo on 27-04-21.
//
#include <netinet/in.h>
#include <sys/time.h>
#include <pthread.h>

#include "instance.h"
#include "client/client.h"
#include "client/rc_client_utils.h"

void make_get_request(struct request *request, int count) {
    request->method = GET;
    sprintf(request->key, "Key from instance %d count %d", 0, count);
    request->key_len = strlen(request->key);
    request->msg_len = 0;
}

void make_set_request(struct request *request, int count) {
    request->method = SET;
    sprintf(request->key, "Key from instance %d count %d", 0, count);
    request->key_len = strlen(request->key);
    sprintf(request->msg, "Message from instance %d count %d", 0, count);
    request->msg_len = strlen(request->msg);
}

void make_expected_response(struct response *response, struct request *made_request, int count) {
    switch (made_request->method) {
        case UNK:
            break;
        case SET:
            response->code = OK;
            break;
        case GET:
            response->code = OK;
            sprintf(response->msg, "Message from instance %d count %d", 0, count-1);
            response->msg_len = strlen(response->msg);
            break;
        case DEL:
            break;
        case PING:
            break;
        case DUMP:
            break;
        case RST:
            break;
        case EXIT:
            break;
        case SETOPT:
            break;
    }
}

void* start_instance(void *arguments) {
    struct thread_args *args = arguments;
    enum connection_type conn_t = args->conn_t;
    struct sockaddr_in *server_addr = args->server_addr;
    unsigned int num_ops = args->num_ops;
//    struct operation *ops = args->ops;

    int returned;
    struct client_to_server_conn conn;
    conn.conn_t = conn_t;
    conn.server_addr = server_addr;


    switch (conn_t) {
        case TCP:
            break;
        case RC:
            conn.rc_server_conn = malloc(sizeof(struct rc_server_conn));
            conn.rc_server_conn->server_sockaddr = server_addr;
            conn.rc_server_conn->request = calloc(1, sizeof(struct request));
            conn.rc_server_conn->response = calloc(1, sizeof(struct response));
            conn.rc_server_conn->expected_response = calloc(1, sizeof(struct response));
            returned = client_prepare_connection(conn.rc_server_conn);
            check(returned, NULL, "Failed to setup client connection , returned = %d \n", returned);
            pr_debug("prepared\n");
            returned = client_connect_to_server(conn.rc_server_conn);
            check(returned, NULL, "Failed to setup client connection , returned = %d \n", returned);
            break;
        case UC:
            break;
        case UD:
            break;
    }
    sleep(1);
    struct operation **ops = calloc(num_ops, sizeof(struct operation*));

    int count = 0;
    ops[count] = malloc(sizeof(struct operation));
    ops[count]->start = malloc(sizeof(struct timeval));
    ops[count]->end = malloc(sizeof(struct timeval));

    gettimeofday(ops[count]->start, NULL);
    do {
//        ops[count] = malloc(sizeof(struct operation));
//        struct operation *current = ops[count];
//        current->start = malloc(sizeof(struct timeval));
//        current->end = malloc(sizeof(struct timeval));
//
//        gettimeofday(current->start, NULL);

//        current->request = calloc(1, sizeof(struct request));
//        current->response = calloc(1, sizeof(struct response));
        struct timeval start, end;
        gettimeofday(&start, NULL);

        if (count == 0) {
            make_set_request(conn.rc_server_conn->request, count);
        }
        else {
            make_get_request(conn.rc_server_conn->request, 0);
        }


//        make_expected_response(conn.rc_server_conn->expected_response, conn.rc_server_conn->request, count);
//        returned = rc_pre_post_receive_response(conn.rc_server_conn, current->response);
//        check(returned, ops, "Failed to receive response, returned = %d \n", returned);

        returned = send_request(&conn, conn.rc_server_conn->request);
        check(returned, ops, "Failed to get send request, returned = %d \n", returned);

        returned = receive_response(&conn, conn.rc_server_conn->response);

//        gettimeofday(current->end, NULL);

//        if (memcmp(conn.rc_server_conn->response, conn.rc_server_conn->expected_response, sizeof(struct response)) != 0) {
//            print_response(conn.rc_server_conn->response);
//            print_response(conn.rc_server_conn->expected_response);
//            return NULL;
//        }

//        usleep(250);
//        sleep(1);
        count++;
        bzero(conn.rc_server_conn->expected_response, sizeof(struct response));

        gettimeofday(&end, NULL);

        double time_taken_usec = ((end.tv_sec - start.tv_sec)*1000000.0+end.tv_usec) - start.tv_usec;
        printf("Time in microseconds: %f microseconds\n", time_taken_usec);
        struct timeval *time_taken = malloc(sizeof(struct timeval));
        timersub(&end, &start, time_taken);

        double time_taken_sec = time_taken->tv_sec + time_taken->tv_usec/1000000.0;
        printf("Time taken in seconds: %f seconds\n", time_taken_sec);

    } while (count < num_ops);
    gettimeofday(ops[0]->end, NULL);

    printf("what the pointer should be %p\n", ops);
    pthread_exit((void*)ops);
}