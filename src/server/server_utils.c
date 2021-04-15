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

int debug = 0;
int verbose = 0;

struct request *allocate_request()
{
    struct request *r = malloc(sizeof(struct request));
    if (r == NULL) {
        pr_debug("error in memory allocation");
        return NULL;
    }

    return r;
}

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
        c = getopt_long(argc, argv, "hvdp:r:", long_options,
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
            case 'h':
            default:
                usage(argv[0]);
                exit(EXIT_SUCCESS);
        }
    }

    switch (connInfo->type) {
        case TCP:
            connInfo->tcp_listening_info = calloc(1, sizeof(struct tcp_conn_info));
            init_tcp_server(connInfo);
            break;
        case RC:
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
            break;
        case UC:
            break;
        case UD:
            break;
    }
    if (ret < 0) {
        error("Cannot accept new connection");
        free(conn_info);
    }
    return ret;
}

void close_connection(int socket)
{
    pr_debug("Closing connection on socket %d\n", socket);
    close(socket);
}

int connection_ready(int socket)
{
    struct timeval timeout;
    timeout.tv_sec = TIMEOUT;
    timeout.tv_usec = 0;
    fd_set rset;

    FD_ZERO(&rset);
    FD_SET(socket, &rset);

    // Close connection after TIMEOUT seconds without requests
    if (select(socket + 1, &rset, NULL, NULL, &timeout) == -1) {
        perror("Select timeout expired with no data\n");
        return -1;
    }

    return 0;
}

int receive_header(int socket, struct request *request)
{
    int recved;
    recved = parse_header(socket, request);
    if (recved == 0)
        return 0;
    if (recved == -1) {
        request->connection_close = 1;
        return -1;
    }
    if (recved == -2) {
        send_response(socket, PARSING_ERROR, 0, NULL);
        request->connection_close = 1;
        return -1;
    }
    return 1;
}

/**
 * Try to read the header line for a request.
 * @return an int representing the request's method, -1 on error.
 * an error can be caused by the socket not ready for I/O or
 * a bad request which cannot be parsed
 */
int recv_request(int socket, struct request *request)
{
    if (connection_ready(socket) == -1) {
        return -1;
    }
    if (receive_header(socket, request) == -1) {
        // Connection closed from client side or error occurred
        free(request->key);
        request->key = NULL;
        return -1;
    }

    request_dispatcher(socket, request);
    return request->method;
}

/**
 * It read 'expected_len' bytes from 'socket' and return them into 'buf'
 * it returns the actual number of read bytes or -1 on error.
 * On error 'request->connection_close' is set to indicate that the connection
 * should be closed from the server side.
 */
int read_payload(int socket, struct request *request, size_t expected_len,
                 char *buf)
{
    char tmp;
    int recvd = 0;
    // Still read out the payload so we keep the stream consistent
    for (size_t i = 0; i < expected_len; i++) {
        if (read(socket, &tmp, 1) <= 0) {
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
