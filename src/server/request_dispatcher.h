//
// Created by jtroo on 14-04-21.
//

#ifndef RDMA_KV_STORE_REQUEST_DISPATCHER_H
#define RDMA_KV_STORE_REQUEST_DISPATCHER_H
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "server_utils.h"
#include "common.h"

int send_response(struct client_info *client, int code, int payload_len, char *payload);
void request_dispatcher(struct client_info *client);

#endif //RDMA_KV_STORE_REQUEST_DISPATCHER_H
