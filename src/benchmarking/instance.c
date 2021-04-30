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
            returned = client_prepare_connection(conn.rc_server_conn);
            check(returned, NULL, "Failed to setup client connection , returned = %d \n", returned);

            returned = client_connect_to_server(conn.rc_server_conn);
            check(returned, NULL, "Failed to setup client connection , returned = %d \n", returned);
            break;
        case UC:
            break;
        case UD:
            break;
    }

    struct operation *ops = calloc(num_ops, sizeof(struct operation));
    int count = 0;
    do {
        pr_info("On count %d\n", count);
        struct operation current = ops[count];
        gettimeofday(current.start, NULL);

        current.request = calloc(1, sizeof(struct request));
        current.response = calloc(1, sizeof(struct response));
        current.expected_response = calloc(1, sizeof(struct response));

        if (count % 2 == 0) {
            make_set_request(current.request, count);
        }
        else {
            make_get_request(current.request, count-1);
        }
        make_expected_response(current.expected_response, current.request, count);
        returned = rc_pre_post_receive_response(conn.rc_server_conn, current.response);
        check(returned, ops, "Failed to receive response, returned = %d \n", returned);

        returned = send_request(&conn, current.request);
        check(returned, ops, "Failed to get send request, returned = %d \n", returned);

        returned = receive_response(&conn, current.response);

//        pr_info("getting time\n");
        gettimeofday(current.end, NULL);
        pr_info("got time\n");
        pr_info("doing memcmp\n");

        if (memcmp(current.response, current.expected_response, sizeof(struct response)) != 0) {
            pr_info("not equal, got:\n");
            print_response(current.response);
            pr_info("expected:\n");
            print_response(current.expected_response);
            return NULL;
        }
        pr_info("equal\n");

        sleep(1);
        count++;
//        pr_info("next\n");

    } while (count < num_ops);
    return ops;
}