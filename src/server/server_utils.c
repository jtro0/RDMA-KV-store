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


void usage(char *prog)
{
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

struct conn_info *server_init(int argc, char *argv[])
{
    struct conn_info *connInfo = calloc(1, sizeof(struct conn_info));
    connInfo->port = PORT;
    connInfo->type = TCP;
    char *rdma_type;

    const struct option long_options[] = {
            {"help", no_argument, NULL, 'h'},
            {"verbose", no_argument, NULL, 'v'},
            {"debug", no_argument, NULL, 'd'},
            {"port", required_argument, NULL, 'p'},
            {"rdma", required_argument, NULL, 'r'},
            {0, 0, 0, 0}
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
                }
                else if (strncmp(rdma_type, "uc", 2) == 0) {
                    connInfo->type = UC;
                    break;
                }
                else if (strncmp(rdma_type, "ud", 2) == 0) {
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

    memset((void *)&connInfo->addr, 0, sizeof(connInfo->addr));
    connInfo->addr.sin_family = AF_INET;
    connInfo->addr.sin_addr.s_addr = htonl(INADDR_ANY);
    connInfo->addr.sin_port = htons(connInfo->port);

    switch (connInfo->type) {
        case TCP:
            connInfo->tcp_listening_info = calloc(1, sizeof(struct tcp_conn_info));
            init_tcp_server(connInfo);
            break;
        case RC:
            connInfo->rc_connection = calloc(1, sizeof(struct rc_server_info));
            init_rc_server(connInfo);
            setup_client_resources(connInfo);
            break;
        case UC:
            break;
        case UD:
            break;
    }

    return connInfo;
}

int accept_new_connection(struct conn_info *conn_info, struct conn_info *new_conn_info) {
    int ret;

    switch (conn_info->type) {
        case TCP:
            new_conn_info->tcp_listening_info = calloc(1, sizeof(struct tcp_conn_info));
            ret = tcp_accept_new_connection(conn_info->tcp_listening_info->socket_fd, new_conn_info);
            break;
        case RC:
//            conn_info->rc_connection->clientInfo = calloc(1, sizeof(struct rc_client_info));
            ret = rc_accept_new_connection(conn_info->rc_connection);
//            send_buffer_meta(conn_info->rc_connection);
            break;
        case UC:
            break;
        case UD:
            break;
    }
    if (ret < 0) {
        error("Cannot accept new connection");
        free(new_conn_info);
    }
    return ret;
}

void close_connection(int socket)
{
    pr_debug("Closing connection on socket %d\n", socket);
    close(socket);
}


int receive_header(struct conn_info *client, struct request *request)
{
    int recved;
    // With strings
    if (client->is_test) {
        // TODO remove dup code
        if (connection_ready(client->tcp_listening_info->socket_fd) == -1) {
            return -1;
        }

        recved = parse_header(client->tcp_listening_info->socket_fd, request);
        if (recved == 0)
            return 0;
        if (recved == -1) {
            request->connection_close = 1;
            return -1;
        }
        if (recved == -2) {
            send_response(client->tcp_listening_info->socket_fd, PARSING_ERROR, 0, NULL);
            request->connection_close = 1;
            return -1;
        }
        return 1;
    }
    switch (client->type) {
        case TCP:
            if (connection_ready(client->tcp_listening_info->socket_fd) == -1) {
                return -1;
            }
            recved = tcp_read_header(client->tcp_listening_info->socket_fd, request);
            break;
        case RC:
            pr_info("rc going to receive\n");
            recved = rc_receive_header(client->rc_connection);
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
int recv_request(struct conn_info *client, struct request *request)
{
    if (receive_header(client, request) == -1) {
        // Connection closed from client side or error occurred
        pr_info("no header\n");
//        free(request->key);
//        request->key = NULL;
        return -1;
    }
    print_request(client->rc_connection->request);

//    request_dispatcher(client, request);
    return request->method;
}

/**
 * It read 'expected_len' bytes from 'socket' and return them into 'buf'
 * it returns the actual number of read bytes or -1 on error.
 * On error 'request->connection_close' is set to indicate that the connection
 * should be closed from the server side.
 */
int read_payload(struct conn_info *client, struct request *request, size_t expected_len,
                 char *buf)
{
    char tmp;
    int recvd = 0;
    // Still read out the payload so we keep the stream consistent
    for (size_t i = 0; i < expected_len; i++) {
        if (read(client->tcp_listening_info->socket_fd, &tmp, 1) <= 0) {
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
int check_payload(int socket, struct request *request, size_t expected_len)
{
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
