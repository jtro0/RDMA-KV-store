//
// Created by jtroo on 21-05-21.
//

#ifndef RDMA_KV_STORE_UD_CLIENT_UTILS_H
#define RDMA_KV_STORE_UD_CLIENT_UTILS_H

#include <netinet/in.h>

#include "common.h"
#include "rdma_common.h"

struct ud_server_conn {
    /* These are basic RDMA resources */
/* These are RDMA connection related resources */
    struct rdma_event_channel *cm_event_channel;
    /* These are memory buffers related resources */
    struct sockaddr_in *server_sockaddr;
    struct ibv_qp *ud_qp;
    struct ibv_cq *ud_cq;
    union ibv_gid server_gid;
    struct qp_attr local_dgram_qp_attrs;	// Local and remote queue pair attributes
    struct qp_attr remote_dgram_qp_attrs;
    struct ibv_pd *pd;
    struct ibv_comp_channel *io_completion_channel; // maybe?
    struct ibv_qp_init_attr qp_init_attr; // maybe?
    struct rdma_cm_id *cm_client_id; // maybe
    struct ibv_mr *client_request_mr, *client_response_mr;
    struct request *request;
    struct ud_response *response, *expected_response;
    int socket_fd;
    int client_counter;
    int request_count;
    struct ibv_ah *ah;
    int client_id;
};

int ud_main(char *key, struct sockaddr_in *server_sockaddr);
int ud_prepare_client(struct ud_server_conn *server_conn);
int ud_client_connect_to_server(struct ud_server_conn *server_conn);
int ud_send_request(struct ud_server_conn *server_conn, struct request *request, int blocking);
int ud_pre_post_receive_response(struct ud_server_conn *server_conn, struct ud_response *response);
int ud_receive_response(struct ud_server_conn *server_conn, struct ud_response *response, int blocking);
int ud_client_disconnect_and_clean(struct ud_server_conn *server_conn);
#endif //RDMA_KV_STORE_UD_CLIENT_UTILS_H
