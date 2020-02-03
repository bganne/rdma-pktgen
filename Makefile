CFLAGS:=-g -Wextra -Werror -std=gnu99
CFLAGS+=-O2
LDFLAGS:=-g
LDLIBS:=-libverbs

all: rdma-tx rdma-rx simple_udp.cap

rdma-tx: tx.o common.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LOADLIBES) $(LDLIBS)

rdma-rx: rx.o common.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LOADLIBES) $(LDLIBS)

simple_udp.cap: simple_udp.py
	python $^ > $@

clean:
	$(RM) *.o rdma-tx rdma-rx simple_udp.cap

.PHONY: all clean
