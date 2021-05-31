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

int tcp_main(char *key, struct sockaddr_in *server_sockaddr) {
    int ret;

    struct tcp_server_conn *server_conn = calloc(1, sizeof(struct tcp_server_conn));
    server_conn->server_sockaddr = server_sockaddr;

    ret = tcp_client_prepare_connection(server_conn);
    check(ret < 0, ret, "Failed to setup client connection , ret = %d \n", ret);

    ret = tcp_client_connect_to_server(server_conn);
    check(ret != 0, ret, "Failed to setup client connection , ret = %d \n", ret);

    bzero(server_conn->request, sizeof(struct request));
    strncpy(server_conn->request->key, "testing", KEY_SIZE);
    server_conn->request->key_len = strlen(server_conn->request->key);
    server_conn->request->method = SET;
    server_conn->request->msg_len = strlen("hello server");
    strncpy(server_conn->request->msg, "hello server", MSG_SIZE);

    print_request(server_conn->request);
    ret = tcp_send_request(server_conn, server_conn->request);
    check(ret <= 0, ret, "Failed to get send request, ret = %d \n", ret);

    bzero(server_conn->response, sizeof(struct response));
    ret = tcp_receive_response(server_conn, server_conn->response);
    print_response(server_conn->response);
    check(ret <= 0, ret, "Failed to receive response, ret = %d \n", ret);

    sleep(5);

    bzero(server_conn->request, sizeof(struct request));
    strncpy(server_conn->request->key, "testing", KEY_SIZE);
    server_conn->request->key_len = strlen(server_conn->request->key);
    server_conn->request->method = GET;
    print_request(server_conn->request);

    ret = tcp_send_request(server_conn, server_conn->request);
    check(ret <= 0, ret, "Failed to get send second request, ret = %d \n", ret);

    bzero(server_conn->response, sizeof(struct response));
    ret = tcp_receive_response(server_conn, server_conn->response);
    print_response(server_conn->response);
    check(ret <= 0, ret, "Failed to receive second response ret = %d \n", ret);

    ret = tcp_client_disconnect_and_clean(server_conn);
    check(ret, ret, "Failed to cleanly disconnect and clean up resources \n", ret);

    return ret;
}