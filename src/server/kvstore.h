#ifndef RDMA_KV_STORE_KVSTORE_H
#define RDMA_KV_STORE_KVSTORE_H

#include "common.h"
#include "hash.h"
#include "server_utils.h"

int set_request(struct client_info *client, struct request *request, struct response *response);

void get_request(struct client_info *client, struct request *pRequest, struct response *response);

void del_request(struct client_info *client, struct request *pRequest, struct response *response);

#endif //RDMA_KV_STORE_KVSTORE_H
