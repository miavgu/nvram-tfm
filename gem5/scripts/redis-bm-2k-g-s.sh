#!/bin/bash
/sbin/m5 checkpoint
redis-server &
sleep 1.5m
date
/sbin/m5 resetstats
/sbin/m5 checkpoint
redis-benchmark -n 2000 -t get,set
/sbin/m5 exit
