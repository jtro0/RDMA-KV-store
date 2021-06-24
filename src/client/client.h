//
// Created by jtroo on 23-04-21.
//

#ifndef RDMA_KV_STORE_CLIENT_H
#define RDMA_KV_STORE_CLIENT_H

#include "common.h"
#include "rc_client_utils.h"
#include "uc_client_utils.h"
#include "ud_client_utils.h"
#include "tcp_client_utils.h"

struct client_to_server_conn {
    enum connection_type conn_t;
    struct sockaddr_in *server_addr;
    struct request *request;
    struct response *response;
    int blocking;

    struct rc_server_conn *rc_server_conn;
    struct ud_server_conn *ud_server_conn;
    struct uc_server_conn *uc_server_conn;
    struct tcp_server_conn *tcp_server_conn;
    int instance_nr;
};

int send_request(struct client_to_server_conn *conn);
int receive_response(struct client_to_server_conn *conn);


#endif //RDMA_KV_STORE_CLIENT_H
