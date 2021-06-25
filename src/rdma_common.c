//
// Created by jtroo on 15-04-21.
//


#include <sys/time.h>
#include "rdma_common.h"

struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd,
                                    void *addr, uint32_t length,
                                    enum ibv_access_flags permission) {
    struct ibv_mr *mr = NULL;
    check(!pd, NULL, "Protection domain is NULL\n", NULL);

    mr = ibv_reg_mr(pd, addr, length, permission);
    check(!mr, NULL, "Failed to create mr on buffer, errno: %d \n", -errno);
    pr_debug("Registered: %p , len: %u , stag: 0x%x \n",
             mr->addr,
             (unsigned int) mr->length,
             mr->lkey);
    return mr;
}

void rdma_buffer_deregister(struct ibv_mr *mr) {
    if (!mr) {
        error("Passed memory region is NULL, ignoring\n");
        return;
    }
    pr_debug("Deregistered: %p , len: %u , stag : 0x%x \n",
             mr->addr,
             (unsigned int) mr->length,
             mr->lkey);
    ibv_dereg_mr(mr);
}

int process_rdma_cm_event(struct rdma_event_channel *echannel,
                          enum rdma_cm_event_type expected_event,
                          struct rdma_cm_event **cm_event) {
    int ret = 1;
    ret = rdma_get_cm_event(echannel, cm_event);
    check(ret, -errno, "Failed to retrieve a cm event, errno: %d \n",
          -errno);

    /* lets see, if it was a good event */
    if (0 != (*cm_event)->status) {
        error("CM event has non zero status: %d\n", (*cm_event)->status);
        ret = -((*cm_event)->status);
        /* important, we acknowledge the event */
        rdma_ack_cm_event(*cm_event);
        return ret;
    }
    /* if it was a good event, was it of the expected type */
    if ((*cm_event)->event != expected_event) {
        error("Unexpected event received: %s [ expecting: %s ]",
              rdma_event_str((*cm_event)->event),
              rdma_event_str(expected_event));
        /* important, we acknowledge the event */
        rdma_ack_cm_event(*cm_event);
        return -1; // unexpected event :(
    }
    pr_debug("A new %s type event is received \n", rdma_event_str((*cm_event)->event));
    /* The caller must acknowledge the event */
    return ret;
}


int process_work_completion_events(struct ibv_comp_channel *comp_channel, struct ibv_wc *wc, int max_wc,
                                   struct ibv_cq *cq_ptr, pthread_mutex_t *lock, int blocking) {
    void *context = NULL;
    int ret = -1, i, total_wc = 0;

    /*
     * We can choose to block and wait for completion event
     */
    if (blocking) {
        cq_ptr = NULL;
        ret = ibv_get_cq_event(comp_channel, /* IO channel where we are expecting the notification */
                               &cq_ptr, /* which CQ has an activity. This should be the same as CQ we created before */
                               &context); /* Associated CQ user context, which we did set */
        check(ret, -errno, "Failed to get next CQ event due to %d \n", -errno);

        /* Request for more notifications. */
        ret = ibv_req_notify_cq(cq_ptr, 0);
        check(ret, -errno, "Failed to request further notifications %d \n", -errno);
    }

    /*
     * Poll on the completion queue for new events. If blocked before, then this contains a WC.
     */
    total_wc = 0;
    do {
        ret = ibv_poll_cq(cq_ptr /* the CQ, we got notification for */,
                          max_wc - total_wc /* number of remaining WC elements*/,
                          wc + total_wc/* where to store */);
        check(ret < 0, ret, "Failed to poll cq for wc due to %d \n", ret);
        total_wc += ret;
    } while (total_wc < max_wc);
    pr_debug("%d WC are completed \n", total_wc);
    /* Now we check validity and status of I/O work completions */
    for (i = 0; i < total_wc; i++) {
        if (wc[i].status != IBV_WC_SUCCESS) {
            error("Work completion (WC) has error status: %s at index %d",
                  ibv_wc_status_str(wc[i].status), i);
            /* return negative value */
            return -(wc[i].status);
        }
    }
    if (blocking) {
        ibv_ack_cq_events(cq_ptr,
                          1 /* we received one event notification.*/);
    }
    return total_wc;
}

/*
 * Same principle as above, but now with a timeout defined in rdma_common.h
 */
int process_work_completion_events_with_timeout(struct ibv_wc *wc, int max_wc, struct ibv_cq *cq_ptr,
                                                struct ibv_comp_channel *comp_channel, int blocking) {
    void *context = NULL;
    int ret = -1, i, total_wc = 0;

    if (blocking) {
        cq_ptr = NULL;
        /* We wait for the notification on the CQ channel */
        ret = ibv_get_cq_event(comp_channel, /* IO channel where we are expecting the notification */
                               &cq_ptr, /* which CQ has an activity. This should be the same as CQ we created before */
                               &context); /* Associated CQ user context, which we did set */
        check(ret, -errno, "Failed to get next CQ event due to %d \n", -errno);

        /* Request for more notifications. */
        ret = ibv_req_notify_cq(cq_ptr, 0);
        check(ret, -errno, "Failed to request further notifications %d \n", -errno);
    }

    struct timeval time;
    unsigned long start_time_msec, current_time_msec;

    // Start timer
    gettimeofday(&time, NULL);
    start_time_msec = (time.tv_sec * 1000) + (time.tv_usec / 1000);

    total_wc = 0;
    do {
        ret = ibv_poll_cq(cq_ptr /* the CQ, we got notification for */,
                          max_wc - total_wc /* number of remaining WC elements*/,
                          wc + total_wc/* where to store */);
        check(ret < 0, ret, "Failed to poll cq for wc due to %d \n", ret);
        total_wc += ret;
        gettimeofday(&time, NULL); // Stop timer
        current_time_msec = (time.tv_sec * 1000) + (time.tv_usec / 1000);
    } while (total_wc < max_wc && ((current_time_msec - start_time_msec) < MAX_POLL_CQ_TIMEOUT)); // Break if we got all WC we requests, or time has run out
    pr_debug("%d WC are completed \n", total_wc);
    /* Now we check validity and status of I/O work completions */
    for (i = 0; i < total_wc; i++) {
        if (wc[i].status != IBV_WC_SUCCESS) {
            error("Work completion (WC) has error status: %s at index %d",
                  ibv_wc_status_str(wc[i].status), i);
            /* return negative value */
            return -(wc[i].status);
        }
    }
    if (blocking) {
        ibv_ack_cq_events(cq_ptr,
                          1 /* we received one event notification.*/);
    }
    return total_wc;
}


/* Code acknowledgment: rping.c from librdmacm/examples */
int get_addr(char *dst, struct sockaddr *addr) {
    struct addrinfo *res;
    int ret = -1;
    ret = getaddrinfo(dst, NULL, NULL, &res);
    check(ret, ret, "getaddrinfo failed - invalid hostname or IP address\n", ret);
    memcpy(addr, res->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo(res);
    return ret;
}

// https://github.com/jcxue/RDMA-Tutorial/blob/65893ec63c9eb004c310e25112c73a80d026c2c3/ib.c
int post_recieve(size_t size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, void *buf) {
    int ret = 0;
    struct ibv_recv_wr *bad_recv_wr;

    struct ibv_sge list = {
            .addr   = (uintptr_t) buf,
            .length = size,
            .lkey   = lkey
    };

    struct ibv_recv_wr recv_wr = {
            .wr_id = wr_id,
            .sg_list = &list,
            .num_sge = 1
    };

    ret = ibv_post_recv(qp, &recv_wr, &bad_recv_wr);
    return ret;
}

int post_send(size_t size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, void *buf) {
    int ret = 0;
    struct ibv_send_wr *bad_send_wr;

    struct ibv_sge list = {
            .addr   = (uintptr_t) buf,
            .length = size,
            .lkey   = lkey
    };

    struct ibv_send_wr send_wr = {
            .wr_id      = wr_id,
            .sg_list    = &list,
            .num_sge    = 1,
            .opcode     = IBV_WR_SEND,
            .send_flags = IBV_SEND_SIGNALED
    };

    ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    return ret;
}

/*
 * Similar as post_send(), but now with AH
 */
int ud_post_send(size_t size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, void *buf, struct ibv_ah *ah,
                 uint32_t qpn) {
    int ret = 0;
    struct ibv_send_wr *bad_send_wr;

    struct ibv_sge list = {
            .addr   = (uint64_t) buf,
            .length = size,
            .lkey   = lkey
    };

    struct ibv_send_wr send_wr = {
            .wr_id      = wr_id,
            .sg_list    = &list,
            .num_sge    = 1,
            .opcode     = IBV_WR_SEND,
            .send_flags = IBV_SEND_SIGNALED,
            .wr.ud.ah = ah, // Give the route it needs to take
            .wr.ud.remote_qpn = qpn,
            .wr.ud.remote_qkey = 0x11111111
    };

    ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    return ret;
}

int ud_set_init_qp(struct ibv_qp *qp) {
    struct ibv_qp_attr dgram_attr = {
            .qp_state        = IBV_QPS_INIT,
            .pkey_index        = 0,
            .port_num        = IB_PHYS_PORT,
            .qkey            = 0x11111111
    };

    int ret = ibv_modify_qp(qp, &dgram_attr,
                            IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY);

    check(ret, ret, "Failed to modify dgram. QP to INIT\n", NULL);
}

int ud_set_rts_qp(struct ibv_qp *qp, int psn) {
    struct ibv_qp_attr dgram_attr = {
            .qp_state		= IBV_QPS_RTR,
    };

    int ret = ibv_modify_qp(qp, &dgram_attr, IBV_QP_STATE);
    check(ret, ret, "Failed to modify dgram. QP to RTR\n", NULL);

    dgram_attr.qp_state = IBV_QPS_RTS;
    dgram_attr.sq_psn = psn;

    ret = ibv_modify_qp(qp, &dgram_attr, IBV_QP_STATE | IBV_QP_SQ_PSN);
    check(ret, ret, "Failed to modify dgram. QP to RTS\n", NULL);

    return 0;
}

int uc_set_init_qp(struct ibv_qp *qp) {
    int ret = 0;
    struct ibv_qp_attr conn_attr = {
            .qp_state		= IBV_QPS_INIT,
            .pkey_index		= 0,
            .port_num		= IB_PHYS_PORT,
            .qp_access_flags = 0
    };

    ret = ibv_modify_qp(qp, &conn_attr,
                  IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
    check(ret, ret, "Failed to modify conn. QP to INIT\n", NULL);

    return ret;
}

uint16_t get_local_lid(struct ibv_context *context)
{
    struct ibv_port_attr attr;

    if (ibv_query_port(context, IB_PHYS_PORT, &attr))
        return 0;

    return attr.lid;
}


int connect_qp(struct ibv_qp *conn_qp, struct qp_attr *local, struct qp_attr *remote) {
    int ret = 0;
    struct ibv_qp_attr conn_attr = {
            .qp_state = IBV_QPS_RTR,
            .path_mtu = IBV_MTU_4096,
            .dest_qp_num = remote->qpn,
            .rq_psn = remote->psn,
            .ah_attr = {
                    .is_global = 0,
                    .dlid = remote->lid,
                    .sl = 0,
                    .src_path_bits = 0,
                    .port_num = IB_PHYS_PORT
            }
    };

    int rtr_flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN
                    | IBV_QP_RQ_PSN;
    ret = ibv_modify_qp(conn_qp, &conn_attr, rtr_flags);
    check(ret, ret, "Failed to modify QP to RTR\n", NULL);

    memset(&conn_attr, 0, sizeof(conn_attr));
    conn_attr.qp_state	    = IBV_QPS_RTS;
    conn_attr.sq_psn	    = local->psn;

    int rts_flags = IBV_QP_STATE | IBV_QP_SQ_PSN;
    ret = ibv_modify_qp(conn_qp, &conn_attr, rts_flags);
    check(ret, ret, "Failed to modify QP to RTS\n", NULL);
}