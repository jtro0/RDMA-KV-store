//
// Created by jtroo on 14-04-21.
//
#define _GNU_SOURCE
#include <netinet/tcp.h>
#include <signal.h>
#include <dlfcn.h>
#include <assert.h>
#include <string.h>

#include "tcp_server_utils.h"

int init_tcp_server(struct conn_info *connInfo) {
    int option = 1;
    
    /* TCP connection */
    connInfo->tcp_listening_info->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (connInfo->tcp_listening_info->socket_fd == -1) {
        perror("socket cannot be created\n");
        exit(EXIT_FAILURE);
    }

    /* if the port is busy and in the TIME_WAIT state, reuse it anyway. */
    if (setsockopt(connInfo->tcp_listening_info->socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&option,
                   sizeof(option)) < 0) {
        close(connInfo->tcp_listening_info->socket_fd);
        perror("setsockopt failed\n");
        exit(EXIT_FAILURE);
    }

    if ((bind(connInfo->tcp_listening_info->socket_fd, (struct sockaddr *)&connInfo->addr,
              sizeof(connInfo->addr))) < 0) {
        perror("address and port binding failed\n");
        exit(EXIT_FAILURE);
    }

    socklen_t addr_len = sizeof(connInfo->addr);
    if (getsockname(connInfo->tcp_listening_info->socket_fd, (struct sockaddr *)&connInfo->addr, &addr_len)
        == -1) {
        perror("address and port binding failed\n");
        exit(EXIT_FAILURE);
    }

    pr_info("[%s] Pid:%d bind on socket:%d Port:%d\n", SERVER,
            (int)getpid(), connInfo->tcp_listening_info->socket_fd, ntohs(connInfo->addr.sin_port));

    if (listen(connInfo->tcp_listening_info->socket_fd, BACKLOG) < 0) {
        perror("Cannot listen on socket");
        exit(EXIT_FAILURE);
    }

    pr_info("Listening socket n. %d\n", connInfo->tcp_listening_info->socket_fd);
    signal(SIGPIPE, SIG_IGN);

    return connInfo->tcp_listening_info->socket_fd;
}

int tcp_accept(int sockfd, struct sockaddr *addr, socklen_t * addrlen)
{
    pr_info("TCP Accept new connection\n");

    static int (*_orig_accept) (int, struct sockaddr *, socklen_t *);
    int ret;
    if (!_orig_accept)
        _orig_accept = dlsym(RTLD_NEXT, "accept");
    pr_info("TCP calling accept\n");

    ret = _orig_accept(sockfd, addr, addrlen);
    pr_info("TCP done calling\n");

    assert(ret != -1);
    pr_info("TCP Accept new connection all good\n");

    return ret;
}

/**
 * call accept() on the listening socket for incoming connections
 * @return 0 on success, -1 on error
 */
int tcp_accept_new_connection(int listen_sock, struct conn_info *conn_info)
{
    int nodelay = 1;
    socklen_t addrlen = sizeof(conn_info->addr);

    if ((conn_info->tcp_listening_info->socket_fd =
                 /* accept(listen_sock, (struct sockaddr *)&conn_info->addr, &addrlen)) < 0) { */
                 tcp_accept(listen_sock, (struct sockaddr *)&conn_info->addr,
                            &addrlen)) < 0) {
        error("Cannot accept new connection\n");
        return -1;
    }

    if (setsockopt
                (conn_info->tcp_listening_info->socket_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay,
                 sizeof(nodelay)) < 0) {
        perror("setsockopt TCP_NODELAT");
        return -1;
    }

    return 0;
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


int tcp_read_header(int socket, struct request *request) {
    int recved = read(socket, request, sizeof(struct request));
    if (recved != sizeof(struct request)) {
        return -1;
    }
    return recved;
}