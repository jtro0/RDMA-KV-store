//
// Created by Jens on 10/05/2021.
//

#ifndef RDMA_KV_STORE_UD_SERVER_UTILS_H
#define RDMA_KV_STORE_UD_SERVER_UTILS_H

#include "server/server_utils.h"

int setup_ud_client_resources(struct ud_client_connection *client);

int init_ud_server(struct server_info *server);

int ud_accept_new_connection(struct server_info *server, struct client_info *client);

int ud_receive_header(struct client_info *client);

int ud_post_receive_request(struct ud_server_info *server);

int ud_send_response(struct client_info *client);

#endif //RDMA_KV_STORE_UD_SERVER_UTILS_H
