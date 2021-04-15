//
// Created by jtroo on 14-04-21.
//

#ifndef RDMA_KV_STORE_TCP_SERVER_UTILS_H
#define RDMA_KV_STORE_TCP_SERVER_UTILS_H

#include "server_utils.h"

int init_tcp_server(struct conn_info *connInfo);
int tcp_accept(int sockfd, struct sockaddr *addr, socklen_t * addrlen);
int tcp_accept_new_connection(int listen_sock, struct conn_info *conn_info);

#endif //RDMA_KV_STORE_TCP_SERVER_UTILS_H
