#!/bin/bash
redis-server &
sleep 1
redis-benchmark -n 1000 -t get,set,lpush,lpop
/sbin/m5 exit
