//
// Created by jtroo on 15-04-21.
//

#ifndef RDMA_KV_STORE_RC_SERVER_UTILS_H
#define RDMA_KV_STORE_RC_SERVER_UTILS_H

#include "server/server_utils.h"

#define NUM_WC 20

int setup_client_resources(struct conn_info *connInfo);
int init_rc_server(struct conn_info *connInfo);
int rc_accept_new_connection(struct rc_server_info *listening);
//int send_buffer_meta(struct rc_server_info *server);
int rc_receive_header(struct rc_server_info *server);
#endif //RDMA_KV_STORE_RC_SERVER_UTILS_H
