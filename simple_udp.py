#!/usr/bin/python
from scapy.all import *

with PcapWriter(sys.stdout) as pcap:
    for i in range(1,255):
        p = (Ether(src='00:11:11:11:11:%x' % i, dst='00:22:22:22:22:%x' % i) /
             IP(src='192.168.1.%i' % i, dst='192.168.2.%i' % i) /
             UDP(sport=1000+i, dport=2000+i))
        pcap.write(p / Raw('\x0a' * (64 - len(p))))
        p = (Ether(src='00:11:11:11:11:%x' % i, dst='00:22:22:22:22:%x' % i) /
             IPv6(src='192:168:1::%i' % i, dst='192:168:2::%i' % i) /
             UDP(sport=1000+i, dport=2000+i))
        pcap.write(p / Raw('\x0a' * (64 - len(p))))
