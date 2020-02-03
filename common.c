#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <infiniband/verbs.h>
#include "common.h"

void rdma_create_qp(struct ibv_cq **cq, struct ibv_qp **qp, uint32_t *lkey, const char *name, int cqe, int max_send_wr, int max_recv_wr, void *addr, size_t len, int access)
{
    int dev_nb;
    struct ibv_device **dev = ibv_get_device_list(&dev_nb);
    assert(dev && dev_nb && "ibv_get_device_list() failed");

    int i;
    for (i=0; i<dev_nb; i++)
        if (0 == strcmp(dev[i]->name, name))
            break;
    assert(i != dev_nb && "device not found");

    struct ibv_context *ctx = ibv_open_device(dev[i]);
    assert(ctx && "ibv_open_device() failed");

    *cq = ibv_create_cq(ctx, cqe, 0, 0, 0);
    assert(*cq && "ibv_create_cq() failed");

    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    assert(pd && "ibv_alloc_pd() failed");

    struct ibv_qp_init_attr qpia = {0};
    qpia.send_cq = *cq;
    qpia.recv_cq = *cq;
    qpia.cap.max_send_wr = max_send_wr;
    qpia.cap.max_send_sge = 1;
    qpia.cap.max_recv_wr = max_recv_wr;
    qpia.cap.max_recv_sge = 1;
    qpia.qp_type = IBV_QPT_RAW_PACKET;
    *qp = ibv_create_qp(pd, &qpia);
    assert(*qp && "ibv_create_qp() failed");

    struct ibv_qp_attr qpa = {0};
    qpa.qp_state = IBV_QPS_INIT;
    qpa.port_num = 1;
    int err = ibv_modify_qp (*qp, &qpa, IBV_QP_STATE | IBV_QP_PORT);
    assert(0 == err && "ibv_modify_qp(INIT) failed");

    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state = IBV_QPS_RTR;
    err = ibv_modify_qp (*qp, &qpa, IBV_QP_STATE);
    assert(0 == err && "ibv_modify_qp(RTR) failed");

    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state = IBV_QPS_RTS;
    err = ibv_modify_qp (*qp, &qpa, IBV_QP_STATE);
    assert(0 == err && "ibv_modify_qp(RTS) failed");

    struct ibv_mr *mr = ibv_reg_mr(pd, addr, len, access);
    assert(mr && "ibv_reg_mr() failed");
    *lkey = mr->lkey;
}
