#include <server/parser.h>
#include "common.h"

struct request *allocate_request() {
    struct request *r = malloc(sizeof(struct request));
    if (r == NULL) {
        pr_debug("error in memory allocation");
        return NULL;
    }

    return r;
}

enum method method_to_enum(const char *str) {
    const size_t nmethods = sizeof(method_conversion) /
                            sizeof(method_conversion[0]);
    size_t i;
    for (i = 0; i < nmethods; i++) {
        if (!strcmp(str, method_conversion[i].str)) {
            return method_conversion[i].val;
        }
    }
    return UNK;
}

const char *code_msg(int code) {
    switch (code) {
        RESPONSE_CODES(RESPONSE_TEXT)
    }
    return "Unknown error";
}

const char *method_to_str(enum method code) {
    const size_t nmethods = sizeof(method_conversion) /
                            sizeof(method_conversion[0]);
    size_t i;
    for (i = 0; i < nmethods; i++) {
        if (method_conversion[i].val == code) {
            return method_conversion[i].str;
        }
    }
    return "UNK";
}

void print_request(struct request *request) {
    pr_info("Request:\nMethod: %s\nKey: %s\nKey_len: %zu\nMessage_len: %zu\nConnection_closed: %d\nMsg: %s\n",
            method_to_str(request->method), request->key, request->key_len, request->msg_len,
            request->connection_close, request->msg);
}

void print_response(struct response *response) {
    pr_info("Response:\nCode: %s\nMsg: %s\nMsg_len: %zu\n",
            code_msg(response->code), response->msg, response->msg_len);
}