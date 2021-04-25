#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <assert.h>
#include <string.h>

#include "hash.h"
#include "request_dispatcher.h"
#include "parser.h"
#include "kvstore.h"

int send_response(struct client_info *client, int code, int payload_len, char *payload) {
    if (client->is_test) {
        char response[MSG_SIZE];
        ssize_t sent;
        int response_len;

        response_len = snprintf(response, sizeof(response), "%d %s %d\n", code,
                                code_msg(code), payload_len);
        if (response_len < 0 || response_len == sizeof(response)) {
            error("Error formatting response (status: %d)\n", code);
            return -1;
        }
        sent = send_on_socket(client->tcp_client->socket_fd, response, response_len);
        if (sent <= 0) {
            error("Cannot send response on socket\n");
            return -1;
        }

        if (payload_len) {
            assert(payload);
            sent = send_on_socket(client->tcp_client->socket_fd, payload, payload_len);
            if (sent <= 0) {
                error("Cannot send payload on socket\n");
                return -1;
            }
            sent = send_on_socket(client->tcp_client->socket_fd, "\n", 1);
            if (sent <= 0) {
                error("Cannot send payload terminator on socket\n");
                return -1;
            }
        }
        pr_debug("Response %s\n", code_msg(code));
        return 0;
    }

    int sent;

    bzero(client->response, sizeof(struct response));
    client->response->code = code;
    memcpy(client->response->msg, payload, MSG_SIZE);
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

/*
 * Thread unsafe
 */
int dump(const char *filename, struct client_info *client) {
    assert(ht != NULL);

    int fd;
    if ((fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1) {
        char errbuf[1024];
        snprintf(errbuf, sizeof(errbuf), "Could not open %s for creating dump",
                 filename);
        error("%s\n", errbuf);
        send_response(client, UNK_ERROR, strlen(errbuf), errbuf);
        return -1;
    }

    for (unsigned bucket = 0; bucket < ht->capacity; bucket++) {
        dprintf(fd, "B %d\n", bucket);
        hash_item_t *curr = ht->items[bucket];
        while (curr != NULL) {
            dprintf(fd, "K %s %zu\n", curr->key, curr->value_size);
            if (write(fd, curr->value, curr->value_size) < 0) {
                char errbuf[1024];
                snprintf(errbuf, sizeof(errbuf),
                         "Could not dump value of size %zu for key %s",
                         curr->value_size, curr->key);
                error("%s\n", errbuf);
                send_response(client, UNK_ERROR, strlen(errbuf), errbuf);
                break;
            }
            if (write(fd, "\n", 1) < 0) {
                error("Could not write newline to dump\n");
                send_response(client, UNK_ERROR, 0, NULL);
                break;
            }
            curr = curr->next;
        }
    }
    close(fd);
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
    struct request *request = client->request;
    pr_info("Method: %s\n", method_to_str(request->method));
    pr_info("Key: %s [%zu]\n", request->key, request->key_len);

    switch (request->method) {
        case PING:
            ping(client);
            break;
        case DUMP:
            dump(DUMP_FILE, client);
            break;
        case EXIT:
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
