//
// Created by jtroo on 15-04-21.
//

#ifndef RDMA_KV_STORE_uc_SERVER_UTILS_H
#define RDMA_KV_STORE_uc_SERVER_UTILS_H

#include "server/server_utils.h"

int setup_uc_client_resources(struct uc_client_connection *client);

int init_uc_server(struct server_info *server);

int uc_accept_new_connection(struct server_info *server, struct client_info *client);

int uc_receive_header(struct client_info *client);

int uc_post_receive_request(struct client_info *client);

int uc_send_response(struct client_info *client);
#endif //RDMA_KV_STORE_uc_SERVER_UTILS_H
