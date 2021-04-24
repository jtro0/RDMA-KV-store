#include <server/parser.h>
#include "common.h"

struct request *allocate_request()
{
    struct request *r = malloc(sizeof(struct request));
    if (r == NULL) {
        pr_debug("error in memory allocation");
        return NULL;
    }
//    r->key = malloc(KEY_SIZE);

    return r;
}


void print_request(struct request *request) {
    pr_info("Request:\nMethod: %s\nKey: %s\nKey_len: %zu\nMessage_len: %zu\nConnection_closed: %d\n",
            method_to_str(request->method), request->key, request->key_len, request->msg_len, request->connection_close);
}