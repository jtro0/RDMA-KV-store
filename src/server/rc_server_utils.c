//
// Created by jtroo on 15-04-21.
//

#include "rc_server_utils.h"

int init_rc_server(struct conn_info *connInfo) {
    struct rdma_cm_event *cmEvent = NULL;
    int ret = -1;

    /*  Open a channel used to report asynchronous communication event */
    connInfo->rc_connection->cm_event_channel = rdma_create_event_channel();
    check_for_error(!connInfo->rc_connection->cm_event_channel, "Creating cm event channel failed with errno : (%d)", -errno);

    pr_info("RDMA CM event channel is created successfully at %p \n",
            connInfo->rc_connection->cm_event_channel);

    /* rdma_cm_id is the connection identifier (like socket) which is used
     * to define an RDMA connection.
     */
    ret = rdma_create_id(connInfo->rc_connection->cm_event_channel, &connInfo->rc_connection->cm_server_id, NULL, RDMA_PS_TCP);
    check_for_error(ret, "Creating server cm id failed with errno: %d ", -errno);

    pr_info("A RDMA connection id for the server is created \n");

    /* Explicit binding of rdma cm id to the socket credentials */
    ret = rdma_bind_addr(connInfo->rc_connection->cm_server_id, (struct sockaddr*) &connInfo->addr);
    check_for_error(ret, "Failed to bind server address, errno: %d \n", -errno);

    pr_info("Server RDMA CM id is successfully bound \n");

    /* Now we start to listen on the passed IP and port. However unlike
     * normal TCP listen, this is a non-blocking call. When a new client is
     * connected, a new connection management (CM) event is generated on the
     * RDMA CM event channel from where the listening id was created. Here we
     * have only one channel, so it is easy. */
    ret = rdma_listen(connInfo->rc_connection->cm_server_id, 8); /* backlog = 8 clients, same as TCP, see man listen*/
    check_for_error(ret, "rdma_listen failed to listen on server address, errno: %d ",
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
    check_for_error(ret, "Failed to get cm event, ret = %d \n" , ret);

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
    check_for_error(ret, "Failed to acknowledge the cm event errno: %d \n", -errno);

    pr_info("A new RDMA client connection id is stored at %p\n", connInfo->rc_connection->cm_client_id);
    return ret;
}
