#!/bin/bash
redis-server --protected-mode no --save "" --port 7000 &
sleep 5
memtier_benchmark --server 127.0.0.1 --port 7000 --threads 4 --clients 15 --data-size 1024 --ratio 1:0 --requests allkeys --key-minimum 1 --key-maximum 200000 --key-pattern P:P --pipeline 1 --show-config --hide-histogram --key-prefix memtier-benchmark-prefix-redistests
/sbin/m5 resetstats
/sbin/m5 checkpoint
memtier_benchmark --server 127.0.0.1 --port 7000 --threads 4 --clients 15 --data-size 1024 --ratio 0:1 --key-minimum 1 --key-maximum 200000 --key-pattern R:R --pipeline 32 --test-time 60 --distinct-client-seed --show-config --hide-histogram --key-prefix memtier-benchmark-prefix-redistests
/sbin/m5 exit
