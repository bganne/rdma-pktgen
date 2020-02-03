#ifndef RDMA_COMMON_H_
#define RDMA_COMMON_H_

#include <stddef.h>
#include <stdint.h>
#include <infiniband/verbs.h>

#define ALIGNED __attribute__((aligned(64)))

void rdma_create_qp(struct ibv_cq **cq, struct ibv_qp **qp, uint32_t *lkey, const char *name, int cqe, int max_send_wr, int max_recv_wr, void *addr, size_t len, int access);

#endif  /* RDMA_COMMON_H_ */
