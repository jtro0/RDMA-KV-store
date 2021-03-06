//
// Created by jtroo on 27-04-21.
//
#include <netinet/in.h>
#include <sys/time.h>
#include <pthread.h>

#include "instance.h"
#include "client/client.h"

void make_get_request(struct client_to_server_conn *conn, int count) {
    struct request *request = NULL;
    switch (conn->conn_t) {
        case TCP:
            request = conn->tcp_server_conn->request;
            break;
        case RC:
            request = conn->rc_server_conn->request;
            break;
        case UC:
            request = conn->uc_server_conn->request;
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
            request = conn->tcp_server_conn->request;
            break;
        case RC:
            request = conn->rc_server_conn->request;
            break;
        case UC:
            request = conn->uc_server_conn->request;
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

/*
 * Used to validate response
 */
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
        case PING:
        case RST:
        case EXIT:
        case SETOPT:
            response->code = OK;
            break;
    }
}

/*
 * Client thread
 */
void* start_instance(void *arguments) {
    struct thread_args *args = arguments;
    enum connection_type conn_t = args->conn_t;
    struct sockaddr_in *server_addr = args->server_addr;
    unsigned int num_ops = args->num_ops; // Number of operations needed to perform
    int instance_nr = args->instance_nr;
    int blocking = args->blocking;

    int returned;
    struct client_to_server_conn conn;
    conn.conn_t = conn_t;
    conn.server_addr = server_addr;
    conn.instance_nr = instance_nr;
    conn.blocking = blocking;

    // Ugly but it works
    switch (conn_t) {
        case TCP:
            conn.tcp_server_conn = malloc(sizeof(struct tcp_server_conn));
            conn.tcp_server_conn->server_sockaddr = server_addr;
            returned = tcp_client_prepare_connection(conn.tcp_server_conn);
            returned = tcp_client_connect_to_server(conn.tcp_server_conn);
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
            conn.uc_server_conn = malloc(sizeof(struct uc_server_conn));
            conn.uc_server_conn->server_sockaddr = server_addr;
            conn.uc_server_conn->request = calloc(1, sizeof(struct request));
            conn.uc_server_conn->response = calloc(1, sizeof(struct response));
            conn.uc_server_conn->expected_response = calloc(1, sizeof(struct response));
            returned = uc_client_prepare_connection(conn.uc_server_conn);
            check(returned, NULL, "Failed to setup client connection , returned = %d \n", returned);
            returned = uc_client_connect_to_server(conn.uc_server_conn);
            check(returned, NULL, "Failed to setup client connection , returned = %d \n", returned);
            break;
        case UD:
            conn.ud_server_conn = malloc(sizeof(struct ud_server_conn));
            conn.ud_server_conn->server_sockaddr = server_addr;
            conn.ud_server_conn->request = calloc(1, sizeof(struct request));
            conn.ud_server_conn->response = calloc(1, sizeof(struct ud_response));
            conn.ud_server_conn->expected_response = calloc(1, sizeof(struct ud_response));
            conn.ud_server_conn->client_id = instance_nr;
            returned = ud_prepare_client(conn.ud_server_conn);
            check(returned, NULL, "Failed to setup client connection , returned = %d \n", returned);
            returned = ud_client_connect_to_server(conn.ud_server_conn);
            check(returned, NULL, "Failed to setup client connection , returned = %d \n", returned);
            break;
    }
    struct operation *ops = calloc(num_ops, sizeof(struct operation)); // Array of operations, used to store time

    sleep(1); // Wait until server is fully ready

    int count = 0;

    do {
        // 5% chance of a SET request
        if (rand() % 100 <= 5) {
            make_set_request(&conn, count);
        }
        else { // 95% chance of GET request
            make_get_request(&conn, (rand() % (count+1))-1);
        }
        ops[count].start = malloc(sizeof(struct timeval));
        ops[count].end = malloc(sizeof(struct timeval));

        gettimeofday(ops[count].start, NULL); // Start timer of current operation

        returned = 0;
        while (returned == 0) { // Loop until response is received
            returned = send_request(&conn);
            check(returned, ops, "Failed to get send request, returned = %d \n", returned);

            returned = receive_response(&conn);
            check(returned < 0, ops, "Failed to get receive response, returned = %d \n", returned);
        }

        gettimeofday(ops[count].end, NULL); // Received response, can stop the timer!

        count++;
    } while (count < num_ops);

    pthread_exit((void*)ops); // Return the array of timers
}
