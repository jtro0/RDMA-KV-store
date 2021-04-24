//
// Created by jtroo on 15-04-21.
//

#include "rc_server_utils.h"
#include "rdma_common.h"

int setup_client_resources(struct conn_info *connInfo)
{
    int ret = -1;
    if(!connInfo->rc_connection->cm_client_id){
        rdma_error("Client id is still NULL \n");
        return -EINVAL;
    }
    /* We have a valid connection identifier, lets start to allocate
     * resources. We need:
     * 1. Protection Domains (PD)
     * 2. Memory Buffers
     * 3. Completion Queues (CQ)
     * 4. Queue Pair (QP)
     * Protection Domain (PD) is similar to a "process abstraction"
     * in the operating system. All resources are tied to a particular PD.
     * And accessing recourses across PD will result in a protection fault.
     */
    connInfo->rc_connection->pd = ibv_alloc_pd(connInfo->rc_connection->cm_client_id->verbs
            /* verbs defines a verb's provider,
             * i.e an RDMA device where the incoming
             * client connection came */);
    if (!connInfo->rc_connection->pd) {
        rdma_error("Failed to allocate a protection domain errno: %d\n",
                   -errno);
        return -errno;
    }
    debug("A new protection domain is allocated at %p \n", pd);
    /* Now we need a completion channel, were the I/O completion
     * notifications are sent. Remember, this is different from connection
     * management (CM) event notifications.
     * A completion channel is also tied to an RDMA device, hence we will
     * use cm_client_id->verbs.
     */
    connInfo->rc_connection->io_completion_channel = ibv_create_comp_channel(connInfo->rc_connection->cm_client_id->verbs);
    if (!connInfo->rc_connection->io_completion_channel) {
        rdma_error("Failed to create an I/O completion event channel, %d\n",
                   -errno);
        return -errno;
    }
    debug("An I/O completion event channel is created at %p \n",
          io_completion_channel);
    /* Now we create a completion queue (CQ) where actual I/O
     * completion metadata is placed. The metadata is packed into a structure
     * called struct ibv_wc (wc = work completion). ibv_wc has detailed
     * information about the work completion. An I/O request in RDMA world
     * is called "work" ;)
     */
    connInfo->rc_connection->cq = ibv_create_cq(connInfo->rc_connection->cm_client_id->verbs /* which device*/,
                       CQ_CAPACITY /* maximum capacity*/,
                       NULL /* user context, not used here */,
                                                connInfo->rc_connection->io_completion_channel /* which IO completion channel */,
                       0 /* signaling vector, not used here*/);
    if (!connInfo->rc_connection->cq) {
        rdma_error("Failed to create a completion queue (cq), errno: %d\n",
                   -errno);
        return -errno;
    }
    debug("Completion queue (CQ) is created at %p with %d elements \n",
          cq, cq->cqe);
    /* Ask for the event for all activities in the completion queue*/
    ret = ibv_req_notify_cq(connInfo->rc_connection->cq /* on which CQ */,
                            0 /* 0 = all event type, no filter*/);
    if (ret) {
        rdma_error("Failed to request notifications on CQ errno: %d \n",
                   -errno);
        return -errno;
    }
    /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
     * The capacity here is define statically but this can be probed from the
     * device. We just use a small number as defined in rdma_common.h */
    bzero(&connInfo->rc_connection->qp_init_attr, sizeof connInfo->rc_connection->qp_init_attr);
    connInfo->rc_connection->qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
    connInfo->rc_connection->qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
    connInfo->rc_connection->qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
    connInfo->rc_connection->qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
    connInfo->rc_connection->qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
    /* We use same completion queue, but one can use different queues */
    connInfo->rc_connection-> qp_init_attr.recv_cq = connInfo->rc_connection->cq; /* Where should I notify for receive completion operations */
    connInfo->rc_connection->qp_init_attr.send_cq = connInfo->rc_connection->cq; /* Where should I notify for send completion operations */
    /*Lets create a QP */
    ret = rdma_create_qp(connInfo->rc_connection->cm_client_id /* which connection id */,
                         connInfo->rc_connection->pd /* which protection domain*/,
                         &connInfo->rc_connection->qp_init_attr /* Initial attributes */);
    if (ret) {
        rdma_error("Failed to create QP due to errno: %d\n", -errno);
        return -errno;
    }
    /* Save the reference for handy typing but is not required */
    connInfo->rc_connection->client_qp = connInfo->rc_connection->cm_client_id->qp;
    debug("Client QP created at %p\n", client_qp);
    return ret;
}

int init_rc_server(struct conn_info *connInfo) {
    struct rdma_cm_event *cmEvent = NULL;
    int ret = -1;

    /*  Open a channel used to report asynchronous communication event */
    connInfo->rc_connection->cm_event_channel = rdma_create_event_channel();
    check_for_error(!connInfo->rc_connection->cm_event_channel, -errno, "Creating cm event channel failed with errno : (%d)", -errno);

    pr_info("RDMA CM event channel is created successfully at %p \n",
            connInfo->rc_connection->cm_event_channel);

    /* rdma_cm_id is the connection identifier (like socket) which is used
     * to define an RDMA connection.
     */
    ret = rdma_create_id(connInfo->rc_connection->cm_event_channel, &connInfo->rc_connection->cm_server_id, NULL, RDMA_PS_TCP);
    check_for_error(ret, -errno, "Creating server cm id failed with errno: %d ", -errno);

    pr_info("A RDMA connection id for the server is created \n");

    /* Explicit binding of rdma cm id to the socket credentials */
    ret = rdma_bind_addr(connInfo->rc_connection->cm_server_id, (struct sockaddr*) &connInfo->addr);
    check_for_error(ret, -errno, "Failed to bind server address, errno: %d \n", -errno);

    pr_info("Server RDMA CM id is successfully bound \n");

//    connInfo->rc_connection->wcs = calloc(NUM_WC, sizeof(struct ibv_wc));

    // TODO put this into accept? for multiple clients?
    /* Now we start to listen on the passed IP and port. However unlike
     * normal TCP listen, this is a non-blocking call. When a new client is
     * connected, a new connection management (CM) event is generated on the
     * RDMA CM event channel from where the listening id was created. Here we
     * have only one channel, so it is easy. */
    ret = rdma_listen(connInfo->rc_connection->cm_server_id, 8); /* backlog = 8 clients, same as TCP, see man listen*/
    check_for_error(ret, -errno, "rdma_listen failed to listen on server address, errno: %d ",
                    -errno);

    printf("Server is listening successfully at: %s , port: %d \n",
           inet_ntoa(connInfo->addr.sin_addr),
           ntohs(connInfo->addr.sin_port));
    /* now, we expect a client to connect and generate a RDMA_CM_EVNET_CONNECT_REQUEST
     * We wait (block) on the connection management event channel for
     * the connect event.
     */
    ret = process_rdma_cm_event(connInfo->rc_connection->cm_event_channel,
                                RDMA_CM_EVENT_CONNECT_REQUEST,
                                &cmEvent);
    check_for_error(ret, ret, "Failed to get cm event, ret = %d \n" , ret);

    /* Much like TCP connection, listening returns a new connection identifier
     * for newly connected client. In the case of RDMA, this is stored in id
     * field. For more details: man rdma_get_cm_event
     */
    connInfo->rc_connection->cm_client_id = cmEvent->id;
    /* now we acknowledge the event. Acknowledging the event free the resources
     * associated with the event structure. Hence any reference to the event
     * must be made before acknowledgment. Like, we have already saved the
     * client id from "id" field before acknowledging the event.
     */
    ret = rdma_ack_cm_event(cmEvent);
    check_for_error(ret, -errno, "Failed to acknowledge the cm event errno: %d \n", -errno);

    pr_info("A new RDMA client connection id is stored at %p\n", connInfo->rc_connection->cm_client_id);
    return ret;
}

int rc_accept_new_connection(struct rc_server_info *listening) {
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    struct sockaddr_in remote_sockaddr;
    int ret = -1;
    check_for_error(!listening->cm_client_id || !listening->cm_client_id, -EINVAL, "Client resources are not properly setup\n", -EINVAL);
    /* we prepare the receive buffer in which we will receive the client metadata*/
    listening->client_metadata_mr = rdma_buffer_register(listening->pd /* which protection domain */,
                                              &listening->client_metadata_attr /* what memory */,
                                              sizeof(listening->client_metadata_attr) /* what length */,
                                              (IBV_ACCESS_LOCAL_WRITE) /* access permissions */);
    check_for_error(!listening->client_metadata_mr, -ENOMEM, "Failed to register client attr buffer\n", -ENOMEM);

    /* We pre-post this receive buffer on the QP. SGE credentials is where we
     * receive the metadata from the client */
    listening->client_recv_sge.addr = (uint64_t) listening->client_metadata_mr->addr; // same as &client_buffer_attr
    listening->client_recv_sge.length = listening->client_metadata_mr->length;
    listening->client_recv_sge.lkey = listening->client_metadata_mr->lkey;
    /* Now we link this SGE to the work request (WR) */
    bzero(&listening->client_recv_wr, sizeof(listening->client_recv_wr));
    listening->client_recv_wr.sg_list = &listening->client_recv_sge;
    listening->client_recv_wr.num_sge = 1; // only one SGE
    ret = ibv_post_recv(listening->client_qp /* which QP */,
                        &listening->client_recv_wr /* receive work request*/,
                        &listening->bad_client_recv_wr /* error WRs */);
    check_for_error(ret, ret, "Failed to pre-post the receive buffer, errno: %d \n", ret);

    pr_info("Receive buffer pre-posting is successful \n");
    /* Now we accept the connection. Recall we have not accepted the connection
     * yet because we have to do lots of resource pre-allocation */
    memset(&conn_param, 0, sizeof(conn_param));
    /* this tell how many outstanding requests can we handle */
    conn_param.initiator_depth = 3; /* For this exercise, we put a small number here */
    /* This tell how many outstanding requests we expect other side to handle */
    conn_param.responder_resources = 3; /* For this exercise, we put a small number */
    ret = rdma_accept(listening->cm_client_id, &conn_param);

    check_for_error(ret, -errno, "Failed to accept the connection, errno: %d \n", -errno);

    /* We expect an RDMA_CM_EVNET_ESTABLISHED to indicate that the RDMA
 * connection has been established and everything is fine on both, server
 * as well as the client sides.
 */
    pr_info("Going to wait for : RDMA_CM_EVENT_ESTABLISHED event \n");
    ret = process_rdma_cm_event(listening->cm_event_channel,
                                RDMA_CM_EVENT_ESTABLISHED,
                                &cm_event);
    check_for_error(ret, -errno, "Failed to get the cm event, errnp: %d \n", -errno);
    /* We acknowledge the event */
    ret = rdma_ack_cm_event(cm_event);
    check_for_error(ret, -errno, "Failed to acknowledge the cm event %d\n", -errno);
    /* Just FYI: How to extract connection information */
    memcpy(&remote_sockaddr /* where to save */,
           rdma_get_peer_addr(listening->cm_client_id) /* gives you remote sockaddr */,
           sizeof(struct sockaddr_in) /* max size */);
    printf("A new connection is accepted from %s \n",
           inet_ntoa(remote_sockaddr.sin_addr));
    return ret;
}

int send_buffer_meta(struct rc_server_info *server) {
    struct ibv_wc wc;
    int ret = -1;
    /* Now, we first wait for the client to start the communication by
     * sending the server its metadata info. The server does not use it
     * in our example. We will receive a work completion notification for
     * our pre-posted receive request.
     */
//    ret = process_work_completion_events(server->io_completion_channel, &wc, 1);
//    if (ret != 1) {
//        rdma_error("Failed to receive , ret = %d \n", ret);
//        return ret;
//    }
    /* if all good, then we should have client's buffer information, lets see */
//    printf("Client side buffer information is received...\n");
//    show_rdma_buffer_attr(&server->client_metadata_attr);
//    printf("The client has requested buffer length of : %u bytes \n",
//           server->client_metadata_attr.length);

    server->request = allocate_request();
    /* We need to setup requested memory buffer. This is where the client will
    * do RDMA READs and WRITEs. */
    server->request_mr = rdma_buffer_register(server->pd /* which protection domain */,
                                         server->request,
                                          sizeof(struct request) /* what size to allocate */,
                                         (IBV_ACCESS_LOCAL_WRITE|
                                          IBV_ACCESS_REMOTE_READ| // TODO Remove this permission?
                                          IBV_ACCESS_REMOTE_WRITE) /* access permissions */);
    check_for_error(!server->request_mr, -ENOMEM, "Server failed to create a buffer \n", -ENOMEM);

    /* This buffer is used to transmit information about the above
 * buffer to the client. So this contains the metadata about the server
 * buffer. Hence this is called metadata buffer. Since this is already
 * on allocated, we just register it.
     * We need to prepare a send I/O operation that will tell the
 * client the address of the server buffer.
 */
    server->request_attr.address = (uint64_t) server->request_mr->addr;
    server->request_attr.length = (uint32_t) server->request_mr->length;
    server->request_attr.stag.local_stag = (uint32_t) server->request_mr->lkey;
    server->client_metadata_mr = rdma_buffer_register(server->pd /* which protection domain*/,
                                              &server->request_attr /* which memory to register */,
                                              sizeof(server->request_attr) /* what is the size of memory */,
                                              IBV_ACCESS_LOCAL_WRITE /* what access permission */);
    if(!server->client_metadata_mr){
        rdma_error("Server failed to create to hold server metadata \n");
        /* we assume that this is due to out of memory error */
        return -ENOMEM;
    }
    /* We need to transmit this buffer. So we create a send request.
 * A send request consists of multiple SGE elements. In our case, we only
 * have one
 */
    server->client_send_sge.addr = (uint64_t) &server->request_attr;
    server->client_send_sge.length = sizeof(server->request_attr);
    server->client_send_sge.lkey = server->client_metadata_mr->lkey;
    /* now we link this sge to the send request */
    bzero(&server->client_send_wr, sizeof(server->client_send_wr));
    server->client_send_wr.sg_list = &server->client_send_sge;
    server->client_send_wr.num_sge = 1; // only 1 SGE element in the array
    server->client_send_wr.opcode = IBV_WR_SEND; // This is a send request
    server->client_send_wr.send_flags = IBV_SEND_SIGNALED; // We want to get notification
    /* This is a fast data path operation. Posting an I/O request */
    ret = ibv_post_send(server->client_qp /* which QP */,
                        &server->client_send_wr /* Send request that we prepared before */,
                        &server->bad_client_send_wr /* In case of error, this will contain failed requests */);
    if (ret) {
        rdma_error("Posting of server metdata failed, errno: %d \n",
                   -errno);
        return -errno;
    }
    /* We check for completion notification */
    ret = process_work_completion_events(server->io_completion_channel, &wc, 1);
    if (ret != 1) {
        rdma_error("Failed to send server metadata, ret = %d \n", ret);
        return ret;
    }
    debug("Local buffer metadata has been sent to the client \n");
    return 0;
}

int rc_receive_header(struct rc_server_info *server) {
    int ret = 0, n = 0;
    struct ibv_wc wc;

    ret = post_recieve(sizeof(struct request), server->client_metadata_mr->lkey, wc.wr_id, server->client_qp,
                       server->request);

    ret = process_work_completion_events(server->io_completion_channel, &wc, 1);



//    do {
//        n = ibv_poll_cq(server->cq, 1, &server->wc);
//    } while (n < 1);
//    if (server->wc.status != IBV_WC_SUCCESS) {
//        if (server->wc.opcode == IBV_WC_RECV) {
//            pr_debug("receive failed");
//            return -1;
//        }
//    }
//
//    if (server->wc.opcode == IBV_WC_RECV) {
//        pr_info("got recv\n");
//        return sizeof(struct request);
//    }

    return ret;
}