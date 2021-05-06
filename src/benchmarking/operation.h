//
// Created by jtroo on 27-04-21.
//

#ifndef RDMA_KV_STORE_OPERATION_H
#define RDMA_KV_STORE_OPERATION_H

struct operation {
    struct operation *next; // maybe array?

    struct timeval *start;
    struct timeval *end;
//    struct request *request;
//    struct response *response;
//    struct response *expected_response;
};
#endif //RDMA_KV_STORE_OPERATION_H
