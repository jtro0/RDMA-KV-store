//
// Created by jtroo on 14-04-21.
//

#ifndef RDMA_KV_STORE_SERVER_UTILS_H
#define RDMA_KV_STORE_SERVER_UTILS_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

#include "request_dispatcher.h"
#include "common.h"

#define BACKLOG     10
#define TIMEOUT     60

enum connection_type { TCP, RC, UC, UD};

struct tcp_conn_info {
    struct sockaddr_in addr;
    int socket_fd;
};

struct conn_info {
    enum connection_type type;
    unsigned int port;
    bool is_test;

    struct tcp_conn_info *tcp_listening_info;
};

struct conn_info *server_init(int argc, char *arg[]);
int accept_new_connection(struct conn_info *conn_info, struct conn_info *new_conn_info);
int recv_request(struct conn_info *client, struct request *request);
int connection_ready(int socket);
int receive_header(struct conn_info *client, struct request *request);
void close_connection(int socket);
struct request *allocate_request();
int read_payload(struct conn_info *client, struct request *request, size_t expected_len,
                 char *buf);
int check_payload(int socket, struct request *request, size_t expected_len);

#endif //RDMA_KV_STORE_SERVER_UTILS_H
