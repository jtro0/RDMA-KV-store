//
// Created by jtroo on 23-04-21.
//

#ifndef RDMA_KV_STORE_UC_CLIENT_UTILS_H
#define RDMA_KV_STORE_UC_CLIENT_UTILS_H

#include <netinet/in.h>

#include "common.h"
#include "rdma_common.h"

struct uc_server_conn {
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

int uc_main(char *key, struct sockaddr_in *server_sockaddr);
int uc_client_prepare_connection(struct uc_server_conn *server_conn);
int uc_client_connect_to_server(struct uc_server_conn *server_conn);
int uc_send_request(struct uc_server_conn *server_conn, struct request *request);
int uc_pre_post_receive_response(struct uc_server_conn *server_conn, struct response *response);
int uc_receive_response(struct uc_server_conn *server_conn, struct response *response);
int uc_client_disconnect_and_clean(struct uc_server_conn *server_conn);
#endif //RDMA_KV_STORE_UC_CLIENT_UTILS_H
