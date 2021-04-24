//
// Created by jtroo on 15-04-21.
//

#include "rc_server_utils.h"
#include "rdma_common.h"

int setup_client_resources(struct rc_client_connection *client) {
    int ret = -1;
    check(!client->cm_client_id, -EINVAL, "Client id is still NULL\n", -EINVAL);

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
    client->pd = ibv_alloc_pd(client->cm_client_id->verbs
            /* verbs defines a verb's provider,
             * i.e an RDMA device where the incoming
             * client connection came */);
    check(!client->pd, -errno, "Failed to allocate a protection domain errno: %d\n",
          -errno);

    pr_debug("A new protection domain is allocated at %p \n", client->pd);
    /* Now we need a completion channel, were the I/O completion
     * notifications are sent. Remember, this is different from connection
     * management (CM) event notifications.
     * A completion channel is also tied to an RDMA device, hence we will
     * use cm_client_id->verbs.
     */
    client->io_completion_channel = ibv_create_comp_channel(
            client->cm_client_id->verbs);
    check(!client->io_completion_channel, -errno,
          "Failed to create an I/O completion event channel, %d\n",
          -errno);

    pr_debug("An I/O completion event channel is created at %p \n",
             client->io_completion_channel);
    /* Now we create a completion queue (CQ) where actual I/O
     * completion metadata is placed. The metadata is packed into a structure
     * called struct ibv_wc (wc = work completion). ibv_wc has detailed
     * information about the work completion. An I/O request in RDMA world
     * is called "work" ;)
     */
    client->cq = ibv_create_cq(client->cm_client_id->verbs /* which device*/,
                               CQ_CAPACITY /* maximum capacity*/,
                               NULL /* user context, not used here */,
                               client->io_completion_channel /* which IO completion channel */,
                               0 /* signaling vector, not used here*/);
    check(!client->cq, -errno, "Failed to create a completion queue (cq), errno: %d\n",
          -errno);

    pr_debug("Completion queue (CQ) is created at %p with %d elements \n",
             client->cq, client->cq->cqe);
    /* Ask for the event for all activities in the completion queue*/
    ret = ibv_req_notify_cq(client->cq /* on which CQ */,
                            0 /* 0 = all event type, no filter*/);
    check(ret, -errno, "Failed to request notifications on CQ errno: %d \n",
          -errno);

    /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
     * The capacity here is define statically but this can be probed from the
     * device. We just use a small number as defined in rdma_common.h */
    bzero(&client->qp_init_attr, sizeof client->qp_init_attr);
    client->qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
    client->qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
    client->qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
    client->qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
    client->qp_init_attr.qp_type = IBV_QPT_RC; /* QP type, RC = Reliable connection */
    /* We use same completion queue, but one can use different queues */
    client->qp_init_attr.recv_cq = client->cq; /* Where should I notify for receive completion operations */
    client->qp_init_attr.send_cq = client->cq; /* Where should I notify for send completion operations */
    /*Lets create a QP */
    ret = rdma_create_qp(client->cm_client_id /* which connection id */,
                         client->pd /* which protection domain*/,
                         &client->qp_init_attr /* Initial attributes */);
    check(ret, -errno, "Failed to create QP due to errno: %d\n", -errno);

    /* Save the reference for handy typing but is not required */
    client->client_qp = client->cm_client_id->qp;
    pr_debug("Client QP created at %p\n", client->client_qp);
    return ret;
}

int init_rc_server(struct server_info *server) {
    struct rdma_cm_event *cmEvent = NULL;
    int ret = -1;

    /*  Open a channel used to report asynchronous communication event */
    server->rc_server_info->cm_event_channel = rdma_create_event_channel();
    check(!server->rc_server_info->cm_event_channel, -errno,
          "Creating cm event channel failed with errno : (%d)", -errno);

    pr_info("RDMA CM event channel is created successfully at %p \n",
            server->rc_server_info->cm_event_channel);

    /* rdma_cm_id is the connection identifier (like socket) which is used
     * to define an RDMA connection.
     */
    ret = rdma_create_id(server->rc_server_info->cm_event_channel, &server->rc_server_info->cm_server_id, NULL,
                         RDMA_PS_TCP);
    check(ret, -errno, "Creating server cm id failed with errno: %d ", -errno);

    pr_info("A RDMA connection id for the server is created \n");

    /* Explicit binding of rdma cm id to the socket credentials */
    ret = rdma_bind_addr(server->rc_server_info->cm_server_id, (struct sockaddr *) &server->addr);
    check(ret, -errno, "Failed to bind server address, errno: %d \n", -errno);

    pr_info("Server RDMA CM id is successfully bound \n");

    server->client->rc_client = malloc(sizeof(struct rc_client_connection));
    // TODO put this into accept? for multiple clients?
    /* Now we start to listen on the passed IP and port. However unlike
     * normal TCP listen, this is a non-blocking call. When a new client is
     * connected, a new connection management (CM) event is generated on the
     * RDMA CM event channel from where the listening id was created. Here we
     * have only one channel, so it is easy. */
    ret = rdma_listen(server->rc_server_info->cm_server_id, 8); /* backlog = 8 clients, same as TCP, see man listen*/
    check(ret, -errno, "rdma_listen failed to listen on server address, errno: %d ",
          -errno);

    printf("Server is listening successfully at: %s , port: %d \n",
           inet_ntoa(server->addr.sin_addr),
           ntohs(server->addr.sin_port));
    /* now, we expect a client to connect and generate a RDMA_CM_EVNET_CONNECT_REQUEST
     * We wait (block) on the connection management event channel for
     * the connect event.
     */
    ret = process_rdma_cm_event(server->rc_server_info->cm_event_channel,
                                RDMA_CM_EVENT_CONNECT_REQUEST,
                                &cmEvent);
    check(ret, ret, "Failed to get cm event, ret = %d \n", ret);

    /* Much like TCP connection, listening returns a new connection identifier
     * for newly connected client. In the case of RDMA, this is stored in id
     * field. For more details: man rdma_get_cm_event
     */
    server->client->rc_client->cm_client_id = cmEvent->id;
    /* now we acknowledge the event. Acknowledging the event free the resources
     * associated with the event structure. Hence any reference to the event
     * must be made before acknowledgment. Like, we have already saved the
     * client id from "id" field before acknowledging the event.
     */
    ret = rdma_ack_cm_event(cmEvent);
    check(ret, -errno, "Failed to acknowledge the cm event errno: %d \n", -errno);

    pr_info("A new RDMA client connection id is stored at %p\n", server->client->rc_client->cm_client_id);
    return ret;
}

int rc_accept_new_connection(struct server_info *server) {
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    struct sockaddr_in remote_sockaddr;
    int ret = -1;
    check(!server->client->rc_client->cm_client_id || !server->client->rc_client->cm_client_id, -EINVAL,
          "Client resources are not properly setup\n", -EINVAL);
    /* we prepare the receive buffer in which we will receive the client request*/
//    server->client->request = allocate_request();
    server->client->rc_client->request_mr = rdma_buffer_register(server->client->rc_client->pd /* which protection domain */,
                                                      server->client->request/* what memory */,
                                                      sizeof(struct request) /* what length */,
                                                      (IBV_ACCESS_LOCAL_WRITE |
                                                  IBV_ACCESS_REMOTE_READ | // TODO Remove this permission?
                                                  IBV_ACCESS_REMOTE_WRITE) /* access permissions */);
    check(!server->client->rc_client->request_mr, -ENOMEM, "Failed to register client attr buffer\n", -ENOMEM);

    ret = rc_post_receive_request(server->client);

    check(ret, ret, "Failed to pre-post the receive buffer, errno: %d \n", ret);

    pr_info("Receive buffer pre-posting is successful \n");
    /* Now we accept the connection. Recall we have not accepted the connection
     * yet because we have to do lots of resource pre-allocation */
    memset(&conn_param, 0, sizeof(conn_param));
    /* this tell how many outstanding requests can we handle */
    conn_param.initiator_depth = 3; /* For this exercise, we put a small number here */
    /* This tell how many outstanding requests we expect other side to handle */
    conn_param.responder_resources = 3; /* For this exercise, we put a small number */
    ret = rdma_accept(server->client->rc_client->cm_client_id, &conn_param);

    check(ret, -errno, "Failed to accept the connection, errno: %d \n", -errno);

    /* We expect an RDMA_CM_EVNET_ESTABLISHED to indicate that the RDMA
 * connection has been established and everything is fine on both, server
 * as well as the client sides.
 */
    pr_info("Going to wait for : RDMA_CM_EVENT_ESTABLISHED event \n");
    ret = process_rdma_cm_event(server->rc_server_info->cm_event_channel,
                                RDMA_CM_EVENT_ESTABLISHED,
                                &cm_event);
    check(ret, -errno, "Failed to get the cm event, errno: %d \n", -errno);
    /* We acknowledge the event */
    ret = rdma_ack_cm_event(cm_event);
    check(ret, -errno, "Failed to acknowledge the cm event %d\n", -errno);
    /* Just FYI: How to extract connection information */
    memcpy(&remote_sockaddr /* where to save */,
           rdma_get_peer_addr(server->client->rc_client->cm_client_id) /* gives you remote sockaddr */,
           sizeof(struct sockaddr_in) /* max size */);
    printf("A new connection is accepted from %s \n",
           inet_ntoa(remote_sockaddr.sin_addr));
    return ret;
}

int rc_receive_header(struct rc_client_connection *client) {
    int ret = 0, n = 0;
    struct ibv_wc wc;
    ret = process_work_completion_events(client->io_completion_channel, &wc, 1);
    check(ret < 0, -errno, "Failed to receive header: %d\n", ret);
    return ret;
}

int rc_post_receive_request(struct client_info *client) {
    int ret = 0;

    client->rc_client->client_recv_sge.addr = (uint64_t) client->rc_client->request_mr->addr;
    client->rc_client->client_recv_sge.length = client->rc_client->request_mr->length;
    client->rc_client->client_recv_sge.lkey = client->rc_client->request_mr->lkey;
    /* Now we link this SGE to the work request (WR) */
    bzero(&client->rc_client->client_recv_wr, sizeof(client->rc_client->client_recv_wr));
    client->rc_client->client_recv_wr.sg_list = &client->rc_client->client_recv_sge;
    client->rc_client->client_recv_wr.num_sge = 1; // only one SGE
    ret = ibv_post_recv(client->rc_client->client_qp /* which QP */,
                        &client->rc_client->client_recv_wr /* receive work request*/,
                        &client->rc_client->bad_client_recv_wr /* error WRs */);
    return ret;
}