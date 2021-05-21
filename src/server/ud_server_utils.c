//
// Created by Jens on 10/05/2021.
//

#include <signal.h>
#include <netinet/tcp.h>
#include <dlfcn.h>
#include "ud_server_utils.h"
#include "rdma_common.h"

int init_ud_server(struct server_info *server) {
    int ret = -1;

    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    struct ibv_device *ib_dev = dev_list[0];
    struct ibv_context *context = ibv_open_device(ib_dev);

    server->ud_server_info->pd = ibv_alloc_pd(context);
    check(!server->ud_server_info->pd, -errno, "Failed to allocate a protection domain errno: %d\n",
          -errno);

    pr_debug("A new protection domain is allocated at %p \n", server->ud_server_info->pd);
    /* Now we need a completion channel, were the I/O completion
     * notifications are sent. Remember, this is different from connection
     * management (CM) event notifications.
     * A completion channel is also tied to an RDMA device, hence we will
     * use cm_client_id->verbs.
     */
    server->ud_server_info->io_completion_channel = ibv_create_comp_channel(context);
    check(!server->ud_server_info->io_completion_channel, -errno,
          "Failed to create an I/O completion event channel, %d\n",
          -errno);

    pr_debug("An I/O completion event channel is created at %p \n",
             server->ud_server_info->io_completion_channel);
    /* Now we create a completion queue (CQ) where actual I/O
     * completion metadata is placed. The metadata is packed into a structure
     * called struct ibv_wc (wc = work completion). ibv_wc has detailed
     * information about the work completion. An I/O request in RDMA world
     * is called "work" ;)
     */
    server->ud_server_info->ud_cq = ibv_create_cq(context /* which device*/,
                               CQ_CAPACITY /* maximum capacity*/,
                               NULL /* user context, not used here */,
                                               server->ud_server_info->io_completion_channel /* which IO completion channel */,
                               0 /* signaling vector, not used here*/);
    check(!server->ud_server_info->ud_cq, -errno, "Failed to create a completion queue (cq), errno: %d\n",
          -errno);

    pr_debug("Completion queue (CQ) is created at %p with %d elements \n",
             server->ud_server_info->ud_cq, server->ud_server_info->ud_cq->cqe);
    /* Ask for the event for all activities in the completion queue*/
    ret = ibv_req_notify_cq(server->ud_server_info->ud_cq /* on which CQ */,
                            0 /* 0 = all event type, no filter*/);
    check(ret, -errno, "Failed to request notifications on CQ errno: %d \n",
          -errno);
    pr_info("Been here\n");
    /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
     * The capacity here is define statically but this can be probed from the
     * device. We just use a small number as defined in rdma_common.h */
    bzero(&server->ud_server_info->qp_init_attr, sizeof server->ud_server_info->qp_init_attr);
    pr_info("Am here\n");

    server->ud_server_info->qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
    server->ud_server_info->qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
    server->ud_server_info->qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
    server->ud_server_info->qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
    server->ud_server_info->qp_init_attr.qp_type = IBV_QPT_UD; /* QP type, UD = Unreliable datagram */
    /* We use same completion queue, but one can use different queues */
    server->ud_server_info->qp_init_attr.recv_cq = server->ud_server_info->ud_cq; /* Where should I notify for receive completion operations */
    server->ud_server_info->qp_init_attr.send_cq = server->ud_server_info->ud_cq; /* Where should I notify for send completion operations */
    /*Lets create a QP */
//    ret = rdma_create_qp(server->ud_server_info->cm_client_id /* which connection id TODO look here this is not needed*/,
//                         server->ud_server_info->pd /* which protection domain*/,
//                         &server->ud_server_info->qp_init_attr /* Initial attributes */);
    pr_info("Or here\n");

    server->ud_server_info->ud_qp = ibv_create_qp(server->ud_server_info->pd, &server->ud_server_info->qp_init_attr);
    check(server->ud_server_info->ud_qp == NULL, -errno, "Failed to create QP due to errno: %d\n", -errno);

    /* Save the reference for handy typing but is not required */
//    server->ud_server_info->ud_qp = server->ud_server_info->cm_client_id->qp;
//    pr_debug("Client QP created at %p\n", server->ud_server_info->ud_qp);
    pr_info("Also here\n");

    ud_set_init_qp(server->ud_server_info->ud_qp);
    pr_info("Maybe here\n");

    ibv_query_gid(context, IB_PHYS_PORT, 0, &server->ud_server_info->server_gid);
    server->ud_server_info->local_dgram_qp_attrs.gid_global_interface_id = server->ud_server_info->server_gid.global.interface_id;
    server->ud_server_info->local_dgram_qp_attrs.gid_global_subnet_prefix = server->ud_server_info->server_gid.global.subnet_prefix;
    server->ud_server_info->local_dgram_qp_attrs.lid = get_local_lid(context);
    server->ud_server_info->local_dgram_qp_attrs.qpn = server->ud_server_info->ud_qp->qp_num;
    server->ud_server_info->local_dgram_qp_attrs.psn = lrand48() & 0xffffff;

    server->ud_server_info->request = calloc(REQUEST_BACKLOG, sizeof(struct request));

    /* we prepare the receive buffer in which we will receive the client request*/
    server->ud_server_info->request_mr = rdma_buffer_register(server->ud_server_info->pd /* which protection domain */,
                                                         server->ud_server_info->request/* what memory */,
                                                         sizeof(struct request)*REQUEST_BACKLOG /* what length */,
                                                         (IBV_ACCESS_LOCAL_WRITE |
                                                          IBV_ACCESS_REMOTE_READ | // TODO Remove this permission?
                                                          IBV_ACCESS_REMOTE_WRITE) /* access permissions */);
    check(!server->ud_server_info->request_mr, -ENOMEM, "Failed to register client attr buffer\n", -ENOMEM);

    for (int i = 0; i < REQUEST_BACKLOG; i++) {
        server->ud_server_info->request_count = i;
        ret = ud_post_receive_request(server->ud_server_info);
        check(ret, ret, "Failed to pre-post the receive buffer %d, errno: %d \n", i, ret);
    }
    server->ud_server_info->request_count = 0;


    // Set up TCP server to accept new clients
    int option = 1;
    /* TCP connection */
    server->ud_server_info->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server->ud_server_info->socket_fd == -1) {
        perror("socket cannot be created\n");
        exit(EXIT_FAILURE);
    }

    /* if the port is busy and in the TIME_WAIT state, reuse it anyway. */
    if (setsockopt(server->ud_server_info->socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *) &option,
                   sizeof(option)) < 0) {
        close(server->ud_server_info->socket_fd);
        perror("setsockopt failed\n");
        exit(EXIT_FAILURE);
    }

    if ((bind(server->ud_server_info->socket_fd, (struct sockaddr *) &server->addr,
              sizeof(server->addr))) < 0) {
        perror("address and port binding failed\n");
        exit(EXIT_FAILURE);
    }

    socklen_t addr_len = sizeof(server->addr);
    if (getsockname(server->ud_server_info->socket_fd, (struct sockaddr *) &server->addr, &addr_len)
        == -1) {
        perror("address and port binding failed\n");
        exit(EXIT_FAILURE);
    }

    pr_info("[%s] Pid:%d bind on socket:%d Port:%d\n", SERVER,
            (int) getpid(), server->ud_server_info->socket_fd, ntohs(server->addr.sin_port));

    if (listen(server->ud_server_info->socket_fd, BACKLOG) < 0) {
        perror("Cannot listen on socket");
        exit(EXIT_FAILURE);
    }

    pr_info("Listening socket n. %d\n", server->ud_server_info->socket_fd);
    signal(SIGPIPE, SIG_IGN);

    pr_info("UD Server has been set up, ready to accept clients\n");

    return ret;
}

int ud_accept_new_connection(struct server_info *server, struct client_info *client) {
    int ret = -1;

    /* now we register the metadata memory */
    client->ud_client->response_mr = rdma_buffer_register(server->ud_server_info->pd,
                                                          client->response,
                                                          sizeof(struct response),
                                                          IBV_ACCESS_LOCAL_WRITE);
    check(!client->rc_client->response_mr, ret, "Failed to register the client metadata buffer, ret = %d \n", ret);

    int nodelay = 1;
    socklen_t addrlen = sizeof(server->addr);

    if ((client->ud_client->socket_fd =
                 accept(server->tcp_server_info->socket_fd, (struct sockaddr *) &server->addr,
                            &addrlen)) < 0) {
        error("Cannot accept new connection\n");
        return -1;
    }

    if (setsockopt
                (client->ud_client->socket_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay,
                 sizeof(nodelay)) < 0) {
        perror("setsockopt TCP_NODELAT");
        return -1;
    }

    if (read(client->ud_client->socket_fd, &server->ud_server_info->remote_dgram_qp_attrs[server->ud_server_info->client_counter++],
            sizeof(struct qp_attr)) < 0) {
        pr_debug("Could not read queue pair attributes from socket\n");
    }
    pr_info("Got qp attributes from client %d\n", server->ud_server_info->client_counter-1);

    if (write(client->ud_client->socket_fd, &server->ud_server_info->local_dgram_qp_attrs, sizeof(struct qp_attr)) < 0) {
        pr_debug("Could not write queue pair attributes to client\n");
    }
    pr_info("Sent qp attributes to client %d\n", server->ud_server_info->client_counter-1);

    printf("A new connection is accepted from\n");

    return ret;
}

int ud_receive_header(struct client_info *client);


// Might need to do UD stuff
int ud_post_receive_request(struct ud_server_info *server) {
    int ret = 0;
    pr_info("receiving %d\n", server->request_count);
    ret = post_recieve(sizeof(struct request), server->request_mr->lkey, server->request_count, server->ud_qp,
                       &server->request[server->request_count]);
    return ret;
}

int ud_send_response(struct client_info *client) {
    int ret = -1;
    struct ibv_wc wc;

    ret = post_send(sizeof(struct response), client->rc_client->response_mr->lkey, 0, client->rc_client->client_qp, client->response);
    ret = process_work_completion_events(client->rc_client->io_completion_channel, &wc, 1, client->rc_client->cq);
    check(ret < 0, -errno, "Failed to send response: %d\n", ret);
    return ret;
}