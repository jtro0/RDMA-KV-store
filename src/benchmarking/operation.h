//
// Created by jtroo on 27-04-21.
//

#ifndef RDMA_KV_STORE_OPERATION_H
#define RDMA_KV_STORE_OPERATION_H

struct operation {
    struct timeval *start;
    struct timeval *end;
};
#endif //RDMA_KV_STORE_OPERATION_H
