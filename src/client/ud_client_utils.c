//
// Created by jtroo on 21-05-21.
//

#include "ud_client_utils.h"

/* This function prepares client side connection resources for an RDMA connection */
int ud_prepare_client(struct ud_server_conn *server_conn) {
    int ret = -1;

    struct ibv_device **dev_list = ibv_get_device_list(NULL);
    struct ibv_device *ib_dev = dev_list[0];
    struct ibv_context *context = ibv_open_device(ib_dev);

    server_conn->pd = ibv_alloc_pd(context);
    check(!server_conn->pd, -errno, "Failed to allocate a protection domain errno: %d\n",
          -errno);

    pr_debug("A new protection domain is allocated at %p \n", server_conn->pd);
    /* Now we need a completion channel, were the I/O completion
     * notifications are sent. Remember, this is different from connection
     * management (CM) event notifications.
     * A completion channel is also tied to an RDMA device, hence we will
     * use cm_client_id->verbs.
     */
    server_conn->io_completion_channel = ibv_create_comp_channel(context);
    check(!server_conn->io_completion_channel, -errno,
          "Failed to create an I/O completion event channel, %d\n",
          -errno);

    pr_debug("An I/O completion event channel is created at %p \n",
             server_conn->io_completion_channel);
    /* Now we create a completion queue (CQ) where actual I/O
     * completion metadata is placed. The metadata is packed into a structure
     * called struct ibv_wc (wc = work completion). ibv_wc has detailed
     * information about the work completion. An I/O request in RDMA world
     * is called "work" ;)
     */
    server_conn->ud_cq = ibv_create_cq(context /* which device*/,
                                                  CQ_CAPACITY /* maximum capacity*/,
                                                  NULL /* user context, not used here */,
                                                  server_conn->io_completion_channel /* which IO completion channel */,
                                                  0 /* signaling vector, not used here*/);
    check(!server_conn->ud_cq, -errno, "Failed to create a completion queue (cq), errno: %d\n",
          -errno);

    pr_debug("Completion queue (CQ) is created at %p with %d elements \n",
             server_conn->ud_cq, server_conn->ud_cq->cqe);
    /* Ask for the event for all activities in the completion queue*/
    ret = ibv_req_notify_cq(server_conn->ud_cq /* on which CQ */,
                            0 /* 0 = all event type, no filter*/);
    check(ret, -errno, "Failed to request notifications on CQ errno: %d \n",
          -errno);

    /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
     * The capacity here is define statically but this can be probed from the
     * device. We just use a small number as defined in rdma_common.h */
    bzero(&server_conn->qp_init_attr, sizeof server_conn->qp_init_attr);
    server_conn->qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
    server_conn->qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
    server_conn->qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
    server_conn->qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
    server_conn->qp_init_attr.qp_type = IBV_QPT_UD; /* QP type, UD = Unreliable datagram */
    /* We use same completion queue, but one can use different queues */
    server_conn->qp_init_attr.recv_cq = server_conn->ud_cq; /* Where should I notify for receive completion operations */
    server_conn->qp_init_attr.send_cq = server_conn->ud_cq; /* Where should I notify for send completion operations */
    /*Lets create a QP */
//    ret = rdma_create_qp(server_conn->cm_client_id /* which connection id TODO change this to ibv_create_qp*/,
//                         server_conn->pd /* which protection domain*/,
//                         &server_conn->qp_init_attr /* Initial attributes */);
//    check(ret, -errno, "Failed to create QP due to errno: %d\n", -errno);
//
//    /* Save the reference for handy typing but is not required */
//    server_conn->ud_qp = server_conn->cm_client_id->qp;
//    pr_debug("Client QP created at %p\n", server_conn->ud_qp);

    server_conn->ud_qp = ibv_create_qp(server_conn->pd, &server_conn->qp_init_attr);
    check(server_conn->ud_qp == NULL, -errno, "Failed to create QP due to errno: %d\n", -errno);

    ud_set_init_qp(server_conn->ud_qp);

    ibv_query_gid(context, IB_PHYS_PORT, 0, &server_conn->server_gid);
    server_conn->local_dgram_qp_attrs.gid_global_interface_id = server_conn->server_gid.global.interface_id;
    server_conn->local_dgram_qp_attrs.gid_global_subnet_prefix = server_conn->server_gid.global.subnet_prefix;
    server_conn->local_dgram_qp_attrs.lid = get_local_lid(context);
    server_conn->local_dgram_qp_attrs.qpn = server_conn->ud_qp->qp_num;
    server_conn->local_dgram_qp_attrs.psn = lrand48() & 0xffffff;


    server_conn->client_request_mr = rdma_buffer_register(server_conn->pd,
                                                          server_conn->request,
                                                          sizeof(struct request),
                                                          (IBV_ACCESS_LOCAL_WRITE |
                                                           IBV_ACCESS_REMOTE_READ |
                                                           IBV_ACCESS_REMOTE_WRITE));
    check(!server_conn->client_request_mr, -ENOMEM, "Failed to register client attr buffer\n", -ENOMEM);
    ud_pre_post_receive_response(server_conn, server_conn->response);


    return 0;
}

/* Connects to the RDMA server */
int ud_client_connect_to_server(struct ud_server_conn *server_conn) {
    server_conn->socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    check(server_conn->socket_fd < 0, -1, "Socket could not be made: %d\n", server_conn->socket_fd);

    pr_info("connecting\n");
//  Check if a connection can be made
    check(connect(server_conn->socket_fd, (struct sockaddr *) server_conn->server_sockaddr, sizeof(struct sockaddr_in)), -1, "Connection failed!\n", NULL);
    pr_info("connected\n");
    if (write(server_conn->socket_fd, &server_conn->local_dgram_qp_attrs, sizeof(struct qp_attr)) < 0) {
        pr_debug("Could not write queue pair attributes to server\n");
    }

    if (read(server_conn->socket_fd, &server_conn->remote_dgram_qp_attrs, sizeof(struct qp_attr)) < 0) {
        pr_debug("Could not read servers queue pair attributes\n");
    }

    pr_info("%d %d %d\n", server_conn->remote_dgram_qp_attrs.lid, server_conn->remote_dgram_qp_attrs.qpn, server_conn->remote_dgram_qp_attrs.psn);

    struct ibv_ah_attr ah_attr;
    bzero(&ah_attr, sizeof ah_attr);
    ah_attr.dlid = server_conn->remote_dgram_qp_attrs.lid;
    ah_attr.port_num = IB_PHYS_PORT;

    server_conn->ah = ibv_create_ah(server_conn->pd, &ah_attr);
    check(!server_conn->ah, -1, "Could not create AH from the info given\n", NULL)

    ud_set_rts_qp(server_conn->ud_qp, server_conn->local_dgram_qp_attrs.psn);

    printf("The client is connected successfully \n");
    return 0;
}

int ud_send_request(struct ud_server_conn *server_conn, struct request *request) {
    int ret;
    struct ibv_wc wc;


    ret = ud_post_send(sizeof(struct request), server_conn->client_request_mr->lkey, 1, server_conn->ud_qp,
                       request, server_conn->ah, server_conn->remote_dgram_qp_attrs.qpn);

    check(ret, -errno, "Failed to send request, errno: %d \n", -errno);

    pr_info("Do we need this?\n");
    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(server_conn->io_completion_channel,
                                         &wc, 1, server_conn->ud_cq);
    check(ret != 1, ret, "We failed to get 1 work completions , ret = %d \n",
          ret);

    return 0;
}

int ud_pre_post_receive_response(struct ud_server_conn *server_conn, struct ud_response *response) {
    int ret;

    server_conn->client_response_mr = rdma_buffer_register(server_conn->pd,
                                                           server_conn->response,
                                                           sizeof(struct ud_response),
                                                           (IBV_ACCESS_LOCAL_WRITE |
                                                            IBV_ACCESS_REMOTE_READ |
                                                            IBV_ACCESS_REMOTE_WRITE));

    ret = post_recieve(sizeof(struct ud_response), server_conn->client_response_mr->lkey, 0, server_conn->ud_qp,
                       server_conn->response);

    check(ret, -errno, "Failed to recv response, errno: %d \n", -errno);

    return ret;
}

int ud_receive_response(struct ud_server_conn *server_conn, struct ud_response *response) {
    int ret;
    struct ibv_wc wc;

    ret = post_recieve(sizeof(struct ud_response), server_conn->client_response_mr->lkey, 0, server_conn->ud_qp,
                       server_conn->response);
    check(ret, -errno, "Failed to recv response, errno: %d \n", -errno);

    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(server_conn->io_completion_channel,
                                         &wc, 1, server_conn->ud_cq);
    check(ret != 1, ret, "We failed to get 1 work completions , ret = %d \n",
          ret);

    return 0;
}

/*
 * This function disconnects the RDMA connection from the server and cleans up
 * all the resources.
 */
int ud_client_disconnect_and_clean(struct ud_server_conn *server_conn) {
    int ret = -1;

    /* Destroy QP */
    rdma_destroy_qp(server_conn->cm_client_id);

    /* Destroy CQ */
    ret = ibv_destroy_cq(server_conn->ud_cq);
    if (ret) {
        error("Failed to destroy completion queue cleanly, %d \n", -errno);
        // we continue anyways;
    }
    /* Destroy completion channel */
    ret = ibv_destroy_comp_channel(server_conn->io_completion_channel);
    if (ret) {
        error("Failed to destroy completion channel cleanly, %d \n", -errno);
        // we continue anyways;
    }
    /* Destroy memory buffers */
    pr_info("client request\n");
    rdma_buffer_deregister(server_conn->client_request_mr);
    pr_info("client dst\n");
    /* We free the buffers */
    free(server_conn->request);
    /* Destroy protection domain */
    ret = ibv_dealloc_pd(server_conn->pd);
    if (ret) {
        error("Failed to destroy client protection domain cleanly, %d \n", -errno);
        // we continue anyways;
    }
    printf("Client resource clean up is complete \n");
    return 0;
}

int ud_main(char *key, struct sockaddr_in *server_sockaddr) {
    int ret;
    struct ud_server_conn *server_conn = calloc(1, sizeof(struct ud_server_conn));
    server_conn->server_sockaddr = server_sockaddr;
    server_conn->request = allocate_request();
    server_conn->response = malloc(sizeof(struct ud_response));

    int client_nr;
    if (strcmp(key, "boo") == 0) {
        client_nr = 1;
    }
    else {
        client_nr = 2;
    }

    ret = ud_prepare_client(server_conn);
    check(ret, ret, "Failed to setup client connection , ret = %d \n", ret);

    ret = ud_client_connect_to_server(server_conn);
    check(ret, ret, "Failed to setup client connection , ret = %d \n", ret);

    bzero(server_conn->request, sizeof(struct request));
    strncpy(server_conn->request->key, key, KEY_SIZE);
    server_conn->request->key_len = strlen(server_conn->request->key);
    server_conn->request->method = SET;
    server_conn->request->msg_len = strlen("hello server");
    strncpy(server_conn->request->msg, "hello server", MSG_SIZE);
    server_conn->request->client_id = client_nr;

    sleep(1);

    print_request(server_conn->request);
    ret = ud_send_request(server_conn, server_conn->request);
    check(ret, ret, "Failed to get send request, ret = %d \n", ret);

    pr_info("Sent!\n");
    bzero(server_conn->response, sizeof(struct ud_response));
    ret = ud_receive_response(server_conn, server_conn->response);
    print_response(&server_conn->response->response);
    check(ret, ret, "Failed to receive response, ret = %d \n", ret);

    sleep(5);

//    server_conn->request = allocate_request();
    bzero(server_conn->request, sizeof(struct request));
    strncpy(server_conn->request->key, key, KEY_SIZE);
    server_conn->request->key_len = strlen(server_conn->request->key);
    server_conn->request->method = GET;
    print_request(server_conn->request);
    server_conn->request->client_id = client_nr;

    ret = ud_send_request(server_conn, server_conn->request);
    check(ret, ret, "Failed to get send second request, ret = %d \n", ret);

    bzero(server_conn->response, sizeof(struct ud_response));
    ret = ud_receive_response(server_conn, server_conn->response);
    print_response(&server_conn->response->response);
    check(ret, ret, "Failed to receive second response ret = %d \n", ret);

    ret = ud_client_disconnect_and_clean(server_conn);
    check(ret, ret, "Failed to cleanly disconnect and clean up resources \n", ret);

    return ret;
}