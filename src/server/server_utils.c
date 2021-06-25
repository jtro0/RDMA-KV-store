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

#include "server_utils.h"
#include "common.h"
#include "request_dispatcher.h"

#include "tcp_server_utils.h"
#include "rc_server_utils.h"
#include "uc_server_utils.h"
#include "ud_server_utils.h"

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
    fprintf(stderr,
            "--blocking -b\n\t Use completion events for RDMA. This will cause threads to block until new completion event is generated.\n");
}

struct server_info *server_init(int argc, char *argv[]) {
    struct server_info *connInfo = calloc(1, sizeof(struct server_info));
    connInfo->port = PORT;
    connInfo->type = TCP;
    char *rdma_type;
    connInfo->blocking = 0;

    const struct option long_options[] = {
            {"help",    no_argument,       NULL, 'h'},
            {"verbose", no_argument,       NULL, 'v'},
            {"debug",   no_argument,       NULL, 'd'},
            {"port",    required_argument, NULL, 'p'},
            {"rdma",    required_argument, NULL, 'r'},
            {"blocking",no_argument,       NULL, 'b'},
            {0,  0,                0,      0}
    };

    // Process arguments
    for (;;) {
        int option_index = 0;
        int c;
        c = getopt_long(argc, argv, "hvdp:r:o", long_options,
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
            case 'b':
                connInfo->blocking = 1;
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

    // Initialize server connections
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
            connInfo->uc_server_info = calloc(1, sizeof(struct uc_server_info));
            init_uc_server(connInfo);
            break;
        case UD:
            connInfo->ud_server_info = calloc(1, sizeof(struct ud_server_info));
            init_ud_server(connInfo);
            break;
    }

    return connInfo;
}

int accept_new_connection(struct server_info *server, struct client_info *client) {
    int ret;

    // Pass along if it needs to block or not
    client->blocking = server->blocking;

    switch (server->type) {
        case TCP:
            client->tcp_client = malloc(sizeof(struct tcp_client_info));
            ret = tcp_accept_new_connection(server, client->tcp_client);
            break;
        case RC:
            client->rc_client = malloc(sizeof(struct rc_client_connection));
            ret = rc_accept_new_connection(server, client);
            break;
        case UC:
            client->uc_client = malloc(sizeof(struct uc_client_connection));
            ret = uc_accept_new_connection(server, client);
            break;
        case UD:
            client->ud_client = malloc(sizeof(struct ud_client_connection));
            ret = ud_accept_new_connection(server, client);
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


int prepare_for_next_request(struct client_info *client) {
    pr_info("client %d: ready to receive request %d\n", client->client_nr, client->request_count);

    int ret = 0;
    switch (client->type) {
        case TCP:
            break;
        case RC:
            ret = rc_post_receive_request(client);
            break;
        case UC:
            ret = uc_post_receive_request(client);
            break;
        case UD:
            // UD does not need to prepare the same way, this is done in ready_for_next_request()
            break;
    }
    check(ret, ret, "Failed to pre-post the receive buffer %d, errno: %d \n", client->request_count, ret);

    return ret;
}

/*
 * Receive request
 */
int receive_header(struct client_info *client) {
    int recved;
    switch (client->type) {
        case TCP:
            if (connection_ready(client->tcp_client->socket_fd) == -1) {
                return -1;
            }
            recved = tcp_read_header(client->tcp_client->socket_fd, &client->request[client->request_count]);
            break;
        case RC:
            recved = rc_receive_header(client);
            break;
        case UC:
            recved = uc_receive_header(client);
            break;
        case UD:
            recved = ud_receive_header(client);
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
    struct request *current = get_current_request(client);
    if (client->type == UD) {
        client->ud_client->client_handling = current->client_id; // Remember who sent this, for AH
    }
    request_dispatcher(client);
    return current;
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
            ret = uc_send_response(client);
            break;
        case UD:
            ret = ud_send_response(client);
            break;
    }
    return ret;
}

struct request *get_current_request(struct client_info *client) {
    struct request *request;
    switch (client->type) {
        case TCP:
        case UC:
        case RC:
            return &client->request[client->request_count];
        case UD:
            request = &client->ud_client->ud_server->request[client->ud_client->ud_server->request_count].request;
            return request;
    }
}

void ready_for_next_request(struct client_info *client) {
    switch (client->type) {
        case TCP:
        case RC:
        case UC:
            client->request_count = (client->request_count + 1) % REQUEST_BACKLOG;
            break;
        case UD:
            ud_post_receive_request(client->ud_client->ud_server);
            client->ud_client->ud_server->request_count = ((client->ud_client->ud_server->request_count + 1) % (MAX_CLIENTS));
            pthread_mutex_unlock(&client->ud_client->ud_server->lock);

            break;
    }
}

bool is_connection_closed(struct client_info *client) {
    switch (client->type) {
        case TCP:
        case RC:
        case UC:
            return client->request->connection_close;
        case UD:
            return client->ud_client->ud_server->request->request.connection_close;
    }
}