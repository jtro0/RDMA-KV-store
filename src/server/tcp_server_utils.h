//
// Created by jtroo on 14-04-21.
//

#ifndef RDMA_KV_STORE_TCP_SERVER_UTILS_H
#define RDMA_KV_STORE_TCP_SERVER_UTILS_H

#include "server/server_utils.h"

int init_tcp_server(struct server_info *connInfo);

int tcp_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

int tcp_accept_new_connection(struct server_info *server, struct tcp_client_info *client);

int tcp_read_header(int socket, struct request *request);

int tcp_send_response(struct client_info *client);
#endif //RDMA_KV_STORE_TCP_SERVER_UTILS_H
