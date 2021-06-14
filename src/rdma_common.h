//
// Created by jtroo on 15-04-21.
//

#ifndef RDMA_KV_STORE_RDMA_COMMON_H
#define RDMA_KV_STORE_RDMA_COMMON_H

/*
 * Header file for the common RDMA routines used in the server/client example
 * program.
 *
 * Author: Animesh Trivedi
 *          atrivedi@apache.org
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h>

#include "common.h"

#ifdef ACN_RDMA_DEBUG
/* Debug Macro */
#define debug(msg, args...) do {\
    printf("DEBUG: "msg, ## args);\
}while(0);

#else

#define debug(msg, args...)

#endif /* ACN_RDMA_DEBUG */

/* Capacity of the completion queue (CQ) */
#define CQ_CAPACITY (16)
//#define CQ_CAPACITY (1025) // HERD
/* MAX SGE capacity */
#define MAX_SGE (2)
/* MAX work requests */
#define MAX_WR (8)
//#define MAX_WR (1024) // HERD TODO check how this affects stuff
/* Default port where the RDMA server is listening */
#define DEFAULT_RDMA_PORT (20886)

#define IB_PHYS_PORT 1			// HERD, Primary physical port number for qps

#define MAX_POLL_CQ_TIMEOUT 5
/*
 * We use attribute so that compiler does not step in and try to pad the structure.
 * We use this structure to exchange information between the server and the client.
 *
 * For details see: http://gcc.gnu.org/onlinedocs/gcc/Type-Attributes.html
 */
struct __attribute((packed)) rdma_buffer_attr {
    uint64_t address;
    uint32_t length;
    union stag {
        /* if we send, we call it local stags */
        uint32_t local_stag;
        /* if we receive, we call it remote stag */
        uint32_t remote_stag;
    } stag;
    uint32_t rkey;
};

struct qp_attr {
    uint64_t gid_global_interface_id;	// Store the gid fields separately because I
    uint64_t gid_global_subnet_prefix; 	// don't like unions. Needed for RoCE only

    int client_id;
    int lid;							// A queue pair is identified by the local id (lid)
    unsigned int qpn;							// of the device port and its queue pair number (qpn)
    int psn;
};

struct ud_request {
    char grh[40];
    struct request request;
};

struct ud_response {
    char grh[40];
    struct response response;
};

/* resolves a given destination name to sin_addr */
int get_addr(char *dst, struct sockaddr *addr);

/* prints RDMA buffer info structure */
void show_rdma_buffer_attr(struct rdma_buffer_attr *attr);

/*
 * Processes an RDMA connection management (CM) event.
 * @echannel: CM event channel where the event is expected.
 * @expected_event: Expected event type
 * @cm_event: where the event will be stored
 */
int process_rdma_cm_event(struct rdma_event_channel *echannel,
                          enum rdma_cm_event_type expected_event,
                          struct rdma_cm_event **cm_event);

/* Allocates an RDMA buffer of size 'length' with permission permission. This
 * function will also register the memory and returns a memory region (MR)
 * identifier or NULL on error.
 * @pd: Protection domain where the buffer should be allocated
 * @length: Length of the buffer
 * @permission: OR of IBV_ACCESS_* permissions as defined for the enum ibv_access_flags
 */
struct ibv_mr *rdma_buffer_alloc(struct ibv_pd *pd,
                                 uint32_t length,
                                 enum ibv_access_flags permission);

/* Frees a previously allocated RDMA buffer. The buffer must be allocated by
 * calling rdma_buffer_alloc();
 * @mr: RDMA memory region to free
 */
void rdma_buffer_free(struct ibv_mr *mr);

/* This function registers a previously allocated memory. Returns a memory region
 * (MR) identifier or NULL on error.
 * @pd: protection domain where to register memory
 * @addr: Buffer address
 * @length: Length of the buffer
 * @permission: OR of IBV_ACCESS_* permissions as defined for the enum ibv_access_flags
 */
struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd,
                                    void *addr,
                                    uint32_t length,
                                    enum ibv_access_flags permission);

/* Deregisters a previously register memory
 * @mr: Memory region to deregister
 */
void rdma_buffer_deregister(struct ibv_mr *mr);

/* Processes a work completion (WC) notification.
 * @comp_channel: Completion channel where the notifications are expected to arrive
 * @wc: Array where to hold the work completion elements
 * @max_wc: Maximum number of expected work completion (WC) elements. wc must be
 *          atleast this size.
 */
int process_work_completion_events(struct ibv_comp_channel *comp_channel, struct ibv_wc *wc, int max_wc,
                                   struct ibv_cq *cq_ptr);
int process_work_completion_events_with_timeout(struct ibv_wc *wc, int max_wc,
                                                struct ibv_cq *cq_ptr);
/* prints some details from the cm id */
void show_rdma_cmid(struct rdma_cm_id *id);

int post_recieve(size_t size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, void *buf);

int post_send(size_t size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, void *buf);
int
ud_post_send(size_t size, uint32_t lkey, uint64_t wr_id, struct ibv_qp *qp, void *buf, struct ibv_ah *ah, uint32_t qpn);
int ud_set_init_qp(struct ibv_qp *qp);
int ud_set_rts_qp(struct ibv_qp *qp, int psn);
uint16_t get_local_lid(struct ibv_context *context);
int connect_qp(struct ibv_qp *conn_qp, struct qp_attr *local, struct qp_attr *remote);
int uc_set_init_qp(struct ibv_qp *qp);
#endif //RDMA_KV_STORE_RDMA_COMMON_H
