//
// Created by jtroo on 15-04-21.
//

#include <signal.h>
#include <netinet/tcp.h>

#include "uc_server_utils.h"
#include "rdma_common.h"

int setup_uc_client_resources(struct uc_client_connection *client) {
    int ret = -1;

    // Create protection domain for this RNIC
    client->pd = ibv_alloc_pd(client->context);
    check(!client->pd, -errno, "Failed to allocate a protection domain errno: %d\n",
          -errno);

    pr_debug("A new protection domain is allocated at %p \n", client->pd);

    // Give the client a completion channel, used for completion queue
    client->io_completion_channel = ibv_create_comp_channel(client->context);
    check(!client->io_completion_channel, -errno,
          "Failed to create an I/O completion event channel, %d\n",
          -errno);

    pr_debug("An I/O completion event channel is created at %p \n",
             client->io_completion_channel);
    // Make a new completion queue
    client->cq = ibv_create_cq(client->context /* which device*/,
                               CQ_CAPACITY /* maximum capacity*/,
                               NULL /* user context, not used here */,
                               client->io_completion_channel /* which IO completion channel */,
                               0 /* signaling vector, not used here*/);
    check(!client->cq, -errno, "Failed to create a completion queue (cq), errno: %d\n",
          -errno);

    pr_debug("Completion queue (CQ) is created at %p with %d elements \n",
             client->cq, client->cq->cqe);
    // Receive notifications, this is required when waiting for completion event
    ret = ibv_req_notify_cq(client->cq /* on which CQ */,
                            0 /* 0 = all event type, no filter*/);
    check(ret, -errno, "Failed to request notifications on CQ errno: %d \n",
          -errno);

    // Create a QP with the following parameters
    bzero(&client->qp_init_attr, sizeof client->qp_init_attr);
    client->qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
    client->qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
    client->qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
    client->qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
    client->qp_init_attr.qp_type = IBV_QPT_UC; /* QP type, UC = Unreliable connection */
    /* We use same completion queue, but one can use different queues */
    client->qp_init_attr.recv_cq = client->cq; /* Where should I notify for receive completion operations */
    client->qp_init_attr.send_cq = client->cq; /* Where should I notify for send completion operations */
    /*
     * Lets create a QP
     * Has to be done in an old fashioned way for UC, strangely does not work with CM
     */
    client->client_qp = ibv_create_qp(client->pd, &client->qp_init_attr);
    check(client->client_qp == NULL, -errno, "Failed to create QP due to errno: %d\n", -errno);
    uc_set_init_qp(client->client_qp);

    // Used when sending info to client
    client->local_qp_attrs.gid_global_interface_id = client->server_gid.global.interface_id;
    client->local_qp_attrs.gid_global_subnet_prefix = client->server_gid.global.subnet_prefix;
    client->local_qp_attrs.lid = get_local_lid(client->context);
    client->local_qp_attrs.qpn = client->client_qp->qp_num;
    client->local_qp_attrs.psn = lrand48() & 0xffffff;

    pr_debug("Client QP created at %p\n", client->client_qp);
    return ret;
}

int init_uc_server(struct server_info *server) {
    int ret = -1;
    // Grab the information for this RNIC
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    struct ibv_device *ib_dev = dev_list[0];
    server->uc_server_info->context = ibv_open_device(ib_dev);

    ibv_query_gid(server->uc_server_info->context, IB_PHYS_PORT, 0, &server->uc_server_info->server_gid);

    // Set up TCP server to accept new clients, and exchange RDMA metadata
    int option = 1;
    /* TCP connection */
    server->uc_server_info->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->uc_server_info->socket_fd == -1) {
        perror("socket cannot be created\n");
        exit(EXIT_FAILURE);
    }

    /* if the port is busy and in the TIME_WAIT state, reuse it anyway. */
    if (setsockopt(server->uc_server_info->socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &option,
                   sizeof(option)) < 0) {
        close(server->uc_server_info->socket_fd);
        perror("setsockopt failed\n");
        exit(EXIT_FAILURE);
    }

    if ((bind(server->uc_server_info->socket_fd, (struct sockaddr *) &server->addr,
              sizeof(server->addr))) < 0) {
        perror("address and port binding failed\n");
        exit(EXIT_FAILURE);
    }

    socklen_t addr_len = sizeof(server->addr);
    if (getsockname(server->uc_server_info->socket_fd, (struct sockaddr *) &server->addr, &addr_len)
        == -1) {
        perror("address and port binding failed\n");
        exit(EXIT_FAILURE);
    }

    pr_info("[%s] Pid:%d bind on socket:%d Port:%d\n", SERVER,
            (int) getpid(), server->uc_server_info->socket_fd, ntohs(server->addr.sin_port));

    if (listen(server->uc_server_info->socket_fd, BACKLOG) < 0) {
        perror("Cannot listen on socket");
        exit(EXIT_FAILURE);
    }

    pr_info("Listening socket n. %d\n", server->uc_server_info->socket_fd);
    signal(SIGPIPE, SIG_IGN);


    printf("Server is listening successfully at: %s , port: %d \n",
           inet_ntoa(server->addr.sin_addr),
           ntohs(server->addr.sin_port));

    return ret;
}

int uc_accept_new_connection(struct server_info *server, struct client_info *client) {
    struct sockaddr_in remote_sockaddr;
    int ret = -1;

    // Pass along server data
    client->uc_client->server_gid = server->uc_server_info->server_gid;
    client->uc_client->context = server->uc_server_info->context;
    setup_uc_client_resources(client->uc_client);

    // Register the request buffer area
    client->uc_client->request_mr = rdma_buffer_register(client->uc_client->pd /* which protection domain */,
                                                      client->request/* what memory */,
                                                      sizeof(struct request)*REQUEST_BACKLOG /* what length */,
                                                      (IBV_ACCESS_LOCAL_WRITE |
                                                  IBV_ACCESS_REMOTE_READ | // TODO Remove this permission?
                                                  IBV_ACCESS_REMOTE_WRITE) /* access permissions */);
    check(!client->uc_client->request_mr, -ENOMEM, "Failed to register client attr buffer\n", -ENOMEM);

    // Pre-post all these requests
    for (int i = 0; i < REQUEST_BACKLOG; i++) {
        client->request_count = i;
        ret = uc_post_receive_request(client);
        check(ret, ret, "Failed to pre-post the receive buffer %d, errno: %d \n", i, ret);
    }
    client->request_count = 0;

    // Register response buffer
    client->uc_client->response_mr = rdma_buffer_register(client->uc_client->pd,
                                                                  client->response,
                                                          sizeof(struct response),
                                                          IBV_ACCESS_LOCAL_WRITE);
    check(!client->uc_client->response_mr, ret, "Failed to register the client metadata buffer, ret = %d \n", ret);

    pr_info("Receive buffer pre-posting is successful \n");


    /*
     * Accept incoming client and exchange metadata
     */
    struct qp_attr remote_qp_attrs;

    int nodelay = 1;
    socklen_t addrlen = sizeof(server->addr);

    if ((client->uc_client->socket_fd =
                 accept(server->uc_server_info->socket_fd, (struct sockaddr *) &server->addr,
                        &addrlen)) < 0) {
        error("Cannot accept new connection\n");
        return -1;
    }

    if (setsockopt
                (client->uc_client->socket_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay,
                 sizeof(nodelay)) < 0) {
        perror("setsockopt TCP_NODELAT");
        return -1;
    }

    pr_info("set sock UD, going to read\n");

    ret = read(client->uc_client->socket_fd, &remote_qp_attrs,
               sizeof(struct qp_attr));
    if (ret < 0) {
        pr_debug("Could not read queue pair attributes from socket\n");
    }
    pr_info("Got qp attributes from client\n");

    ret = write(client->uc_client->socket_fd, &client->uc_client->local_qp_attrs, sizeof(struct qp_attr));
    if (ret < 0) {
        pr_debug("Could not write queue pair attributes to client\n");
    }
    pr_info("Sent qp attributes to client\n");

    // Cannot be done with CM?
    ret = connect_qp(client->uc_client->client_qp, &client->uc_client->local_qp_attrs, &remote_qp_attrs);
    check(ret, ret, "Could not connect qp\n", NULL);

    return ret;
}

int uc_receive_header(struct client_info *client) {
    int ret = 0;
    struct ibv_wc wc;
    ret = process_work_completion_events(client->uc_client->io_completion_channel, &wc, 1, client->uc_client->cq, NULL,
                                         client->blocking);
    check(ret < 0, -errno, "Failed to receive header: %d\n", ret);
    pr_info("wc wr id: %lu, request count: %d\n", wc.wr_id, client->request_count);
    return ret;
}

int uc_post_receive_request(struct client_info *client) {
    int ret = 0;
    pr_info("receiving %d\n", client->request_count);
    ret = post_recieve(sizeof(struct request), client->uc_client->request_mr->lkey, client->request_count, client->uc_client->client_qp,
                       &client->request[client->request_count]);
    return ret;
}

int uc_send_response(struct client_info *client) {
    int ret = -1;
    struct ibv_wc wc;

    ret = post_send(sizeof(struct response), client->uc_client->response_mr->lkey, 0, client->uc_client->client_qp, client->response);
    ret = process_work_completion_events(client->uc_client->io_completion_channel, &wc, 1, client->uc_client->cq, NULL,
                                         client->blocking);
    check(ret < 0, -errno, "Failed to send response: %d\n", ret);
    return ret;
}