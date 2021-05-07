//
// Created by jtroo on 23-04-21.
//
#include "client.h"

int send_request(struct client_to_server_conn *conn, struct request *request) {
    int ret = -1;
    switch (conn->conn_t) {
        case TCP:
            break;
        case RC:
            ret = rc_send_request(conn->rc_server_conn, request);
            break;
        case UC:
            break;
        case UD:
            break;
    }
    return ret;
}

int receive_response(struct client_to_server_conn *conn, struct response *response) {
    int ret = -1;
    switch (conn->conn_t) {
        case TCP:
            break;
        case RC:
            ret = rc_receive_response(conn->rc_server_conn, response);
            break;
        case UC:
            break;
        case UD:
            break;
    }
    return ret;
}