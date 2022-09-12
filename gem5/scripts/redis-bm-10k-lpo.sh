#!/bin/bash
/sbin/m5 checkpoint
redis-server &
sleep 1.5m
date
/sbin/m5 resetstats
/sbin/m5 checkpoint
redis-benchmark -n 10000 -t lpop
/sbin/m5 exit
