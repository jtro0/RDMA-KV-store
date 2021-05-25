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

#define REQUEST_BACKLOG 5

#define MAX_CLIENTS 1024
struct tcp_client_info {
    int socket_fd;
};

struct tcp_conn_info {
    int socket_fd;
};

struct rc_client_connection {
    struct ibv_pd *pd;
    struct ibv_comp_channel *io_completion_channel;
    struct ibv_cq *cq;
    struct ibv_qp_init_attr qp_init_attr;
    struct rdma_cm_id *cm_client_id;
    struct ibv_qp *client_qp;
    struct ibv_mr *request_mr, *response_mr;
};


struct rc_server_info {
    struct rdma_event_channel *cm_event_channel;
    struct rdma_cm_id *cm_server_id;
};

// TODO similar to rc? Make a common rdma?
struct ud_client_connection {
   struct ud_server_info *ud_server;
//   struct ibv_ah *ah; // maybe array of ah pointers in server?
   int socket_fd;
//   struct ud_response *response;
   struct ibv_mr *response_mr;
   struct ibv_wc *wc;
//   struct qp_attr *remote_dgram_qp_attr;
    int client_handling;
};

struct ud_server_info {
    struct ibv_qp *ud_qp;
    struct ibv_cq *ud_recv_cq, *ud_send_cq; // need to split to not get the sent notifying
    union ibv_gid server_gid;
    struct qp_attr local_dgram_qp_attrs;	// Local and remote queue pair attributes
    struct qp_attr remote_dgram_qp_attrs[MAX_CLIENTS]; // TODO make array
    struct ibv_pd *pd;
    struct ibv_comp_channel *io_completion_channel_recv, *io_completion_channel_send;
    struct ibv_qp_init_attr qp_init_attr; // maybe?
    struct rdma_cm_id *cm_client_id; // maybe
//    struct ibv_qp *client_qp;
    struct ibv_mr *request_mr;
    int socket_fd;
    int client_counter;
    struct ud_request *request;
    int request_count;
    struct ibv_ah *ah[MAX_CLIENTS];
};

struct client_info {
    enum connection_type type;
    bool is_test;
    struct request *request;
    unsigned int request_count;
    struct response *response;

    struct tcp_client_info *tcp_client;
    struct rc_client_connection *rc_client;
    struct ud_client_connection *ud_client;

    int client_nr;
};

struct server_info {
    enum connection_type type;
    struct sockaddr_in addr;
    unsigned int port;
    bool is_test;

    struct tcp_conn_info *tcp_server_info;
    struct rc_server_info *rc_server_info;
    struct ud_server_info *ud_server_info;

//    struct client_info *client;// Make array when doing multi clients
};

struct server_info *server_init(int argc, char *arg[]);

int accept_new_connection(struct server_info *server, struct client_info *client);

struct request* recv_request(struct client_info *client);

int connection_ready(int socket);

int prepare_for_next_request(struct client_info *client);

int receive_header(struct client_info *client);

void close_connection(int socket);

int read_payload(struct client_info *client, struct request *request, size_t expected_len,
                 char *buf);

int check_payload(int socket, struct request *request, size_t expected_len);

int send_response_to_client(struct client_info *client);
struct request *get_current_request(struct client_info *client);
void ready_for_next_request(struct client_info *client);
bool is_connection_closed(struct client_info *client);

#endif //RDMA_KV_STORE_SERVER_UTILS_H
