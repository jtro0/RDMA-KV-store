//
// Created by jtroo on 15-04-21.
//

/*
 * Implementation of the common RDMA functions.
 *
 * Authors: Animesh Trivedi
 *          atrivedi@apache.org
 */

#include <sys/time.h>
#include "rdma_common.h"

void show_rdma_cmid(struct rdma_cm_id *id) {
    if (!id) {
        error("Passed ptr is NULL\n");
        return;
    }
    printf("RDMA cm id at %p \n", id);
    if (id->verbs && id->verbs->device)
        printf("dev_ctx: %p (device name: %s) \n", id->verbs,
               id->verbs->device->name);
    if (id->channel)
        printf("cm event channel %p\n", id->channel);
    printf("QP: %p, port_space %x, port_num %u \n", id->qp,
           id->ps,
           id->port_num);
}

void show_rdma_buffer_attr(struct rdma_buffer_attr *attr) {
    if (!attr) {
        error("Passed attr is NULL\n");
        return;
    }
    printf("---------------------------------------------------------\n");
    printf("buffer attr, addr: %p , len: %u , stag : 0x%x \n",
           (void *) attr->address,
           (unsigned int) attr->length,
           attr->stag.local_stag);
    printf("---------------------------------------------------------\n");
}

struct ibv_mr *rdma_buffer_alloc(struct ibv_pd *pd, uint32_t size,
                                 enum ibv_access_flags permission) {
    struct ibv_mr *mr = NULL;
    check(!pd, NULL, "Protection domain is NULL\n", NULL);
    void *buf = calloc(1, size);
    check(!buf, NULL, "failed to allocate buffer\n", NULL);
    pr_debug("Buffer allocated: %p , len: %u \n", buf, size);
    mr = rdma_buffer_register(pd, buf, size, permission);
    if (!mr) {
        free(buf);
    }
    return mr;
}

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

void rdma_buffer_free(struct ibv_mr *mr) {
    if (!mr) {
        error("Passed memory region is NULL, ignoring\n");
        return;
    }
    void *to_free = mr->addr;
    rdma_buffer_deregister(mr);
    pr_debug("Buffer %p free'ed\n", to_free);
    free(to_free);
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
                                   struct ibv_cq *cq_ptr) {
//    struct ibv_cq *cq_ptr = NULL;
    void *context = NULL;
    int ret = -1, i, total_wc = 0;
    /* We wait for the notification on the CQ channel */
//    ret = ibv_get_cq_event(comp_channel, /* IO channel where we are expecting the notification */
//                           &cq_ptr, /* which CQ has an activity. This should be the same as CQ we created before */
//                           &context); /* Associated CQ user context, which we did set */
//    check(ret, -errno, "Failed to get next CQ event due to %d \n", -errno);
//
//    /* Request for more notifications. */
//    ret = ibv_req_notify_cq(cq_ptr, 0);
//    check(ret, -errno, "Failed to request further notifications %d \n", -errno);

    /* We got notification. We reap the work completion (WC) element. It is
 * unlikely but a good practice it write the CQ polling code that
    * can handle zero WCs. ibv_poll_cq can return zero. Same logic as
    * MUTEX conditional variables in pthread programming.
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
    /* Similar to connection management events, we need to acknowledge CQ events */
    ibv_ack_cq_events(cq_ptr,
                      1 /* we received one event notification. This is not
		       number of WC elements */);
    return total_wc;
}

int process_work_completion_events_with_timeout(struct ibv_wc *wc, int max_wc,
                                   struct ibv_cq *cq_ptr) {
//    struct ibv_cq *cq_ptr = NULL;
    void *context = NULL;
    int ret = -1, i, total_wc = 0;

    struct timeval time;
    unsigned long start_time_msec, current_time_msec;

    gettimeofday(&time, NULL);
    start_time_msec = (time.tv_sec * 1000) + (time.tv_usec / 1000);

    /* We got notification. We reap the work completion (WC) element. It is
 * unlikely but a good practice it write the CQ polling code that
    * can handle zero WCs. ibv_poll_cq can return zero. Same logic as
    * MUTEX conditional variables in pthread programming.
 */
    total_wc = 0;
    do {
        ret = ibv_poll_cq(cq_ptr /* the CQ, we got notification for */,
                          max_wc - total_wc /* number of remaining WC elements*/,
                          wc + total_wc/* where to store */);
        check(ret < 0, ret, "Failed to poll cq for wc due to %d \n", ret);
        total_wc += ret;
        gettimeofday(&time, NULL);
        current_time_msec = (time.tv_sec * 1000) + (time.tv_usec / 1000);
    } while (total_wc < max_wc && ((current_time_msec - start_time_msec) < MAX_POLL_CQ_TIMEOUT));
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
    /* Similar to connection management events, we need to acknowledge CQ events */
//    ibv_ack_cq_events(cq_ptr,
//                      1 /* we received one event notification. This is not
//		       number of WC elements */);
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
            .wr.ud.ah = ah,
            .wr.ud.remote_qpn = qpn,
            .wr.ud.remote_qkey = 0x11111111
    };

    printf("send %p %p %p %p %p", buf, &send_wr, &bad_send_wr, qp, ah);

    ret = ibv_post_send(qp, &send_wr, &bad_send_wr);
    return ret;
}

int ud_set_init_qp(struct ibv_qp *qp) {
    struct ibv_qp_attr dgram_attr = {
            .qp_state		= IBV_QPS_INIT,
            .pkey_index		= 0,
            .port_num		= IB_PHYS_PORT,
            .qkey 			= 0x11111111
    };
    pr_info("In here\n");

    int ret = ibv_modify_qp(qp, &dgram_attr,
                            IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY);
    pr_info("After here\n");

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

uint16_t get_local_lid(struct ibv_context *context)
{
    struct ibv_port_attr attr;

    if (ibv_query_port(context, IB_PHYS_PORT, &attr))
        return 0;

    return attr.lid;
}
