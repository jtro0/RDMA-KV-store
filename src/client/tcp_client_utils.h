//
// Created by Jens on 28/05/2021.
//

#ifndef RDMA_KV_STORE_TCP_CLIENT_UTILS_H
#define RDMA_KV_STORE_TCP_CLIENT_UTILS_H

#include "common.h"
#include <netinet/in.h>


struct tcp_server_conn {
    int socket;
    struct sockaddr_in *server_sockaddr;
    struct request *request;
    struct response *response;
};

int tcp_client_prepare_connection(struct tcp_server_conn *server_conn);
int tcp_client_connect_to_server(struct tcp_server_conn *server_conn);
int tcp_send_request(struct tcp_server_conn *server_conn, struct request *request);
int tcp_receive_response(struct tcp_server_conn *server_conn, struct response *response);
int tcp_client_disconnect_and_clean(struct tcp_server_conn *server_conn);

#endif //RDMA_KV_STORE_TCP_CLIENT_UTILS_H
