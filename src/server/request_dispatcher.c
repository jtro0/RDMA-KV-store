#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>

#include "hash.h"
#include "request_dispatcher.h"

int send_response(struct client_info *client, int code, int payload_len, char *payload) {
    int sent;

    bzero(client->response->msg, MSG_SIZE);
    client->response->code = code;
    memcpy(client->response->msg, payload, payload_len);

    client->response->msg_len = payload_len;

    sent = send_response_to_client(client);

    if (sent < 0) {
        error("Cannot send response to client\n");
        return -1;
    }

    return 0;
}

int ping(struct client_info *client) {
    return send_response(client, OK, 0, NULL);
}

int setopt_request(struct client_info *client, struct request *request) {
    if (!strcmp(request->key, "SNDBUF")) {
        char respbuf[256];
        int sndbuf = 0;
        socklen_t optlen = sizeof(sndbuf);

        if (setsockopt(client->tcp_client->socket_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf,
                       sizeof(sndbuf)) < 0) {
            perror("setsockopt SNDBUF");
            return send_response(client, SETOPT_ERROR, 0, NULL);
        }
        if (getsockopt(client->tcp_client->socket_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &optlen)) {
            perror("getsockopt SNDBUF");
            return send_response(client, SETOPT_ERROR, 0, NULL);
        }
        snprintf(respbuf, sizeof(respbuf), "%d", sndbuf);
        return send_response(client, OK, strlen(respbuf), respbuf);
    } else {
        return send_response(client, KEY_ERROR, 0, NULL);
    }
}

void request_dispatcher(struct client_info *client) {
    struct request *request = get_current_request(client);
    pr_info("Method: %s\n", method_to_str(request->method));
    pr_info("Key: %s [%zu]\n", request->key, request->key_len);

    switch (request->method) {
        case PING:
            ping(client);
            break;
        case EXIT: //TODO: Deallocate for clients
            send_response(client, OK, 0, NULL);
            exit(0);
            break;
        case SETOPT:
            setopt_request(client, request);
            break;
        case UNK:
            send_response(client, PARSING_ERROR, 0, NULL);
            break;
        default:
            return;
    }
}
