#!/bin/bash
redis-server &
sleep 5s
/sbin/m5 resetstats
/sbin/m5 checkpoint
redis-benchmark -n 50000 -t get,set,lpush,lpop
/sbin/m5 exit
