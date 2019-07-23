#include <stdint.h>
#include <stdio.h>
#include <infiniband/verbs.h>

#define TXSZ    2048
#define TXNB    2

static const char packet[] = {
    /* ethernet: src=00:0c:41:82:b2:53, dst=00:d0:59:6c:40:4e, type=0x0800 (IPv4) */
    0x00, 0x0c, 0x41, 0x82, 0xb2, 0x53, 0x00, 0xd0, 0x59, 0x6c, 0x40, 0x4e, 0x08, 0x00,
    /* ipv4: src=192.168.50.50, dst=192.168.0.1, proto=17 (UDP) */
    0x45, 0x00, 0x00, 0x3d, 0x0a, 0x41, 0x00, 0x00, 0x80, 0x11, 0x7c, 0xeb, 0xc0, 0xa8, 0x32, 0x32, 0xc0, 0xa8, 0x00, 0x01,
    /* udp: sport=1026, dport=53 */
    0x04, 0x02, 0x00, 0x35, 0x00, 0x29, 0x01, 0xab,
    /* DNS Standard query 0x002b A us.pool.ntp.org */
    0x00, 0x2b, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x75, 0x73, 0x04, 0x70, 0x6f, 0x6f, 0x6c, 0x03, 0x6e, 0x74, 0x70, 0x03, 0x6f, 0x72, 0x67, 0x00, 0x00, 0x01, 0x00, 0x01
};

int main(int argc, const char **argv)
{
    assert(2 == argc && "wrong arguments");

    int dev_nb;
    struct ibv_device **dev = ibv_get_device_list(&dev_nb);
    assert(dev && dev_nb && "ibv_get_device_list() failed");

    int i;
    for (i=0; i<dev_nb; i++)
        if (0 == strcmp(dev[i]->name, argv[1]))
            break;
    assert(i != dev_nb && "device not found");

    struct ibv_context *ctx = ibv_open_device(dev[i]);
    assert(ctx && "ibv_open_device() failed");

    struct ibv_cq *cq = ibv_create_cq(ctx, TXNB, 0, 0, 0);
    assert(cq && "ibv_create_cq() failed");

    struct ibv_pd *pd = ibv_alloc_pd(ctx);
    assert(pd && "ibv_alloc_pd() failed");

    struct ibv_qp_init_attr qpia = {0};
    qpia.send_cq = cq;
    qpia.recv_cq = cq;
    qpia.cap.max_send_wr = TXSZ * TXNB;
    qpia.cap.max_send_sge = 1;
    qpia.qp_type = IBV_QPT_RAW_PACKET;
    struct ibv_qp *qp = ibv_create_qp(pd, &qpia);
    assert(qp && "ibv_create_qp() failed");

    struct ibv_qp_attr qpa = {0};
    qpa.qp_state = IBV_QPS_INIT;
    qpa.port_num = 1;
    int err = ibv_modify_qp (qp, &qpa, IBV_QP_STATE | IBV_QP_PORT);
    assert(0 == err && "ibv_modify_qp(INIT) failed");

    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state = IBV_QPS_RTR;
    err = ibv_modify_qp (qp, &qpa, IBV_QP_STATE);
    assert(0 == err && "ibv_modify_qp(RTR) failed");

    memset(&qpa, 0, sizeof(qpa));
    qpa.qp_state = IBV_QPS_RTS;
    err = ibv_modify_qp (qp, &qpa, IBV_QP_STATE);
    assert(0 == err && "ibv_modify_qp(RTS) failed");

    struct ibv_mr *mr = ibv_reg_mr(pd, (void *)packet, sizeof(packet), IBV_ACCESS_REMOTE_READ);
    assert(mr && "ibv_reg_mr() failed");

    static struct ibv_sge sge[TXNB][TXSZ];
    static struct ibv_send_wr wr[TXNB][TXSZ];
    for (int i=0; i<TXNB; i++) {
        for (int j=0; j<TXSZ; j++) {
            sge[i][j].addr = (uintptr_t)packet;
            sge[i][j].length = sizeof(packet);
            sge[i][j].lkey = mr->lkey;
            wr[i][j].wr_id = i;
            wr[i][j].next = &wr[i][j+1];
            wr[i][j].sg_list = &sge[i][j];
            wr[i][j].num_sge = 1;
            wr[i][j].opcode = IBV_WR_SEND;
        }
        wr[i][TXSZ-1].next = 0;
        wr[i][TXSZ-1].send_flags = IBV_SEND_SIGNALED;
    }

    for (int i=0; i<TXNB; i++) {
        int err = ibv_post_send(qp, wr[i], 0);
        assert(0 == err && "ibv_post_send() failed");
    }
    for (;;) {
        static struct ibv_wc wc[TXNB];
        int nb = ibv_poll_cq (cq, TXNB, wc);
        assert(nb >= 0 && "ibv_poll_cq() failed");
        for (int i=0; i<nb; i++) {
            int err = ibv_post_send(qp, wr[wc[i].wr_id], 0);
            assert(0 == err && "ibv_post_send() failed");
        }
    }

    return 0;
}
