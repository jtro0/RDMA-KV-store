#ifndef RDMA_KV_STORE_KVSTORE_H
#define RDMA_KV_STORE_KVSTORE_H

#include "common.h"
#include "hash.h"

int set_request(int socket, struct request *request);

void get_request(int fd, struct request *pRequest);

void del_request(int fd, struct request *pRequest);

#endif //RDMA_KV_STORE_KVSTORE_H
