//
// Created by jtroo on 23-04-21.
//

#include "uc_client_utils.h"

/* This function prepares client side connection resources for an RDMA connection */
int something_client_prepare_connection(struct uc_server_conn *server_conn) {
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    /*  Open a channel used to report asynchronous communication event */
    server_conn->cm_event_channel = rdma_create_event_channel();
    check(!server_conn->cm_event_channel, -errno, "Creating cm event channel failed, errno: %d \n", -errno);
    pr_debug("RDMA CM event channel is created at : %p \n", server_conn->cm_event_channel);
    /* rdma_cm_id is the connection identifier (like socket) which is used
     * to define an RDMA connection.
     */
    ret = rdma_create_id(server_conn->cm_event_channel, &server_conn->cm_client_id,
                         NULL,
                         RDMA_PS_TCP);
    check(ret, -errno, "Creating cm id failed with errno: %d \n", -errno);
    /* Resolve destination and optional source addresses from IP addresses  to
     * an RDMA address.  If successful, the specified rdma_cm_id will be bound
     * to a local device. */
    ret = rdma_resolve_addr(server_conn->cm_client_id, NULL, (struct sockaddr *) server_conn->server_sockaddr, 2000);

    check(ret, -errno, "Failed to resolve address, errno: %d \n", -errno);
    pr_debug("waiting for cm event: RDMA_CM_EVENT_ADDR_RESOLVED\n");
    ret = process_rdma_cm_event(server_conn->cm_event_channel,
                                RDMA_CM_EVENT_ADDR_RESOLVED,
                                &cm_event);
    check(ret, ret, "Failed to receive a valid event, ret = %d \n", ret);

    /* we ack the event */
    ret = rdma_ack_cm_event(cm_event);
    check(ret, -errno, "Failed to acknowledge the CM event, errno: %d\n", -errno);
    pr_debug("RDMA address is resolved \n");

    /* Resolves an RDMA route to the destination address in order to
     * establish a connection */
    ret = rdma_resolve_route(server_conn->cm_client_id, 2000);
    check(ret, -errno, "Failed to resolve route, erno: %d \n", -errno);
    pr_debug("waiting for cm event: RDMA_CM_EVENT_ROUTE_RESOLVED\n");
    ret = process_rdma_cm_event(server_conn->cm_event_channel,
                                RDMA_CM_EVENT_ROUTE_RESOLVED,
                                &cm_event);
    check(ret, ret, "Failed to receive a valid event, ret = %d \n", ret);
    /* we ack the event */
    ret = rdma_ack_cm_event(cm_event);
    check(ret, -errno, "Failed to acknowledge the CM event, errno: %d \n", -errno);

    printf("Trying to connect to server at : %s port: %d \n",
           inet_ntoa(server_conn->server_sockaddr->sin_addr),
           ntohs(server_conn->server_sockaddr->sin_port));
    /* Protection Domain (PD) is similar to a "process abstraction"
     * in the operating system. All resources are tied to a particular PD.
     * And accessing recourses across PD will result in a protection fault.
     */
    server_conn->pd = ibv_alloc_pd(server_conn->cm_client_id->verbs);
    check(!server_conn->pd, -errno, "Failed to alloc pd, errno: %d \n", -errno);
    pr_debug("pd allocated at %p \n", server_conn->pd);


    pr_debug("here\n");
        /* Now we need a completion channel, were the I/O completion
         * notifications are sent. Remember, this is different from connection
         * management (CM) event notifications.
         * A completion channel is also tied to an RDMA device, hence we will
         * use cm_client_id->verbs.
         */
    server_conn->io_completion_channel = ibv_create_comp_channel(server_conn->cm_client_id->verbs);
    check(!server_conn->io_completion_channel, -errno, "Failed to create IO completion event channel, errno: %d\n",
          -errno);
    pr_debug("completion event channel created at : %p \n", server_conn->io_completion_channel);
    /* Now we create a completion queue (CQ) where actual I/O
     * completion metadata is placed. The metadata is packed into a structure
     * called struct ibv_wc (wc = work completion). ibv_wc has detailed
     * information about the work completion. An I/O request in RDMA world
     * is called "work" ;)
     */
    server_conn->client_cq = ibv_create_cq(server_conn->cm_client_id->verbs /* which device*/,
                                           CQ_CAPACITY /* maximum capacity*/,
                                           NULL /* user context, not used here */,
                                           server_conn->io_completion_channel /* which IO completion channel */,
                                           0 /* signaling vector, not used here*/);
    check(!server_conn->client_cq, -errno, "Failed to create CQ, errno: %d \n", -errno);
    pr_debug("CQ created at %p with %d elements \n", server_conn->client_cq, server_conn->client_cq->cqe);

    ret = ibv_req_notify_cq(server_conn->client_cq, 0);
    check(ret, -errno, "Failed to request notifications, errno: %d\n", -errno);

    /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
      * The capacity here is define statically but this can be probed from the
  * device. We just use a small number as defined in rdma_common.h */
    bzero(&server_conn->qp_init_attr, sizeof server_conn->qp_init_attr);
    server_conn->qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
    server_conn->qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
    server_conn->qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
    server_conn->qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
    server_conn->qp_init_attr.qp_type = IBV_QPT_UC; /* QP type, UC = Unreliable connection */
    /* We use same completion queue, but one can use different queues */
    server_conn->qp_init_attr.recv_cq = server_conn->client_cq; /* Where should I notify for receive completion operations */
    server_conn->qp_init_attr.send_cq = server_conn->client_cq; /* Where should I notify for send completion operations */
    /*Lets create a QP */
    ret = rdma_create_qp(server_conn->cm_client_id /* which connection id */,
                         server_conn->pd /* which protection domain*/,
                         &server_conn->qp_init_attr /* Initial attributes */);
    check(ret, -errno, "Failed to create QP, errno: %d \n", -errno);

    server_conn->client_qp = server_conn->cm_client_id->qp;
    pr_debug("QP created at %p \n", server_conn->client_qp);

    server_conn->client_request_mr = rdma_buffer_register(server_conn->pd,
                                                          server_conn->request,
                                                          sizeof(struct request),
                                                          (IBV_ACCESS_LOCAL_WRITE |
                                                           IBV_ACCESS_REMOTE_READ |
                                                           IBV_ACCESS_REMOTE_WRITE));
    uc_pre_post_receive_response(server_conn, server_conn->response);

    return 0;
}

/* Connects to the RDMA server */
int something_client_connect_to_server(struct uc_server_conn *server_conn) {
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    bzero(&conn_param, sizeof(conn_param));
    conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;
    conn_param.retry_count = 3; // if fail, then how many times to retry
    ret = rdma_connect(server_conn->cm_client_id, &conn_param);
    check(ret, -errno, "Failed to connect to remote host , errno: %d\n", -errno);

    pr_debug("waiting for cm event: RDMA_CM_EVENT_ESTABLISHED\n");
    ret = process_rdma_cm_event(server_conn->cm_event_channel,
                                RDMA_CM_EVENT_ESTABLISHED,
                                &cm_event);
    check(ret, ret, "Failed to get cm event, ret = %d \n", ret);

    ret = rdma_ack_cm_event(cm_event);
    check(ret, -errno, "Failed to acknowledge cm event, errno: %d\n", -errno);

    printf("The client is connected successfully \n");
    return 0;
}

int something_send_request(struct uc_server_conn *server_conn, struct request *request) {
    int ret;
    struct ibv_wc wc;


    ret = post_send(sizeof(struct request), server_conn->client_request_mr->lkey, 1, server_conn->client_qp,
                    request);

    check(ret, -errno, "Failed to send request, errno: %d \n", -errno);

    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(server_conn->io_completion_channel,
                                         &wc, 1, server_conn->client_cq);
    check(ret != 1, ret, "We failed to get 1 work completions , ret = %d \n",
          ret);

    return 0;
}

int uc_pre_post_receive_response(struct uc_server_conn *server_conn, struct response *response) {
    int ret;

    server_conn->client_response_mr = rdma_buffer_register(server_conn->pd,
                                                           server_conn->response,
                                                           sizeof(struct response),
                                                           (IBV_ACCESS_LOCAL_WRITE |
                                                            IBV_ACCESS_REMOTE_READ |
                                                            IBV_ACCESS_REMOTE_WRITE));

    ret = post_recieve(sizeof(struct response), server_conn->client_response_mr->lkey, 0, server_conn->client_qp,
                       server_conn->response);

    check(ret, -errno, "Failed to recv response, errno: %d \n", -errno);
}

int something_receive_response(struct uc_server_conn *server_conn, struct response *response) {
    int ret;
    struct ibv_wc wc;

    ret = post_recieve(sizeof(struct response), server_conn->client_response_mr->lkey, 0, server_conn->client_qp,
                       server_conn->response);
    check(ret, -errno, "Failed to recv response, errno: %d \n", -errno);

    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(server_conn->io_completion_channel,
                                         &wc, 1, server_conn->client_cq);
    check(ret != 1, ret, "We failed to get 1 work completions , ret = %d \n",
          ret);

    return ret;
}

/* This function disconnects the RDMA connection from the server and cleans up
 * all the resources.
 */
int uc_client_disconnect_and_clean(struct uc_server_conn *server_conn) {
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    /* active disconnect from the client side */
    ret = rdma_disconnect(server_conn->cm_client_id);
    if (ret) {
        error("Failed to disconnect, errno: %d \n", -errno);
        //continuing anyways
    }
    ret = process_rdma_cm_event(server_conn->cm_event_channel,
                                RDMA_CM_EVENT_DISCONNECTED,
                                &cm_event);
    if (ret) {
        error("Failed to get RDMA_CM_EVENT_DISCONNECTED event, ret = %d\n",
              ret);
        //continuing anyways
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        error("Failed to acknowledge cm event, errno: %d\n",
              -errno);
        //continuing anyways
    }
    /* Destroy QP */
    rdma_destroy_qp(server_conn->cm_client_id);
    /* Destroy client cm id */
    ret = rdma_destroy_id(server_conn->cm_client_id);
    if (ret) {
        error("Failed to destroy client id cleanly, %d \n", -errno);
        // we continue anyways;
    }
    /* Destroy CQ */
    ret = ibv_destroy_cq(server_conn->client_cq);
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
    rdma_destroy_event_channel(server_conn->cm_event_channel);
    printf("Client resource clean up is complete \n");
    return 0;
}

int uc_main(char *key, struct sockaddr_in *server_sockaddr) {}
//    int ret;
//    struct uc_server_conn *server_conn = calloc(1, sizeof(struct uc_server_conn));
//    server_conn->server_sockaddr = server_sockaddr;
//
//    ret = uc_client_prepare_connection(server_conn);
//    check(ret, ret, "Failed to setup client connection , ret = %d \n", ret);
//
//    ret = uc_client_connect_to_server(server_conn);
//    check(ret, ret, "Failed to setup client connection , ret = %d \n", ret);
//
//    server_conn->request = allocate_request();
//    bzero(server_conn->request, sizeof(struct request));
//    strncpy(server_conn->request->key, "testing", KEY_SIZE);
//    server_conn->request->key_len = strlen(server_conn->request->key);
//    server_conn->request->method = SET;
//    server_conn->request->msg_len = strlen("hello server");
//    strncpy(server_conn->request->msg, "hello server", MSG_SIZE);
//
//    print_request(server_conn->request);
//    ret = uc_send_request(server_conn, server_conn->request);
//    check(ret, ret, "Failed to get send request, ret = %d \n", ret);
//
//    server_conn->response = malloc(sizeof(struct response));
//    bzero(server_conn->response, sizeof(struct response));
//    ret = uc_receive_response(server_conn, server_conn->response);
//    print_response(server_conn->response);
//    check(ret, ret, "Failed to receive response, ret = %d \n", ret);
//
//    sleep(5);
//
//    server_conn->request = allocate_request();
//    bzero(server_conn->request, sizeof(struct request));
//    strncpy(server_conn->request->key, "testing", KEY_SIZE);
//    server_conn->request->key_len = strlen(server_conn->request->key);
//    server_conn->request->method = GET;
//    print_request(server_conn->request);
//
//    ret = uc_send_request(server_conn, server_conn->request);
//    check(ret, ret, "Failed to get send second request, ret = %d \n", ret);
//
//    bzero(server_conn->response, sizeof(struct response));
//    ret = uc_receive_response(server_conn, server_conn->response);
//    print_response(server_conn->response);
//    check(ret, ret, "Failed to receive second response ret = %d \n", ret);
//
//    ret = uc_client_disconnect_and_clean(server_conn);
//    check(ret, ret, "Failed to cleanly disconnect and clean up resources \n", ret);
//
//    return ret;
//}