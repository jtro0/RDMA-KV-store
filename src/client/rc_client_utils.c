//
// Created by jtroo on 23-04-21.
//

#include "rc_client_utils.h"

/* This function prepares client side connection resources for an RDMA connection */
int client_prepare_connection(struct rc_server_conn *server_conn)
{
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    /*  Open a channel used to report asynchronous communication event */
    server_conn->cm_event_channel = rdma_create_event_channel();
    if (!server_conn->cm_event_channel) {
        rdma_error("Creating cm event channel failed, errno: %d \n", -errno);
        return -errno;
    }
    debug("RDMA CM event channel is created at : %p \n", cm_event_channel);
    /* rdma_cm_id is the connection identifier (like socket) which is used
     * to define an RDMA connection.
     */
    ret = rdma_create_id(server_conn->cm_event_channel, &server_conn->cm_client_id,
                         NULL,
                         RDMA_PS_TCP);
    if (ret) {
        rdma_error("Creating cm id failed with errno: %d \n", -errno);
        return -errno;
    }
    /* Resolve destination and optional source addresses from IP addresses  to
     * an RDMA address.  If successful, the specified rdma_cm_id will be bound
     * to a local device. */
    ret = rdma_resolve_addr(server_conn->cm_client_id, NULL, (struct sockaddr*) server_conn->server_sockaddr, 2000);
    if (ret) {
        rdma_error("Failed to resolve address, errno: %d \n", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ADDR_RESOLVED\n");
    ret  = process_rdma_cm_event(server_conn->cm_event_channel,
                                 RDMA_CM_EVENT_ADDR_RESOLVED,
                                 &cm_event);
    if (ret) {
        rdma_error("Failed to receive a valid event, ret = %d \n", ret);
        return ret;
    }
    /* we ack the event */
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the CM event, errno: %d\n", -errno);
        return -errno;
    }
    debug("RDMA address is resolved \n");

    /* Resolves an RDMA route to the destination address in order to
     * establish a connection */
    ret = rdma_resolve_route(server_conn->cm_client_id, 2000);
    if (ret) {
        rdma_error("Failed to resolve route, erno: %d \n", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ROUTE_RESOLVED\n");
    ret = process_rdma_cm_event(server_conn->cm_event_channel,
                                RDMA_CM_EVENT_ROUTE_RESOLVED,
                                &cm_event);
    if (ret) {
        rdma_error("Failed to receive a valid event, ret = %d \n", ret);
        return ret;
    }
    /* we ack the event */
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge the CM event, errno: %d \n", -errno);
        return -errno;
    }
    printf("Trying to connect to server at : %s port: %d \n",
           inet_ntoa(server_conn->server_sockaddr->sin_addr),
           ntohs(server_conn->server_sockaddr->sin_port));
    /* Protection Domain (PD) is similar to a "process abstraction"
     * in the operating system. All resources are tied to a particular PD.
     * And accessing recourses across PD will result in a protection fault.
     */
    server_conn->pd = ibv_alloc_pd(server_conn->cm_client_id->verbs);
    if (!server_conn->pd) {
        rdma_error("Failed to alloc pd, errno: %d \n", -errno);
        return -errno;
    }
    debug("pd allocated at %p \n", pd);
    /* Now we need a completion channel, were the I/O completion
     * notifications are sent. Remember, this is different from connection
     * management (CM) event notifications.
     * A completion channel is also tied to an RDMA device, hence we will
     * use cm_client_id->verbs.
     */
    server_conn->io_completion_channel = ibv_create_comp_channel(server_conn->cm_client_id->verbs);
    if (!server_conn->io_completion_channel) {
        rdma_error("Failed to create IO completion event channel, errno: %d\n",
                   -errno);
        return -errno;
    }
    debug("completion event channel created at : %p \n", io_completion_channel);
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
    if (!server_conn->client_cq) {
        rdma_error("Failed to create CQ, errno: %d \n", -errno);
        return -errno;
    }
    debug("CQ created at %p with %d elements \n", client_cq, client_cq->cqe);
    ret = ibv_req_notify_cq(server_conn->client_cq, 0);
    if (ret) {
        rdma_error("Failed to request notifications, errno: %d\n", -errno);
        return -errno;
    }
    /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
      * The capacity here is define statically but this can be probed from the
  * device. We just use a small number as defined in rdma_common.h */
    bzero(&server_conn->qp_init_attr, sizeof server_conn->qp_init_attr);
    server_conn->qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
    server_conn->qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
    server_conn->qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
    server_conn->qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
    server_conn->qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
    /* We use same completion queue, but one can use different queues */
    server_conn->qp_init_attr.recv_cq = server_conn->client_cq; /* Where should I notify for receive completion operations */
    server_conn->qp_init_attr.send_cq = server_conn->client_cq; /* Where should I notify for send completion operations */
    /*Lets create a QP */
    ret = rdma_create_qp(server_conn->cm_client_id /* which connection id */,
                         server_conn->pd /* which protection domain*/,
                         &server_conn->qp_init_attr /* Initial attributes */);
    if (ret) {
        rdma_error("Failed to create QP, errno: %d \n", -errno);
        return -errno;
    }
    server_conn->client_qp = server_conn->cm_client_id->qp;
    debug("QP created at %p \n", client_qp);
    return 0;
}

/* Pre-posts a receive buffer before calling rdma_connect () */
int client_pre_post_recv_buffer(struct rc_server_conn *server_conn)
{
    int ret = -1;
    server_conn->server_metadata_mr = rdma_buffer_register(server_conn->pd,
                                              &server_conn->server_metadata_attr,
                                              sizeof(server_conn->server_metadata_attr),
                                              (IBV_ACCESS_LOCAL_WRITE));
    if(!server_conn->server_metadata_mr){
        rdma_error("Failed to setup the server metadata mr , -ENOMEM\n");
        return -ENOMEM;
    }
    server_conn->server_recv_sge.addr = (uint64_t) server_conn->server_metadata_mr->addr;
    server_conn->server_recv_sge.length = (uint32_t) server_conn->server_metadata_mr->length;
    server_conn->server_recv_sge.lkey = (uint32_t) server_conn->server_metadata_mr->lkey;
    /* now we link it to the request */
    bzero(&server_conn->server_recv_wr, sizeof(server_conn->server_recv_wr));
    server_conn->server_recv_wr.sg_list = &server_conn->server_recv_sge;
    server_conn->server_recv_wr.num_sge = 1;
    ret = ibv_post_recv(server_conn->client_qp /* which QP */,
                        &server_conn->server_recv_wr /* receive work request*/,
                        &server_conn->bad_server_recv_wr /* error WRs */);
    if (ret) {
        rdma_error("Failed to pre-post the receive buffer, errno: %d \n", ret);
        return ret;
    }
    debug("Receive buffer pre-posting is successful \n");
    return 0;
}

/* Connects to the RDMA server */
static int client_connect_to_server(struct rc_server_conn *server_conn)
{
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    bzero(&conn_param, sizeof(conn_param));
    conn_param.initiator_depth = 3;
    conn_param.responder_resources = 3;
    conn_param.retry_count = 3; // if fail, then how many times to retry
    ret = rdma_connect(server_conn->cm_client_id, &conn_param);
    if (ret) {
        rdma_error("Failed to connect to remote host , errno: %d\n", -errno);
        return -errno;
    }
    debug("waiting for cm event: RDMA_CM_EVENT_ESTABLISHED\n");
    ret = process_rdma_cm_event(server_conn->cm_event_channel,
                                RDMA_CM_EVENT_ESTABLISHED,
                                &cm_event);
    if (ret) {
        rdma_error("Failed to get cm event, ret = %d \n", ret);
        return ret;
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge cm event, errno: %d\n",
                   -errno);
        return -errno;
    }
    printf("The client is connected successfully \n");
    return 0;
}

/* Exchange buffer metadata with the server. The client sends its, and then receives
 * from the server. The client-side metadata on the server is _not_ used because
 * this program is client driven. But it shown here how to do it for the illustration
 * purposes
 */
static int client_xchange_metadata_with_server(struct rc_server_conn *server_conn)
{
    struct ibv_wc wc[2];
    int ret = -1;

    server_conn->client_request_mr = rdma_buffer_register(server_conn->pd,
                                                          server_conn->request,
                                                          sizeof(struct request),
                                                          (IBV_ACCESS_LOCAL_WRITE|
                                                       IBV_ACCESS_REMOTE_READ|
                                                       IBV_ACCESS_REMOTE_WRITE));

    server_conn->request = allocate_request();
    bzero(server_conn->request, sizeof(struct request));
    post_recieve(sizeof(struct request), server_conn->client_request_mr->lkey, wc[1].wr_id, server_conn->client_qp, server_conn->request);



    if(!server_conn->client_request_mr){
        rdma_error("Failed to register the first buffer, ret = %d \n", ret);
        return ret;
    }


    /* we prepare metadata for the first buffer */
    server_conn->client_metadata_attr.address = (uint64_t) server_conn->client_request_mr->addr;
    server_conn->client_metadata_attr.length = server_conn->client_request_mr->length;
    server_conn->client_metadata_attr.stag.local_stag = server_conn->client_request_mr->lkey;
    /* now we register the metadata memory */
    server_conn->client_metadata_mr = rdma_buffer_register(server_conn->pd,
                                              &server_conn->client_metadata_attr,
                                              sizeof(server_conn->client_metadata_attr),
                                              IBV_ACCESS_LOCAL_WRITE);
    if(!server_conn->client_metadata_mr) {
        rdma_error("Failed to register the client metadata buffer, ret = %d \n", ret);
        return ret;
    }
    /* now we fill up SGE */
    server_conn->client_send_sge.addr = (uint64_t) server_conn->client_metadata_mr->addr;
    server_conn->client_send_sge.length = (uint32_t) server_conn->client_metadata_mr->length;
    server_conn->client_send_sge.lkey = server_conn->client_metadata_mr->lkey;
    /* now we link to the send work request */
    bzero(&server_conn->client_send_wr, sizeof(server_conn->client_send_wr));
    server_conn->client_send_wr.sg_list = &server_conn->client_send_sge;
    server_conn->client_send_wr.num_sge = 1;
    server_conn->client_send_wr.opcode = IBV_WR_SEND;
    server_conn->client_send_wr.send_flags = IBV_SEND_SIGNALED;
    /* Now we post it */
    ret = ibv_post_send(server_conn->client_qp,
                        &server_conn->client_send_wr,
                        &server_conn->bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to send client metadata, errno: %d \n",
                   -errno);
        return -errno;
    }
    /* at this point we are expecting 2 work completion. One for our
     * send and one for recv that we will get from the server for
     * its buffer information */
    ret = process_work_completion_events(server_conn->io_completion_channel,
                                         wc, 2);
    if(ret != 2) {
        rdma_error("We failed to get 2 work completions , ret = %d \n",
                   ret);
        return ret;
    }
    debug("Server sent us its buffer location and credentials, showing \n");
    show_rdma_buffer_attr(&server_conn->server_metadata_attr);
    return 0;
}

/* This function does :
 * 1) Prepare memory buffers for RDMA operations
 * 1) RDMA write from src -> remote buffer
 * 2) RDMA read from remote bufer -> dst
 */
static int client_remote_memory_ops(struct rc_server_conn *server_conn)
{
    struct ibv_wc wc;
    int ret = -1;
//    server_conn->client_dst_mr = rdma_buffer_register(server_conn->pd,
//                                         dst,
//                                         strlen(src),
//                                         (IBV_ACCESS_LOCAL_WRITE |
//                                          IBV_ACCESS_REMOTE_WRITE |
//                                          IBV_ACCESS_REMOTE_READ));
    if (!server_conn->client_dst_mr) {
        rdma_error("We failed to create the destination buffer, -ENOMEM\n");
        return -ENOMEM;
    }
    /* Step 1: is to copy the local buffer into the remote buffer. We will
     * reuse the previous variables. */
    /* now we fill up SGE */
    server_conn->client_send_sge.addr = (uint64_t) server_conn->client_request_mr->addr;
    server_conn->client_send_sge.length = (uint32_t) server_conn->client_request_mr->length;
    server_conn->client_send_sge.lkey = server_conn->client_request_mr->lkey;
    /* now we link to the send work request */
    bzero(&server_conn->client_send_wr, sizeof(server_conn->client_send_wr));
    server_conn->client_send_wr.sg_list = &server_conn->client_send_sge;
    server_conn->client_send_wr.num_sge = 1;
    server_conn->client_send_wr.opcode = IBV_WR_RDMA_WRITE;
    server_conn->client_send_wr.send_flags = IBV_SEND_SIGNALED;
    /* we have to tell server side info for RDMA */
    server_conn->client_send_wr.wr.rdma.rkey = server_conn->server_metadata_attr.stag.remote_stag;
    server_conn->client_send_wr.wr.rdma.remote_addr = server_conn->server_metadata_attr.address;
    /* Now we post it */
    ret = ibv_post_send(server_conn->client_qp,
                        &server_conn->client_send_wr,
                        &server_conn->bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to write client src buffer, errno: %d \n",
                   -errno);
        return -errno;
    }
    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(server_conn->io_completion_channel,
                                         &wc, 1);
    if(ret != 1) {
        rdma_error("We failed to get 1 work completions , ret = %d \n",
                   ret);
        return ret;
    }
    pr_debug("Client side WRITE is complete \n");
    /* Now we prepare a READ using same variables but for destination */
    server_conn->client_send_sge.addr = (uint64_t) server_conn->client_dst_mr->addr;
    server_conn->client_send_sge.length = (uint32_t) server_conn->client_dst_mr->length;
    server_conn->client_send_sge.lkey = server_conn->client_dst_mr->lkey;
    /* now we link to the send work request */
    bzero(&server_conn->client_send_wr, sizeof(server_conn->client_send_wr));
    server_conn->client_send_wr.sg_list = &server_conn->client_send_sge;
    server_conn->client_send_wr.num_sge = 1;
    server_conn->client_send_wr.opcode = IBV_WR_RDMA_READ;
    server_conn->client_send_wr.send_flags = IBV_SEND_SIGNALED;
    /* we have to tell server side info for RDMA */
    server_conn->client_send_wr.wr.rdma.rkey = server_conn->server_metadata_attr.stag.remote_stag;
    server_conn->client_send_wr.wr.rdma.remote_addr = server_conn->server_metadata_attr.address;
    /* Now we post it */
    ret = ibv_post_send(server_conn->client_qp,
                        &server_conn->client_send_wr,
                        &server_conn->bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to read client dst buffer from the master, errno: %d \n",
                   -errno);
        return -errno;
    }
    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(server_conn->io_completion_channel,
                                         &wc, 1);
    if(ret != 1) {
        rdma_error("We failed to get 1 work completions , ret = %d \n",
                   ret);
        return ret;
    }
    debug("Client side READ is complete \n");
    return 0;
}


//server_conn->server_metadata_mr = rdma_buffer_register(server_conn->pd,
//                                                       &server_conn->server_metadata_attr,
//                                                       sizeof(server_conn->server_metadata_attr),
//                                                       (IBV_ACCESS_LOCAL_WRITE));
//if(!server_conn->server_metadata_mr){
//rdma_error("Failed to setup the server metadata mr , -ENOMEM\n");
//return -ENOMEM;
//}
//server_conn->server_recv_sge.addr = (uint64_t) server_conn->server_metadata_mr->addr;
//server_conn->server_recv_sge.length = (uint32_t) server_conn->server_metadata_mr->length;
//server_conn->server_recv_sge.lkey = (uint32_t) server_conn->server_metadata_mr->lkey;
///* now we link it to the request */
//bzero(&server_conn->server_recv_wr, sizeof(server_conn->server_recv_wr));
//server_conn->server_recv_wr.sg_list = &server_conn->server_recv_sge;
//server_conn->server_recv_wr.num_sge = 1;
//ret = ibv_post_recv(server_conn->client_qp /* which QP */,
//                    &server_conn->server_recv_wr /* receive work request*/,
//                    &server_conn->bad_server_recv_wr /* error WRs */);

int get_server_request_metadata(struct rc_server_conn *server_conn) {
    struct ibv_wc wc;
    int ret = -1;

    ret = process_work_completion_events(server_conn->io_completion_channel, &wc, 1);
    if (ret != 1) {
        rdma_error("Failed to receive , ret = %d \n", ret);
        return ret;
    }
    /* if all good, then we should have client's buffer information, lets see */
    printf("Client side buffer information is received...\n");
    show_rdma_buffer_attr(&server_conn->server_metadata_attr);
    printf("The client has requested buffer length of : %u bytes \n",
           server_conn->server_metadata_attr.length);

    return 0;
}

int send_request(struct rc_server_conn *server_conn, struct request *request) {
    int ret;
    struct ibv_wc wc;
    server_conn->client_request_mr = rdma_buffer_register(server_conn->pd,
                                         request,
                                         sizeof(struct request),
                                         (IBV_ACCESS_LOCAL_WRITE|
                                          IBV_ACCESS_REMOTE_READ|
                                          IBV_ACCESS_REMOTE_WRITE));

    ret = post_send(sizeof(struct request), server_conn->client_request_mr->lkey, wc.wr_id, server_conn->client_qp, request);

//    server_conn->client_send_sge.addr = (uint64_t) server_conn->client_request_mr->addr;
//    server_conn->client_send_sge.length = (uint32_t) server_conn->client_request_mr->length;
//    server_conn->client_send_sge.lkey = server_conn->client_request_mr->lkey;
//    /* now we link to the send work request */
//    bzero(&server_conn->client_send_wr, sizeof(server_conn->client_send_wr));
//    server_conn->client_send_wr.sg_list = &server_conn->client_send_sge;
//    server_conn->client_send_wr.num_sge = 1;
//    server_conn->client_send_wr.opcode = IBV_WR_SEND;
//    server_conn->client_send_wr.send_flags = IBV_SEND_SIGNALED;
////    /* we have to tell server side info for RDMA */
////    server_conn->client_send_wr.wr.rdma.rkey = server_conn->server_metadata_attr.stag.remote_stag;
////    server_conn->client_send_wr.wr.rdma.remote_addr = server_conn->server_metadata_attr.address;
//    /* Now we post it */
//    ret = ibv_post_send(server_conn->client_qp,
//                        &server_conn->client_send_wr,
//                        &server_conn->bad_client_send_wr);
    if (ret) {
        rdma_error("Failed to write client src buffer, errno: %d \n",
                   -errno);
        return -errno;
    }
    /* at this point we are expecting 1 work completion for the write */
    ret = process_work_completion_events(server_conn->io_completion_channel,
                                         &wc, 1);
    if(ret != 1) {
        rdma_error("We failed to get 1 work completions , ret = %d \n",
                   ret);
        return ret;
    }
    return 0;
}


/* This function disconnects the RDMA connection from the server and cleans up
 * all the resources.
 */
static int client_disconnect_and_clean(struct rc_server_conn *server_conn)
{
    struct rdma_cm_event *cm_event = NULL;
    int ret = -1;
    /* active disconnect from the client side */
    ret = rdma_disconnect(server_conn->cm_client_id);
    if (ret) {
        rdma_error("Failed to disconnect, errno: %d \n", -errno);
        //continuing anyways
    }
    ret = process_rdma_cm_event(server_conn->cm_event_channel,
                                RDMA_CM_EVENT_DISCONNECTED,
                                &cm_event);
    if (ret) {
        rdma_error("Failed to get RDMA_CM_EVENT_DISCONNECTED event, ret = %d\n",
                   ret);
        //continuing anyways
    }
    ret = rdma_ack_cm_event(cm_event);
    if (ret) {
        rdma_error("Failed to acknowledge cm event, errno: %d\n",
                   -errno);
        //continuing anyways
    }
    /* Destroy QP */
    rdma_destroy_qp(server_conn->cm_client_id);
    /* Destroy client cm id */
    ret = rdma_destroy_id(server_conn->cm_client_id);
    if (ret) {
        rdma_error("Failed to destroy client id cleanly, %d \n", -errno);
        // we continue anyways;
    }
    /* Destroy CQ */
    ret = ibv_destroy_cq(server_conn->client_cq);
    if (ret) {
        rdma_error("Failed to destroy completion queue cleanly, %d \n", -errno);
        // we continue anyways;
    }
    /* Destroy completion channel */
    ret = ibv_destroy_comp_channel(server_conn->io_completion_channel);
    if (ret) {
        rdma_error("Failed to destroy completion channel cleanly, %d \n", -errno);
        // we continue anyways;
    }
    /* Destroy memory buffers */
    pr_info("server meta\n");
    rdma_buffer_deregister(server_conn->server_metadata_mr);
    pr_info("client meta\n");
    rdma_buffer_deregister(server_conn->client_metadata_mr);
    pr_info("client request\n");
    rdma_buffer_deregister(server_conn->client_request_mr);
    pr_info("client dst\n");
    rdma_buffer_deregister(server_conn->client_dst_mr);
    /* We free the buffers */
    free(server_conn->request);
//    free(dst);
    /* Destroy protection domain */
    ret = ibv_dealloc_pd(server_conn->pd);
    if (ret) {
        rdma_error("Failed to destroy client protection domain cleanly, %d \n", -errno);
        // we continue anyways;
    }
    rdma_destroy_event_channel(server_conn->cm_event_channel);
    printf("Client resource clean up is complete \n");
    return 0;
}

int rc_main(char *key, struct sockaddr_in *server_sockaddr) {
    int ret;
    struct rc_server_conn *server_conn = calloc(1, sizeof(struct rc_server_conn));
    server_conn->server_sockaddr = server_sockaddr;

    ret = client_prepare_connection(server_conn);
    if (ret) {
        rdma_error("Failed to setup client connection , ret = %d \n", ret);
        return ret;
    }
    ret = client_pre_post_recv_buffer(server_conn);
    if (ret) {
        rdma_error("Failed to setup client connection , ret = %d \n", ret);
        return ret;
    }
    ret = client_connect_to_server(server_conn);
    if (ret) {
        rdma_error("Failed to setup client connection , ret = %d \n", ret);
        return ret;
    }

    ret = get_server_request_metadata(server_conn);
    if (ret) {
        rdma_error("Failed to get server request location , ret = %d \n", ret);
        return ret;
    }
    server_conn->request = allocate_request();
    bzero(server_conn->request, sizeof(struct request));
    strncpy(server_conn->request->key, "testing", KEY_SIZE);
    server_conn->request->key_len = strlen(server_conn->request->key);
    server_conn->request->method = GET;
    server_conn->request->msg_len = strlen("hello server");
    ret = send_request(server_conn, server_conn->request);
    if (ret) {
        rdma_error("Failed to get send request, ret = %d \n", ret);
        return ret;
    }
//    ret = client_xchange_metadata_with_server(server_conn);
//    if (ret) {
//        rdma_error("Failed to setup client connection , ret = %d \n", ret);
//        return ret;
//    }
//    ret = client_remote_memory_ops(server_conn);
//    if (ret) {
//        rdma_error("Failed to finish remote memory ops, ret = %d \n", ret);
//        return ret;
//    }
//    if (check_src_dst()) {
//        rdma_error("src and dst buffers do not match \n");
//    } else {
//        printf("...\nSUCCESS, source and destination buffers match \n");
//    }
    ret = client_disconnect_and_clean(server_conn);
    if (ret) {
        rdma_error("Failed to cleanly disconnect and clean up resources \n");
    }
    return ret;
}