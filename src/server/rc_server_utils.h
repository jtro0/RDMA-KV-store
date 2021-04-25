//
// Created by jtroo on 15-04-21.
//

#ifndef RDMA_KV_STORE_RC_SERVER_UTILS_H
#define RDMA_KV_STORE_RC_SERVER_UTILS_H

#include "server/server_utils.h"

int setup_client_resources(struct rc_client_connection *client);

int init_rc_server(struct server_info *server);

int rc_accept_new_connection(struct server_info *server);

int rc_receive_header(struct rc_client_connection *client);

int rc_post_receive_request(struct client_info *client);

int rc_send_response(struct client_info *client);
#endif //RDMA_KV_STORE_RC_SERVER_UTILS_H
