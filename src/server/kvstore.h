#ifndef RDMA_KV_STORE_KVSTORE_H
#define RDMA_KV_STORE_KVSTORE_H

#include "common.h"
#include "hash.h"
#include "server_utils.h"

int set_request(struct conn_info *client, struct request *request);

void get_request(struct conn_info *client, struct request *pRequest);

void del_request(struct conn_info *client, struct request *pRequest);

#endif //RDMA_KV_STORE_KVSTORE_H
