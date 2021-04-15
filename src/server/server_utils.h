//
// Created by jtroo on 14-04-21.
//

#ifndef RDMA_KV_STORE_SERVER_UTILS_H
#define RDMA_KV_STORE_SERVER_UTILS_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "request_dispatcher.h"
#include "common.h"

// TODO Remove RDMA stuff here and move them to rdma_common or rc/uc/ud_server_utils
#include "rdma_common.h"

#define BACKLOG     10
#define TIMEOUT     60

enum connection_type { TCP, RC, UC, UD};

struct tcp_conn_info {
    int socket_fd;
};

struct rc_conn_info {
    struct rdma_event_channel *cm_event_channel;
    struct rdma_cm_id *cm_server_id;
    struct ibv_pd *pd;
    struct ibv_comp_channel *io_completion_channel;
    struct ibv_cq *cq;
    struct rdma_cm_id *cm_client_id;
};

struct conn_info {
    enum connection_type type;
    struct sockaddr_in addr;
    unsigned int port;

    struct tcp_conn_info *tcp_listening_info;
    struct rc_conn_info *rc_connection;
};

struct conn_info *server_init(int argc, char *arg[]);
int accept_new_connection(struct conn_info *conn_info, struct conn_info *new_conn_info);
int recv_request(int socket, struct request *request);
int connection_ready(int socket);
int receive_header(int socket, struct request *request);
void close_connection(int socket);
struct request *allocate_request();
int read_payload(int socket, struct request *request, size_t expected_len,
                 char *buf);
int check_payload(int socket, struct request *request, size_t expected_len);

#endif //RDMA_KV_STORE_SERVER_UTILS_H
