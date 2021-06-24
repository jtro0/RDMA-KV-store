//
// Created by jtroo on 23-04-21.
//

#ifndef RDMA_KV_STORE_RC_CLIENT_UTILS_H
#define RDMA_KV_STORE_RC_CLIENT_UTILS_H

#include <netinet/in.h>

#include "common.h"
#include "rdma_common.h"

struct rc_server_conn {
    /* These are basic RDMA resources */
/* These are RDMA connection related resources */
    struct rdma_event_channel *cm_event_channel;
    struct rdma_cm_id *cm_client_id;
    struct ibv_pd *pd;
    struct ibv_comp_channel *io_completion_channel;
    struct ibv_cq *client_cq;
    struct ibv_qp_init_attr qp_init_attr;
    struct ibv_qp *client_qp;
/* These are memory buffers related resources */
    struct ibv_mr *client_request_mr, *client_response_mr;
    struct request *request;
    struct response *response, *expected_response;

    struct sockaddr_in *server_sockaddr;
};

int rc_main(char *key, struct sockaddr_in *server_sockaddr);
int client_prepare_connection(struct rc_server_conn *server_conn);
int client_connect_to_server(struct rc_server_conn *server_conn);
int rc_send_request(struct rc_server_conn *server_conn, struct request *request, int blocking);
int rc_pre_post_receive_response(struct rc_server_conn *server_conn, struct response *response);
int rc_receive_response(struct rc_server_conn *server_conn, struct response *response, int blocking);
int client_disconnect_and_clean(struct rc_server_conn *server_conn);
#endif //RDMA_KV_STORE_RC_CLIENT_UTILS_H
