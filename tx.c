#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <infiniband/verbs.h>
#include "common.h"

/*
 * we build a pipeline to tx packets:
 *   each pipeline batch is composed of TXSZ packets
 *   pipeline depth is TXNB
 */
#define TXSZ    1024
#define TXNB    2

struct packet {
    uintptr_t addr;
    int len;
};

static const char packet1[] ALIGNED = {
    /* ethernet: dst=00:0c:41:82:b2:53, src=00:d0:59:6c:40:4e, type=0x0800 (IPv4) */
    0x00, 0x0c, 0x41, 0x82, 0xb2, 0x53, 0x00, 0xd0, 0x59, 0x6c, 0x40, 0x4e, 0x08, 0x00,
    /* ipv4: src=192.168.50.50, dst=192.168.0.1, proto=17 (UDP) */
    0x45, 0x00, 0x00, 0x3d, 0x0a, 0x41, 0x00, 0x00, 0x80, 0x11, 0x7c, 0xeb, 0xc0, 0xa8, 0x32, 0x32, 0xc0, 0xa8, 0x00, 0x01,
    /* udp: sport=1026, dport=53 */
    0x04, 0x02, 0x00, 0x35, 0x00, 0x29, 0x01, 0xab,
    /* DNS Standard query 0x002b A us.pool.ntp.org */
    0x00, 0x2b, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x75, 0x73, 0x04, 0x70, 0x6f, 0x6f, 0x6c, 0x03, 0x6e, 0x74, 0x70, 0x03, 0x6f, 0x72, 0x67, 0x00, 0x00, 0x01, 0x00, 0x01
};

static struct packet packet1_ = {
    .addr = (uintptr_t)packet1,
    .len = sizeof(packet1)
};

/* default to packet1 */
static struct packet *packets = &packet1_;
static unsigned packets_nb = 1;
static const void *packets_mr_addr = packet1;
static size_t packets_mr_len = sizeof(packet1);

static void load_pcap(const char *fname)
{
    packets = 0;
    packets_nb = 0;
    packets_mr_addr = 0;
    packets_mr_len = 0;

    int fd = open(fname, O_RDONLY);
    assert(fd >= 0 && "open() failed");

    struct stat stat;
    int err = fstat(fd, &stat);
    assert(0 == err && "fstat() failed");
    assert(stat.st_size >= 24 + 16 + 64); /* we want at least 1 64B packet */

    const struct {
        uint32_t magic;
        uint32_t version;
        uint32_t tz_offset;
        uint32_t ts_acc;
        uint32_t snap_len;
        uint32_t ll_type;
    } *pcap = mmap(0, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(MAP_FAILED != pcap && "mmap() failed");
    assert(0xa1b2c3d4 == pcap->magic);
    assert(0x00040002 == pcap->version);
    assert(0 == pcap->tz_offset);
    assert(0 == pcap->ts_acc);
    assert(pcap->snap_len >= 64);
    assert(1 == pcap->ll_type); /* DLT_EN10MB aka Ethernet */

    const struct {
        uint32_t ts_sec;
        uint32_t ts_usec;
        uint32_t cap_len;
        uint32_t wire_len;
        uint8_t data[];
    } *pkt;
    for (pkt = (void *)(pcap + 1);
            (uintptr_t)pkt + sizeof(*pkt) < (uintptr_t)pcap + stat.st_size;
            pkt = (void *)(pkt->data + pkt->cap_len)) {
        assert((uintptr_t)pkt->data + pkt->cap_len <= (uintptr_t)pcap + stat.st_size);
        packets_nb++;
        if (0 == (packets_nb & (packets_nb - 1)))
            packets = realloc(packets, packets_nb * 2 * sizeof(packets[0]));
        packets[packets_nb-1].addr = (uintptr_t)pkt->data;
        packets[packets_nb-1].len = pkt->cap_len;
    }

    packets_mr_addr = pcap;
    packets_mr_len = stat.st_size;
}

int main(int argc, const char **argv)
{
    switch (argc) {
        case 2:
            __builtin_prefetch(packet1 +  0, 0, 1);
            __builtin_prefetch(packet1 + 64, 0, 1);
            break;
        case 3:
            load_pcap(argv[2]);
            break;
        default:
            fprintf(stderr, "Usage: %s <ib device> [pcap]\n", argv[0]);
            return -1;
    }

    struct ibv_cq *cq;
    struct ibv_qp *qp;
    uint32_t lkey;
    rdma_create_qp(&cq, &qp, &lkey, argv[1], TXNB, TXNB * TXSZ, 0, (void *)packets_mr_addr, packets_mr_len, 0);

    static struct ibv_sge sge[TXNB][TXSZ] ALIGNED;
    static struct ibv_send_wr wr[TXNB][TXSZ] ALIGNED;
    for (int i=0; i<TXNB; i++) {
        for (int j=0; j<TXSZ; j++) {
            int pktid = (i*TXSZ+j) % packets_nb;
            sge[i][j].addr = packets[pktid].addr;
            sge[i][j].length = packets[pktid].len;
            sge[i][j].lkey = lkey;
            wr[i][j].wr_id = i;
            wr[i][j].next = &wr[i][j+1];
            wr[i][j].sg_list = &sge[i][j];
            wr[i][j].num_sge = 1;
            wr[i][j].opcode = IBV_WR_SEND;
        }
        wr[i][TXSZ-1].next = 0;
        wr[i][TXSZ-1].send_flags = IBV_SEND_SIGNALED;
    }

    /* fill the pipeline */
    for (int i=0; i<TXNB; i++) {
        int err = ibv_post_send(qp, wr[i], 0);
        assert(0 == err && "ibv_post_send() failed");
    }

    unsigned long tx = 0;
    unsigned long refresh = 1;
    struct timeval last;
    gettimeofday(&last, 0);
    for (;;) {
        static struct ibv_wc wc[TXNB] ALIGNED;
        int nb = ibv_poll_cq (cq, TXNB, wc);
        assert(nb >= 0 && "ibv_poll_cq() failed");
        for (int i=0; i<nb; i++) {
            int err = ibv_post_send(qp, wr[wc[i].wr_id], 0);
            assert(0 == err && "ibv_post_send() failed");
        }
        tx += nb;
        if (tx >= refresh) {
            struct timeval now;
            gettimeofday(&now, 0);
            timersub(&now, &last, &last);
            double sec = last.tv_sec + last.tv_usec * 1e-6;
            double pps = (tx * TXSZ) / sec;
            printf(" === %g PPS\n", pps);
            refresh = tx  >= sec ? tx / sec : 1;
            tx = 0;
            last = now;
        }
    }

    return 0;
}
