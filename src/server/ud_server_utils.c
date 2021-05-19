//
// Created by Jens on 10/05/2021.
//

#include "ud_server_utils.h"
#include "rdma_common.h"

int setup_ud_client_resources(struct ud_client_connection *client) {
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
    client->qp_init_attr.qp_type = IBV_QPT_UD; /* QP type, UD = Unreliable datagram */
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

int ud_set_init_qp(struct ibv_qp *qp) {
    struct ibv_qp_attr dgram_attr = {
            .qp_state		= IBV_QPS_INIT,
            .pkey_index		= 0,
            .port_num		= IB_PHYS_PORT,
            .qkey 			= 0x11111111
    };

    int ret = ibv_modify_qp(qp, &dgram_attr,
                      IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY);
    check(ret, ret, "Failed to modify dgram. QP to INIT\n", NULL);
}

uint16_t get_local_lid(struct ibv_context *context)
{
    struct ibv_port_attr attr;

    if (ibv_query_port(context, IB_PHYS_PORT, &attr))
        return 0;

    return attr.lid;
}

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

    /* Now the last step, set up the queue pair (send, recv) queues and their capacity.
     * The capacity here is define statically but this can be probed from the
     * device. We just use a small number as defined in rdma_common.h */
    bzero(&server->ud_server_info->qp_init_attr, sizeof server->ud_server_info->qp_init_attr);
    server->ud_server_info->qp_init_attr.cap.max_recv_sge = MAX_SGE; /* Maximum SGE per receive posting */
    server->ud_server_info->qp_init_attr.cap.max_recv_wr = MAX_WR; /* Maximum receive posting capacity */
    server->ud_server_info->qp_init_attr.cap.max_send_sge = MAX_SGE; /* Maximum SGE per send posting */
    server->ud_server_info->qp_init_attr.cap.max_send_wr = MAX_WR; /* Maximum send posting capacity */
    server->ud_server_info->qp_init_attr.qp_type = IBV_QPT_UD; /* QP type, UD = Unreliable datagram */
    /* We use same completion queue, but one can use different queues */
    server->ud_server_info->qp_init_attr.recv_cq = server->ud_server_info->ud_cq; /* Where should I notify for receive completion operations */
    server->ud_server_info->qp_init_attr.send_cq = server->ud_server_info->ud_cq; /* Where should I notify for send completion operations */
    /*Lets create a QP */
    ret = rdma_create_qp(server->ud_server_info->cm_client_id /* which connection id */,
                         server->ud_server_info->pd /* which protection domain*/,
                         &server->ud_server_info->qp_init_attr /* Initial attributes */);
    check(ret, -errno, "Failed to create QP due to errno: %d\n", -errno);

    /* Save the reference for handy typing but is not required */
    server->ud_server_info->ud_qp = server->ud_server_info->cm_client_id->qp;
    pr_debug("Client QP created at %p\n", server->ud_server_info->ud_qp);

    ud_set_init_qp(server->ud_server_info->ud_qp);

    ibv_query_gid(context, IB_PHYS_PORT, 0, &server->ud_server_info->server_gid);
    server->ud_server_info->local_dgram_qp_attrs.gid_global_interface_id = server->ud_server_info->server_gid.global.interface_id;
    server->ud_server_info->local_dgram_qp_attrs.gid_global_subnet_prefix = server->ud_server_info->server_gid.global.subnet_prefix;
    server->ud_server_info->local_dgram_qp_attrs.lid = get_local_lid(context);
    server->ud_server_info->local_dgram_qp_attrs.qpn = server->ud_server_info->ud_qp->qp_num;
    server->ud_server_info->local_dgram_qp_attrs.psn = lrand48() & 0xffffff;

    // Set up UDP server to accept new clients


    pr_info("UD Server has been set up, ready to accept clients\n");

    return ret;
}

int ud_accept_new_connection(struct server_info *server, struct client_info *client) {
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = NULL;
    struct sockaddr_in remote_sockaddr;
    int ret = -1;

    /* now, we expect a client to connect and generate a RDMA_CM_EVNET_CONNECT_REQUEST
     * We wait (block) on the connection management event channel for
     * the connect event.
     */
    ret = process_rdma_cm_event(server->ud_server_info->cm_event_channel,
                                RDMA_CM_EVENT_CONNECT_REQUEST,
                                &cm_event);
    check(ret, ret, "Failed to get cm event, ret = %d \n", ret);

    pr_info("connected and setting client id\n");
    /* Much like TCP connection, listening returns a new connection identifier
     * for newly connected client. In the case of RDMA, this is stored in id
     * field. For more details: man rdma_get_cm_event
     */
    client->ud_client->cm_client_id = cm_event->id;

    pr_debug("setting client resources\n");
    setup_ud_client_resources(client->ud_client);

    /* now we acknowledge the event. Acknowledging the event free the resources
     * associated with the event structure. Hence any reference to the event
     * must be made before acknowledgment. Like, we have already saved the
     * client id from "id" field before acknowledging the event.
     */
    ret = rdma_ack_cm_event(cm_event);
    check(ret, -errno, "Failed to acknowledge the cm event errno: %d \n", -errno);

    pr_info("A new RDMA client connection id is stored at %p\n", client->ud_client->cm_client_id);

    check(!client->ud_client->cm_client_id || !client->ud_client->cm_client_id, -EINVAL,
          "Client resources are not properly setup\n", -EINVAL);
    /* we prepare the receive buffer in which we will receive the client request*/
    client->ud_client->request_mr = rdma_buffer_register(client->ud_client->pd /* which protection domain */,
                                                         client->request/* what memory */,
                                                         sizeof(struct request)*REQUEST_BACKLOG /* what length */,
                                                         (IBV_ACCESS_LOCAL_WRITE |
                                                          IBV_ACCESS_REMOTE_READ | // TODO Remove this permission?
                                                          IBV_ACCESS_REMOTE_WRITE) /* access permissions */);
    check(!client->ud_client->request_mr, -ENOMEM, "Failed to register client attr buffer\n", -ENOMEM);

    for (int i = 0; i < REQUEST_BACKLOG; i++) {
        client->request_count = i;
        ret = ud_post_receive_request(client);
        check(ret, ret, "Failed to pre-post the receive buffer %d, errno: %d \n", i, ret);
    }
    client->request_count = 0;

    /* now we register the metadata memory */
    client->ud_client->response_mr = rdma_buffer_register(client->ud_client->pd,
                                                          client->response,
                                                          sizeof(struct response),
                                                          IBV_ACCESS_LOCAL_WRITE);
    check(!client->ud_client->response_mr, ret, "Failed to register the client metadata buffer, ret = %d \n", ret);

    pr_info("Receive buffer pre-posting is successful \n");
    /* Now we accept the connection. Recall we have not accepted the connection
     * yet because we have to do lots of resource pre-allocation */
    memset(&conn_param, 0, sizeof(conn_param));
    /* this tell how many outstanding requests can we handle */
    conn_param.initiator_depth = 3; /* For this exeudise, we put a small number here */
    /* This tell how many outstanding requests we expect other side to handle */
    conn_param.responder_resources = 3; /* For this exercise, we put a small number */
    ret = rdma_accept(client->ud_client->cm_client_id, &conn_param);

    check(ret, -errno, "Failed to accept the connection, errno: %d \n", -errno);

    /* We expect an RDMA_CM_EVNET_ESTABLISHED to indicate that the RDMA
 * connection has been established and everything is fine on both, server
 * as well as the client sides.
 */
    pr_info("Going to wait for : RDMA_CM_EVENT_ESTABLISHED event \n");
    ret = process_rdma_cm_event(server->ud_server_info->cm_event_channel,
                                RDMA_CM_EVENT_ESTABLISHED,
                                &cm_event);
    check(ret, -errno, "Failed to get the cm event, errno: %d \n", -errno);
    /* We acknowledge the event */
    ret = rdma_ack_cm_event(cm_event);
    check(ret, -errno, "Failed to acknowledge the cm event %d\n", -errno);
    /* Just FYI: How to extract connection information */
    memcpy(&remote_sockaddr /* where to save */,
           rdma_get_peer_addr(client->ud_client->cm_client_id) /* gives you remote sockaddr */,
           sizeof(struct sockaddr_in) /* max size */);
    printf("A new connection is accepted from %s \n",
           inet_ntoa(remote_sockaddr.sin_addr));
    return ret;
}

int ud_receive_header(struct client_info *client);

int ud_post_receive_request(struct client_info *client);

int ud_send_response(struct client_info *client);