#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/time.h>
#include <infiniband/verbs.h>
#include "common.h"

/* how many bytes of a packet we capture */
#define PKTSZ   9216
/*
 * we build a pipeline to rx packets:
 *   each pipeline batch is composed of RXSZ packets
 *   pipeline depth is RXNB
 */
#define RXSZ    1024
#define RXNB    2

int main(int argc, const char **argv)
{
    if (argc != 2) {
            fprintf(stderr, "Usage: %s <ib device>\n", argv[0]);
            return -1;
    }

    struct ibv_cq *cq;
    struct ibv_qp *qp;
    uint32_t lkey;
    static char packets[RXNB][RXSZ][PKTSZ];
    rdma_create_qp(&cq, &qp, &lkey, argv[1], RXNB * RXSZ, 0, RXNB * RXSZ, packets, sizeof(packets), IBV_ACCESS_LOCAL_WRITE);

    static struct ibv_sge sge[RXNB][RXSZ] ALIGNED;
    static struct ibv_recv_wr wr[RXNB][RXSZ] ALIGNED;
    for (int i=0; i<RXNB; i++) {
        for (int j=0; j<RXSZ; j++) {
            sge[i][j].addr = (uintptr_t)packets[i][j];
            sge[i][j].length = PKTSZ;
            sge[i][j].lkey = lkey;
            wr[i][j].wr_id = i;
            wr[i][j].next = &wr[i][j+1];
            wr[i][j].sg_list = &sge[i][j];
            wr[i][j].num_sge = 1;
        }
        wr[i][RXSZ-1].next = 0;
    }

    /* fill the pipeline */
    for (int i=0; i<RXNB; i++) {
        int err = ibv_post_recv(qp, wr[i], 0);
        assert(0 == err && "ibv_post_send() failed");
    }

    /* initialize sniffer rules to get copy of all traffic */
    struct ibv_flow_attr fa = {0};
    fa.type = IBV_FLOW_ATTR_SNIFFER;
    fa.size = sizeof(fa);
    fa.port = 1;
    struct ibv_flow *flow = ibv_create_flow(qp, &fa);
    assert(flow && "ibv_create_flow() failed");

    uint32_t last_id = 0;
    unsigned long rx = 0;
    unsigned long refresh = 1;
    struct timeval last;
    gettimeofday(&last, 0);
    for (;;) {
        static struct ibv_wc wc[RXNB * RXSZ] ALIGNED;
        int nb;
        while (0 == (nb = ibv_poll_cq(cq, RXNB * RXSZ, wc)));
        assert(nb > 0 && "ibv_poll_cq() failed");
        if (last_id != wc[nb-1].wr_id) {
            /* we have completed a pipeline stage, reschedule it */
            int err = ibv_post_recv(qp, wr[last_id], 0);
            assert(0 == err && "ibv_post_send() failed");
            last_id = wc[nb-1].wr_id;
        }
        rx += nb;
        if (rx >= refresh) {
            struct timeval now;
            gettimeofday(&now, 0);
            timersub(&now, &last, &last);
            double sec = last.tv_sec + last.tv_usec * 1e-6;
            double pps = rx / sec;
            printf(" === %g PPS\n", pps);
            rx = 0;
            refresh = pps >= 2 ? pps : 1;
            last = now;
        }
    }

    return 0;
}
