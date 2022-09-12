#!/bin/bash
redis-server &
sleep 1
/sbin/m5 resetstats
/sbin/m5 checkpoint
redis-benchmark -n 10000 -t get,set,lpush,lpop
/sbin/m5 exit
