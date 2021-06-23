//
// Created by jtroo on 27-04-21.
//

#ifndef RDMA_KV_STORE_INSTANCE_H
#define RDMA_KV_STORE_INSTANCE_H
#include "operation.h"
#include "common.h"
struct thread_args {
    enum connection_type conn_t;
    struct sockaddr_in *server_addr;
    unsigned int num_ops;
    int instance_nr;
    int blocking;
};

void * start_instance(void *args);

#endif //RDMA_KV_STORE_INSTANCE_H
