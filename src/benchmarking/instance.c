//
// Created by jtroo on 27-04-21.
//
#include <netinet/in.h>
#include <sys/time.h>
#include <pthread.h>

#include "instance.h"
#include "client/client.h"
#include "client/rc_client_utils.h"
#include "client/ud_client_utils.h"

void make_get_request(struct client_to_server_conn *conn, int count) {
    struct request *request = NULL;
    switch (conn->conn_t) {
        case TCP:
            break;
        case RC:
            request = conn->rc_server_conn->request;
            break;
        case UC:
            break;
        case UD:
            request = conn->ud_server_conn->request;
            break;
    }

    request->method = GET;
    sprintf(request->key, "Key from instance %d count %d", conn->instance_nr, count);
    request->key_len = strlen(request->key);
    request->msg_len = 0;
    request->client_id = conn->instance_nr;
}

void make_set_request(struct client_to_server_conn *conn, int count) {
    struct request *request = NULL;
    switch (conn->conn_t) {
        case TCP:
            break;
        case RC:
            request = conn->rc_server_conn->request;
            break;
        case UC:
            break;
        case UD:
            request = conn->ud_server_conn->request;
            break;
    }

    request->method = SET;
    sprintf(request->key, "Key from instance %d count %d", conn->instance_nr, count);
    request->key_len = strlen(request->key);
    sprintf(request->msg, "Message from instance %d count %d", conn->instance_nr, count);
    request->msg_len = strlen(request->msg);
    request->client_id = conn->instance_nr;
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
    unsigned int instance_nr = args->instance_nr;

    int returned;
    struct client_to_server_conn conn;
    conn.conn_t = conn_t;
    conn.server_addr = server_addr;
    conn.instance_nr = instance_nr;

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
            conn.ud_server_conn = malloc(sizeof(struct ud_server_conn));
            conn.ud_server_conn->server_sockaddr = server_addr;
            conn.ud_server_conn->request = calloc(1, sizeof(struct request));
            conn.ud_server_conn->response = calloc(1, sizeof(struct ud_response));
            conn.ud_server_conn->expected_response = calloc(1, sizeof(struct ud_response));
            returned = ud_prepare_client(conn.ud_server_conn);
            check(returned, NULL, "Failed to setup client connection , returned = %d \n", returned);
            pr_debug("prepared\n");
            returned = ud_client_connect_to_server(conn.ud_server_conn);
            check(returned, NULL, "Failed to setup client connection , returned = %d \n", returned);
            break;
    }
    struct operation *ops = calloc(num_ops, sizeof(struct operation));

    usleep(1000);

    int count = 0;

    do {
        if (count == 0) {
            make_set_request(&conn, count);
        }
        else {
            make_get_request(&conn, 0);
        }
        ops[count].start = malloc(sizeof(struct timeval));
        ops[count].end = malloc(sizeof(struct timeval));

        gettimeofday(ops[count].start, NULL);

        returned = send_request(&conn);
        check(returned, ops, "Failed to get send request, returned = %d \n", returned);

        returned = receive_response(&conn);
        check(returned, ops, "Failed to get receive response, returned = %d \n", returned);

        gettimeofday(ops[count].end, NULL);

//        usleep(1);
        count++;
//        bzero(conn.rc_server_conn->expected_response, sizeof(struct response));
    } while (count < num_ops);

    pthread_exit((void*)ops);
}
