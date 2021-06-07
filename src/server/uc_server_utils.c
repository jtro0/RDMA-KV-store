//
// Created by jtroo on 15-04-21.
//

#include "uc_server_utils.h"
#include "rdma_common.h"

int setup_uc_client_resources(struct uc_client_connection *client) {
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
    pr_debug("allocating pd\n");
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
    client->qp_init_attr.qp_type = IBV_QPT_UC; /* QP type, UC = Unreliable connection */
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

int init_uc_server(struct server_info *server) {
    int ret = -1;

    /*  Open a channel used to report asynchronous communication event */
    server->uc_server_info->cm_event_channel = rdma_create_event_channel();
    check(!server->uc_server_info->cm_event_channel, -errno,
          "Creating cm event channel failed with errno : (%d)", -errno);

    pr_info("RDMA CM event channel is created successfully at %p \n",
            server->uc_server_info->cm_event_channel);

    /* rdma_cm_id is the connection identifier (like socket) which is used
     * to define an RDMA connection.
     */
    ret = rdma_create_id(server->uc_server_info->cm_event_channel, &server->uc_server_info->cm_server_id, NULL,
                         RDMA_PS_TCP);
    check(ret, -errno, "Creating server cm id failed with errno: %d ", -errno);

    pr_info("A RDMA connection id for the server is created \n");

    /* Explicit binding of rdma cm id to the socket credentials */
    ret = rdma_bind_addr(server->uc_server_info->cm_server_id, (struct sockaddr *) &server->addr);
    check(ret, -errno, "Failed to bind server address, errno: %d \n", -errno);

    pr_info("Server RDMA CM id is successfully bound \n");

    // TODO put this into accept? for multiple clients?
    /* Now we start to listen on the passed IP and port. However unlike
     * normal TCP listen, this is a non-blocking call. When a new client is
     * connected, a new connection management (CM) event is generated on the
     * RDMA CM event channel from where the listening id was created. Here we
     * have only one channel, so it is easy. */
    ret = rdma_listen(server->uc_server_info->cm_server_id, 8); /* backlog = 8 clients, same as TCP, see man listen*/
    check(ret, -errno, "rdma_listen failed to listen on server address, errno: %d ",
          -errno);

    printf("Server is listening successfully at: %s , port: %d \n",
           inet_ntoa(server->addr.sin_addr),
           ntohs(server->addr.sin_port));

    return ret;
}

int uc_accept_new_connection(struct server_info *server, struct client_info *client) {
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    struct sockaddr_in remote_sockaddr;
    int ret = -1;

    /* now, we expect a client to connect and generate a RDMA_CM_EVNET_CONNECT_REQUEST
     * We wait (block) on the connection management event channel for
     * the connect event.
     */
    ret = process_rdma_cm_event(server->uc_server_info->cm_event_channel,
                                RDMA_CM_EVENT_CONNECT_REQUEST,
                                &cm_event);
    check(ret, ret, "Failed to get cm event, ret = %d \n", ret);

    pr_info("connected and setting client id\n");
    /* Much like TCP connection, listening returns a new connection identifier
     * for newly connected client. In the case of RDMA, this is stored in id
     * field. For more details: man rdma_get_cm_event
     */
    client->uc_client->cm_client_id = cm_event->id;

    pr_debug("setting client resources\n");
    setup_uc_client_resources(client->uc_client);

    /* now we acknowledge the event. Acknowledging the event free the resources
     * associated with the event structure. Hence any reference to the event
     * must be made before acknowledgment. Like, we have already saved the
     * client id from "id" field before acknowledging the event.
     */
//    ret = rdma_ack_cm_event(cm_event);
//    check(ret, -errno, "Failed to acknowledge the cm event errno: %d \n", -errno);

    pr_info("A new RDMA client connection id is stored at %p\n", client->uc_client->cm_client_id);

    check(!client->uc_client->cm_client_id || !client->uc_client->cm_client_id, -EINVAL,
          "Client resources are not properly setup\n", -EINVAL);
    /* we prepare the receive buffer in which we will receive the client request*/
    client->uc_client->request_mr = rdma_buffer_register(client->uc_client->pd /* which protection domain */,
                                                      client->request/* what memory */,
                                                      sizeof(struct request)*REQUEST_BACKLOG /* what length */,
                                                      (IBV_ACCESS_LOCAL_WRITE |
                                                  IBV_ACCESS_REMOTE_READ | // TODO Remove this permission?
                                                  IBV_ACCESS_REMOTE_WRITE) /* access permissions */);
    check(!client->uc_client->request_mr, -ENOMEM, "Failed to register client attr buffer\n", -ENOMEM);

    for (int i = 0; i < REQUEST_BACKLOG; i++) {
        client->request_count = i;
        ret = uc_post_receive_request(client);
        check(ret, ret, "Failed to pre-post the receive buffer %d, errno: %d \n", i, ret);
    }
    client->request_count = 0;

    /* now we register the metadata memory */
    client->uc_client->response_mr = rdma_buffer_register(client->uc_client->pd,
                                                                  client->response,
                                                          sizeof(struct response),
                                                          IBV_ACCESS_LOCAL_WRITE);
    check(!client->uc_client->response_mr, ret, "Failed to register the client metadata buffer, ret = %d \n", ret);

    pr_info("Receive buffer pre-posting is successful \n");
    /* Now we accept the connection. Recall we have not accepted the connection
     * yet because we have to do lots of resource pre-allocation */
    memset(&conn_param, 0, sizeof(conn_param));
    /* this tell how many outstanding requests can we handle */
    conn_param.initiator_depth = 3; /* For this exercise, we put a small number here */
    /* This tell how many outstanding requests we expect other side to handle */
    conn_param.responder_resources = 3; /* For this exercise, we put a small number */
    ret = rdma_accept(client->uc_client->cm_client_id, &conn_param);

    check(ret, -errno, "Failed to accept the connection, errno: %d \n", -errno);

    /* We expect an RDMA_CM_EVNET_ESTABLISHED to indicate that the RDMA
 * connection has been established and everything is fine on both, server
 * as well as the client sides.
 */
    pr_info("Going to wait for : RDMA_CM_EVENT_ESTABLISHED event \n");
    ret = process_rdma_cm_event(server->uc_server_info->cm_event_channel,
                                RDMA_CM_EVENT_ESTABLISHED,
                                &cm_event);
    check(ret, -errno, "Failed to get the cm event, errno: %d \n", -errno);
    /* We acknowledge the event */
    ret = rdma_ack_cm_event(cm_event);
    check(ret, -errno, "Failed to acknowledge the cm event %d\n", -errno);
    /* Just FYI: How to extract connection information */
    memcpy(&remote_sockaddr /* where to save */,
           rdma_get_peer_addr(client->uc_client->cm_client_id) /* gives you remote sockaddr */,
           sizeof(struct sockaddr_in) /* max size */);
    printf("A new connection is accepted from %s \n",
           inet_ntoa(remote_sockaddr.sin_addr));
    return ret;
}

int uc_receive_header(struct client_info *client) {
    int ret = 0;
    struct ibv_wc wc;
    ret = process_work_completion_events(client->uc_client->io_completion_channel, &wc, 1, client->uc_client->cq);
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
    ret = process_work_completion_events(client->uc_client->io_completion_channel, &wc, 1, client->uc_client->cq);
    check(ret < 0, -errno, "Failed to send response: %d\n", ret);
    return ret;
}