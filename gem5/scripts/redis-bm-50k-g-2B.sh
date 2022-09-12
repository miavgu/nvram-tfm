#!/bin/bash
redis-server &
sleep 5
/sbin/m5 resetstats
#/sbin/m5 checkpoint
redis-benchmark -n 50000 -t get -d 2
/sbin/m5 exit
