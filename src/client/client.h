//
// Created by jtroo on 23-04-21.
//

#ifndef RDMA_KV_STORE_CLIENT_H
#define RDMA_KV_STORE_CLIENT_H

#include "common.h"
#include "rc_client_utils.h"

struct client_to_server_conn {
    enum connection_type conn_t;
    struct sockaddr_in *server_addr;
    struct request *request;
    struct response *response;

    struct rc_server_conn *rc_server_conn;
};

int send_request(struct client_to_server_conn *conn, struct request *request);
int receive_response(struct client_to_server_conn *conn, struct response *response);


#endif //RDMA_KV_STORE_CLIENT_H
