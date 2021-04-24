//
// Created by jtroo on 14-04-21.
//

#ifndef RDMA_KV_STORE_SERVER_UTILS_H
#define RDMA_KV_STORE_SERVER_UTILS_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

#include "common.h"

// TODO Remove RDMA stuff here and move them to rdma_common or rc/uc/ud_server_utils
#include "rdma_common.h"

#define BACKLOG     10
#define TIMEOUT     60


struct tcp_conn_info {
    int socket_fd;
};

//struct rc_client_info {
//    struct rdma_cm_id *cm_client_id;
//    struct ibv_qp *client_qp;
//    struct ibv_mr *client_metadata_mr;
//    struct rdma_buffer_attr client_metadata_attr;
//    struct ibv_sge client_recv_sge;
//    struct ibv_recv_wr client_recv_wr;
//    struct ibv_recv_wr *bad_client_recv_wr;
//
//    struct request *request;
//    struct ibv_mr *request_mr, *request_attr_mr;
//    struct rdma_buffer_attr request_attr;
//    struct ibv_sge client_send_sge;
//    struct ibv_send_wr client_send_wr;
//    struct ibv_send_wr *bad_client_send_wr;
//    struct ibv_wc *wc;
//};

struct rc_server_info {
    struct rdma_event_channel *cm_event_channel;
    struct rdma_cm_id *cm_server_id;
    struct ibv_pd *pd;
    struct ibv_comp_channel *io_completion_channel;
    struct ibv_cq *cq;
//    struct rc_client_info *clientInfo;

//    struct ibv_wc *wcs;
    struct rdma_cm_id *cm_client_id;
    struct ibv_qp *client_qp;
//    struct ibv_mr *client_metadata_mr;
//    struct rdma_buffer_attr client_metadata_attr;
    struct ibv_sge client_recv_sge;
    struct ibv_recv_wr client_recv_wr;
    struct ibv_recv_wr *bad_client_recv_wr;

    struct request *request;
    struct ibv_mr *request_mr, *request_attr_mr;
    struct rdma_buffer_attr request_attr;
    struct ibv_sge client_send_sge;
    struct ibv_send_wr client_send_wr;
    struct ibv_send_wr *bad_client_send_wr;
    struct ibv_wc wc;
    struct ibv_qp_init_attr qp_init_attr;
};

struct conn_info {
    enum connection_type type;
    struct sockaddr_in addr;
    unsigned int port;
    bool is_test;

    struct tcp_conn_info *tcp_listening_info;
    struct rc_server_info *rc_connection;
};

struct conn_info *server_init(int argc, char *arg[]);
int accept_new_connection(struct conn_info *conn_info, struct conn_info *new_conn_info);
int recv_request(struct conn_info *client, struct request *request);
int connection_ready(int socket);
int receive_header(struct conn_info *client, struct request *request);
void close_connection(int socket);
int read_payload(struct conn_info *client, struct request *request, size_t expected_len,
                 char *buf);
int check_payload(int socket, struct request *request, size_t expected_len);

#endif //RDMA_KV_STORE_SERVER_UTILS_H
