CFLAGS:=-g -Wall -Werror -std=gnu99
CFLAGS+=-O2
LDFLAGS:=-g
LDLIBS:=-libverbs

all: rdma-pktgen

rdma-pktgen: rdma-pktgen.c

clean:
	$(RM) rdma-pktgen

.PHONY: all clean
