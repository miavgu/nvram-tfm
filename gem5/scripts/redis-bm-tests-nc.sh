#!/bin/bash
redis-server &
sleep 1s
#/sbin/m5 resetstats
#/sbin/m5 checkpoint
redis-benchmark -n 70000 -t get,set,lpush,lpop -d 3584 -c 200 &
redis-benchmark -n 70000 -t get,set,lpush,lpop -d 3584 -c 200 
/sbin/m5 exit
