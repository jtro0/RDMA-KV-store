#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <signal.h>
#include <netinet/tcp.h>
#include <dlfcn.h>
#include <assert.h>

#include "parser.h"
#include "server_utils.h"
#include "common.h"
#include "request_dispatcher.h"

#include "tcp_server_utils.h"
#include "rc_server_utils.h"

int debug = 0;
int verbose = 0;


void usage(char *prog) {
    fprintf(stderr, "Usage %s [--help -h] [--verbose -v] [--debug -d] "
                    "[--port -p] [--rdma -r]\n", prog);
    fprintf(stderr, "--help -h\n\t Print help message\n");
    fprintf(stderr, "--verbose -v\n\t Print info messages\n");
    fprintf(stderr, "--debug -d\n\t Print debug info\n");
    fprintf(stderr,
            "--port -p\n\t Port to bind on. Default: pick the first available port\n");
    fprintf(stderr,
            "--rdma -r\n\t Use RDMA. Choose rc, uc, or ud.\n");
}

struct server_info *server_init(int argc, char *argv[]) {
    struct server_info *connInfo = calloc(1, sizeof(struct server_info));
    connInfo->port = PORT;
    connInfo->type = TCP;
    char *rdma_type;

    const struct option long_options[] = {
            {"help",    no_argument,       NULL, 'h'},
            {"verbose", no_argument,       NULL, 'v'},
            {"debug",   no_argument,       NULL, 'd'},
            {"port",    required_argument, NULL, 'p'},
            {"rdma",    required_argument, NULL, 'r'},
            {0, 0, 0,                            0}
    };

    for (;;) {
        int option_index = 0;
        int c;
        c = getopt_long(argc, argv, "hvdp:r:t", long_options,
                        &option_index);
        if (c == -1)
            break;

        switch (c) {
            case 'v':
                verbose = 1;
                break;
            case 'd':
                debug = 1;
                break;
            case 'p':
                connInfo->port = atoi(optarg);
                break;
            case 'r':
                rdma_type = optarg;
                if (strncmp(rdma_type, "rc", 2) == 0) {
                    connInfo->type = RC;
                    break;
                } else if (strncmp(rdma_type, "uc", 2) == 0) {
                    connInfo->type = UC;
                    break;
                } else if (strncmp(rdma_type, "ud", 2) == 0) {
                    connInfo->type = UD;
                    break;
                }
            case 't':
                connInfo->is_test = true;
                break;
            case 'h':
            default:
                usage(argv[0]);
                exit(EXIT_SUCCESS);
        }
    }

    memset((void *) &connInfo->addr, 0, sizeof(connInfo->addr));
    connInfo->addr.sin_family = AF_INET;
    connInfo->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    connInfo->addr.sin_port = htons(connInfo->port);

    connInfo->client = malloc(sizeof(struct client_info));
    connInfo->client->request = calloc(REQUEST_BACKLOG, sizeof(struct request));
//    connInfo->client->request = malloc(sizeof(struct request));
    connInfo->client->response = malloc(sizeof(struct response));
    connInfo->client->request_count = 0;

    switch (connInfo->type) {
        case TCP:
            connInfo->tcp_server_info = calloc(1, sizeof(struct tcp_conn_info));
            init_tcp_server(connInfo);
            break;
        case RC:
            connInfo->rc_server_info = calloc(1, sizeof(struct rc_server_info));
            init_rc_server(connInfo);
            break;
        case UC:
            break;
        case UD:
            break;
    }

    return connInfo;
}

int accept_new_connection(struct server_info *server, struct client_info *client) {
    int ret;

    switch (server->type) {
        case TCP:
            client->tcp_client = malloc(sizeof(struct tcp_client_info));
            ret = tcp_accept_new_connection(server, client->tcp_client);
            break;
        case RC:
            setup_client_resources(client->rc_client);
            ret = rc_accept_new_connection(server);
            break;
        case UC:
            break;
        case UD:
            break;
    }
    if (ret < 0) {
        error("Cannot accept new connection");
        free(client);
    }
    return ret;
}

void close_connection(int socket) {
    pr_debug("Closing connection on socket %d\n", socket);
    close(socket);
}


// TODO Ask if it is better to have it wait for the prev request to be done or to alloc/reg new request
int ready_for_next_request(struct client_info *client) {
    client->request_count = (client->request_count+1)%REQUEST_BACKLOG;
    pr_info("ready to receive request %d\n", client->request_count);
//    bzero(&client->request[client->request_count], sizeof(struct request));
//    client->request[client->request_count].msg_len = 0;
//    client->request[client->request_count].key_len = 0;
//    client->request[client->request_count].method = 0;
//    bzero(client->request[client->request_count].msg, MSG_SIZE);
//    bzero(client->request[client->request_count].key, KEY_SIZE);

    int ret = 0;
    switch (client->type) {
        case TCP:
            break;
        case RC:
            ret = rc_post_receive_request(client);
            break;
        case UC:
            break;
        case UD:
            break;
    }
    return ret;
}

int receive_header(struct client_info *client) {
    int recved;
    // With strings
    if (client->is_test) {
        // TODO remove dup code
        if (connection_ready(client->tcp_client->socket_fd) == -1) {
            return -1;
        }

        recved = parse_header(client->tcp_client->socket_fd, &client->request[client->request_count]);
        if (recved == 0)
            return 0;
        if (recved == -1) {
            client->request[client->request_count].connection_close = 1;
            return -1;
        }
        if (recved == -2) {
            send_response(client, PARSING_ERROR, 0, NULL);
            client->request[client->request_count].connection_close = 1;
            return -1;
        }
        return 1;
    }
    switch (client->type) {
        case TCP:
            if (connection_ready(client->tcp_client->socket_fd) == -1) {
                return -1;
            }
            recved = tcp_read_header(client->tcp_client->socket_fd, &client->request[client->request_count]);
            break;
        case RC:
            pr_info("rc going to receive\n");
            recved = rc_receive_header(client->rc_client);
            break;
        case UC:
            break;
        case UD:
            break;
    }
    return recved;
}

/**
 * Try to read the header line for a request.
 * @return an int representing the request's method, -1 on error.
 * an error can be caused by the socket not ready for I/O or
 * a bad request which cannot be parsed
 */
struct request *recv_request(struct client_info *client) {
    if (receive_header(client) == -1) {
        // Connection closed from client side or error occurred
        pr_info("No header received\n");
        return NULL;
    }
    struct request *test = malloc(sizeof(struct request));
    memcpy(test, &client->request[client->request_count], sizeof(struct request));
    print_request(test);
    request_dispatcher(client);
    return &client->request[client->request_count];
}

/**
 * It read 'expected_len' bytes from 'socket' and return them into 'buf'
 * it returns the actual number of read bytes or -1 on error.
 * On error 'request->connection_close' is set to indicate that the connection
 * should be closed from the server side.
 */
int read_payload(struct client_info *client, struct request *request, size_t expected_len,
                 char *buf) {
    char tmp;
    int recvd = 0;
    // Still read out the payload so we keep the stream consistent
    for (size_t i = 0; i < expected_len; i++) {
        if (read(client->tcp_client->socket_fd, &tmp, 1) <= 0) {
            request->connection_close = 1;
            return -1;
        }
        recvd++;
        buf[i] = tmp;
    }

    return recvd;
}

/**
 * Check the payload is well formed and read the last byte which shoudl be '\n'
 * It returns 0 on success, -1 otherwise.
 */
int check_payload(int socket, struct request *request, size_t expected_len) {
    char tmp;
    int rcved;

    // The payload (if there was any) should be followed by a \n
    if (expected_len &&
        (((rcved = read(socket, &tmp, 1)) <= 0) || tmp != '\n')) {
        error("Corrupted stream (read %d chars, char %c (%#x))\n",
              rcved, tmp, tmp);
        request->connection_close = 1;
        return -1;
    }
    return 0;
}

int send_response_to_client(struct client_info *client) {
    int ret;
    switch (client->type) {
        case TCP:
            ret = tcp_send_response(client);
            break;
        case RC:
            ret = rc_send_response(client);
            break;
        case UC:
            break;
        case UD:
            break;
    }
    return ret;
}
