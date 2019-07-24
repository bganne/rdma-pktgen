CFLAGS:=-g -Wall -Werror -std=gnu99
CFLAGS+=-O2
LDFLAGS:=-g
LDLIBS:=-libverbs

all: rdma-pktgen simple_udp.cap

rdma-pktgen: rdma-pktgen.c

simple_udp.cap: simple_udp.py
	python $^ > $@

clean:
	$(RM) rdma-pktgen simple_udp.cap

.PHONY: all clean
