//
// Created by jtroo on 23-04-21.
//
#include "client.h"

int send_request(struct client_to_server_conn *conn) {
    int ret = -1;
    switch (conn->conn_t) {
        case TCP:
            ret = tcp_send_request(conn->tcp_server_conn, conn->tcp_server_conn->request);
            if (ret > 0)
                ret = 0;
            break;
        case RC:
            ret = rc_send_request(conn->rc_server_conn, conn->rc_server_conn->request);
            break;
        case UC:
            ret = something_send_request(conn->uc_server_conn, conn->uc_server_conn->request);
            break;
        case UD:
            ret = ud_send_request(conn->ud_server_conn, conn->ud_server_conn->request);
            break;
    }
    return ret;
}

int receive_response(struct client_to_server_conn *conn) {
    int ret = -1;
    switch (conn->conn_t) {
        case TCP:
            ret = tcp_receive_response(conn->tcp_server_conn, conn->tcp_server_conn->response);
            break;
        case RC:
            ret = rc_receive_response(conn->rc_server_conn, conn->rc_server_conn->response);
            break;
        case UC:
            ret = something_receive_response(conn->uc_server_conn, conn->uc_server_conn->response);
            break;
        case UD:
            ret = ud_receive_response(conn->ud_server_conn, conn->ud_server_conn->response);
            break;
    }
    return ret;
}