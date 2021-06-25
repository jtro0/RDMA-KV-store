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

    // Get the info on RNIC
    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    struct ibv_device *ib_dev = dev_list[0];
    struct ibv_context *context = ibv_open_device(ib_dev);

    // Create an protection domain on this RNIC
    server->ud_server_info->pd = ibv_alloc_pd(context);
    check(!server->ud_server_info->pd, -errno, "Failed to allocate a protection domain errno: %d\n",
          -errno);

    pr_debug("A new protection domain is allocated at %p \n", server->ud_server_info->pd);

    // Completion channel for recv queue
    server->ud_server_info->io_completion_channel_recv = ibv_create_comp_channel(context);
    check(!server->ud_server_info->io_completion_channel_recv, -errno,
          "Failed to create an I/O completion event channel, %d\n",
          -errno);

    pr_debug("An I/O completion event channel is created at %p \n",
             server->ud_server_info->io_completion_channel_recv);

    // Compelition queue for recv
    server->ud_server_info->ud_recv_cq = ibv_create_cq(context /* which device*/,
                               MAX_CLIENTS+1 /* maximum capacity*/,
                               NULL /* user context, not used here */,
                                               server->ud_server_info->io_completion_channel_recv /* which IO completion channel */,
                               0 /* signaling vector, not used here*/);
    check(!server->ud_server_info->ud_recv_cq, -errno, "Failed to create a completion queue (cq), errno: %d\n",
          -errno);

    pr_debug("Completion queue (CQ) is created at %p with %d elements \n",
             server->ud_server_info->ud_recv_cq, server->ud_server_info->ud_recv_cq->cqe);
    // Notify on this queue
    ret = ibv_req_notify_cq(server->ud_server_info->ud_recv_cq /* on which CQ */,
                            0 /* 0 = all event type, no filter*/);
    check(ret, -errno, "Failed to request notifications on CQ errno: %d \n",
          -errno);



    // Repeat for a send queue
    server->ud_server_info->io_completion_channel_send = ibv_create_comp_channel(context);
    check(!server->ud_server_info->io_completion_channel_send, -errno,
          "Failed to create an I/O completion event channel, %d\n",
          -errno);

    pr_debug("An I/O completion event channel is created at %p \n",
             server->ud_server_info->io_completion_channel_send);

    server->ud_server_info->ud_send_cq = ibv_create_cq(context /* which device*/,
                                                  MAX_CLIENTS+1 /* maximum capacity*/,
                                                  NULL /* user context, not used here */,
                                                  server->ud_server_info->io_completion_channel_send /* which IO completion channel */,
                                                  0 /* signaling vector, not used here*/);
    check(!server->ud_server_info->ud_send_cq, -errno, "Failed to create a completion queue (cq), errno: %d\n",
          -errno);

    pr_debug("Completion queue (CQ) is created at %p with %d elements \n",
             server->ud_server_info->ud_send_cq, server->ud_server_info->ud_send_cq->cqe);
    ret = ibv_req_notify_cq(server->ud_server_info->ud_send_cq /* on which CQ */,
                            0 /* 0 = all event type, no filter*/);
    check(ret, -errno, "Failed to request notifications on CQ errno: %d \n",
          -errno);


    // Create a QP with the following parameters
    bzero(&server->ud_server_info->qp_init_attr, sizeof server->ud_server_info->qp_init_attr);
    pr_info("Am here\n");

    server->ud_server_info->qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
    server->ud_server_info->qp_init_attr.cap.max_recv_wr = MAX_CLIENTS; /* Maximum receive posting capacity */
    server->ud_server_info->qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
    server->ud_server_info->qp_init_attr.cap.max_send_wr = MAX_CLIENTS; /* Maximum send posting capacity */
    server->ud_server_info->qp_init_attr.qp_type = IBV_QPT_UD; /* QP type, UD = Unreliable datagram */
    /* We use same completion queue, but one can use different queues */
    server->ud_server_info->qp_init_attr.recv_cq = server->ud_server_info->ud_recv_cq; /* Where should I notify for receive completion operations */
    server->ud_server_info->qp_init_attr.send_cq = server->ud_server_info->ud_send_cq; /* Where should I notify for send completion operations */
    /*Lets create a QP old-schooled */
    server->ud_server_info->ud_qp = ibv_create_qp(server->ud_server_info->pd, &server->ud_server_info->qp_init_attr);
    check(server->ud_server_info->ud_qp == NULL, -errno, "Failed to create QP due to errno: %d\n", -errno);

    // Change state of QP
    ud_set_init_qp(server->ud_server_info->ud_qp);
    pr_info("Maybe here\n");

    // Store local metadata for client
    ibv_query_gid(context, IB_PHYS_PORT, 0, &server->ud_server_info->server_gid);
    server->ud_server_info->local_dgram_qp_attrs.gid_global_interface_id = server->ud_server_info->server_gid.global.interface_id;
    server->ud_server_info->local_dgram_qp_attrs.gid_global_subnet_prefix = server->ud_server_info->server_gid.global.subnet_prefix;
    server->ud_server_info->local_dgram_qp_attrs.lid = get_local_lid(context);
    server->ud_server_info->local_dgram_qp_attrs.qpn = server->ud_server_info->ud_qp->qp_num;
    server->ud_server_info->local_dgram_qp_attrs.psn = rand() & 0xffffff;

    pr_info("%d %d %d\n", server->ud_server_info->local_dgram_qp_attrs.lid, server->ud_server_info->local_dgram_qp_attrs.qpn, server->ud_server_info->local_dgram_qp_attrs.psn);
    server->ud_server_info->request = calloc(MAX_CLIENTS, sizeof(struct ud_request));

    // Register array of requests, up to MAX_CLIENTS
    server->ud_server_info->request_mr = rdma_buffer_register(server->ud_server_info->pd /* which protection domain */,
                                                         server->ud_server_info->request/* what memory */,
                                                         sizeof(struct ud_request)*MAX_CLIENTS /* what length */,
                                                         (IBV_ACCESS_LOCAL_WRITE |
                                                          IBV_ACCESS_REMOTE_READ | // TODO Remove this permission?
                                                          IBV_ACCESS_REMOTE_WRITE) /* access permissions */);
    check(!server->ud_server_info->request_mr, -ENOMEM, "Failed to register client attr buffer\n", -ENOMEM);

    // Pre-post these
    for (int i = 0; i < MAX_CLIENTS; i++) {
        server->ud_server_info->request_count = i;
        ret = ud_post_receive_request(server->ud_server_info);
        check(ret, ret, "Failed to pre-post the receive buffer %d, errno: %d \n", i, ret);
    }
    server->ud_server_info->request_count = 0;
    // Set QP state to Ready to Send
    ud_set_rts_qp(server->ud_server_info->ud_qp, server->ud_server_info->local_dgram_qp_attrs.psn);

    // Set up TCP server to accept new clients and exchange data with new clients
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

    pr_info("Registering response UD on client count %d\n", server->ud_server_info->client_counter);
    // A response buffer per worker thread
    client->ud_client->response_mr = rdma_buffer_register(server->ud_server_info->pd,
                                                          client->response,
                                                          sizeof(struct response),
                                                          IBV_ACCESS_LOCAL_WRITE);
    check(!client->ud_client->response_mr, ret, "Failed to register the client metadata buffer, ret = %d \n", ret);
    pr_info("Registered response UD\n");

    client->ud_client->ud_server = server->ud_server_info;

    client->ud_client->wc = calloc(1, sizeof(struct ibv_wc));

    int nodelay = 1;
    socklen_t addrlen = sizeof(server->addr);

    pr_info("Accepting UD\n");
    if ((client->ud_client->socket_fd =
                 accept(server->ud_server_info->socket_fd, (struct sockaddr *) &server->addr,
                            &addrlen)) < 0) {
        error("Cannot accept new connection\n");
        return -1;
    }
    pr_info("Accepted UD\n");

    if (setsockopt
                (client->ud_client->socket_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay,
                 sizeof(nodelay)) < 0) {
        perror("setsockopt TCP_NODELAT");
        return -1;
    }


    /*
     * Exchange metadata with client
     */
    ret = read(client->ud_client->socket_fd, &server->ud_server_info->remote_dgram_qp_attrs[server->ud_server_info->client_counter],
               sizeof(struct qp_attr));
    if (ret < 0) {
        pr_debug("Could not read queue pair attributes from socket\n");
    }
    pr_info("Got qp attributes from client %d\n", server->ud_server_info->client_counter);

    ret = write(client->ud_client->socket_fd, &server->ud_server_info->local_dgram_qp_attrs, sizeof(struct qp_attr));
    if (ret < 0) {
        pr_debug("Could not write queue pair attributes to client\n");
    }
    pr_info("Sent qp attributes to client %d\n", server->ud_server_info->client_counter);

    int client_id = server->ud_server_info->remote_dgram_qp_attrs[server->ud_server_info->client_counter].client_id;

    /*
     * Create a AH (route) for this client
     */
    struct ibv_ah_attr ah_attr;
    bzero(&ah_attr, sizeof ah_attr);
    ah_attr.dlid = server->ud_server_info->remote_dgram_qp_attrs[server->ud_server_info->client_counter].lid;
    ah_attr.port_num = IB_PHYS_PORT;

    server->ud_server_info->ah[client_id] = ibv_create_ah(server->ud_server_info->pd, &ah_attr);

    check(server->ud_server_info->ah[client_id] == NULL, -1, "Could not create AH from the info given\n", NULL)
    server->ud_server_info->client_counter++;
    printf("A new connection is accepted\n");

    return ret;
}

int ud_receive_header(struct client_info *client) {
    int ret = 0;
    bzero(client->ud_client->wc, sizeof(struct ibv_wc));
    client->ud_client->wc->wc_flags = IBV_WC_GRH;

    ret = process_work_completion_events(client->ud_client->ud_server->io_completion_channel_recv,
                                         client->ud_client->wc, 1, client->ud_client->ud_server->ud_recv_cq,
                                         &client->ud_client->ud_server->lock_proc_wc, client->blocking);
    pthread_mutex_lock(&client->ud_client->ud_server->lock);
    check(ret < 0, -errno, "Failed to receive header: %d\n", ret);
    pr_info("wc wr id: %lu, request count: %d\n", client->ud_client->wc->wr_id, client->request_count);

    return ret;
}


int ud_post_receive_request(struct ud_server_info *server) {
    int ret = 0;
    pr_info("receiving %d\n", server->request_count);
    ret = post_recieve(sizeof(struct ud_request), server->request_mr->lkey, server->request_count, server->ud_qp,
                       &server->request[server->request_count]);
    return ret;
}

int ud_send_response(struct client_info *client) {
    int ret = -1;
    struct ibv_wc wc;

    ret = ud_post_send(sizeof(struct response), client->ud_client->response_mr->lkey, 0, client->ud_client->ud_server->ud_qp, client->response,
                        client->ud_client->ud_server->ah[client->ud_client->client_handling], client->ud_client->ud_server->remote_dgram_qp_attrs[client->ud_client->client_handling].qpn);
    ret = process_work_completion_events(client->ud_client->ud_server->io_completion_channel_send, &wc, 1,
                                         client->ud_client->ud_server->ud_send_cq, NULL, client->blocking);
    check(ret < 0, -errno, "Failed to send response: %d\n", ret);
    return ret;
}