#!/bin/bash
redis-server &
sleep 5
/sbin/m5 resetstats
#/sbin/m5 checkpoint
redis-benchmark -n 100000 -t get -d 300
/sbin/m5 exit
