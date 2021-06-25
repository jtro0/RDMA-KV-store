//
// Created by Jens on 28/05/2021.
//

#include <netinet/tcp.h>
#include <errno.h>
#include <string.h>
#include "tcp_client_utils.h"

/* This function prepares client side connection resources for an RDMA connection */
int tcp_client_prepare_connection(struct tcp_server_conn *server_conn) {
    server_conn->socket = socket(AF_INET, SOCK_STREAM, 0);
    check(server_conn->socket < 0, -1, "Socket could not be made: %d\n", server_conn->socket);

    server_conn->request = calloc(1, sizeof(struct request));
    server_conn->response = calloc(1, sizeof(struct response));

    return server_conn->socket;
}

/* Connects to the TCP server */
int tcp_client_connect_to_server(struct tcp_server_conn *server_conn) {
    int ret = -1;

    ret = connect(server_conn->socket, (struct sockaddr *) server_conn->server_sockaddr, sizeof(struct sockaddr_in));
    check(ret != 0, ret, "Could not connect\n", ret);

    return ret;
}

int tcp_send_request(struct tcp_server_conn *server_conn, struct request *request) {
    int ret;

    ret = write(server_conn->socket, server_conn->request, sizeof(struct request));
    check(ret <= 0, -errno, "Failed to send request, errno: %d \n", -errno);

    return ret;
}

int tcp_receive_response(struct tcp_server_conn *server_conn, struct response *response) {
    int ret;

    ret = read(server_conn->socket, response, sizeof(struct response));
    check(ret <= 0, -errno, "Failed to recv response, errno: %d \n", -errno);

    return ret;
}

/* This function disconnects the TCP connection from the server and cleans up
 * all the resources.
 */
int tcp_client_disconnect_and_clean(struct tcp_server_conn *server_conn) {
   int ret = -1;

   ret = close(server_conn->socket);
    /* We free the buffers */
    free(server_conn->request);
    free(server_conn->response);

    printf("Client resource clean up is complete \n");
    return ret;
}