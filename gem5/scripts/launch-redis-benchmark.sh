#!/bin/bash
/sbin/m5 checkpoint
redis-server &
sleep 1.5m
/sbin/m5 resetstats
/sbin/m5 checkpoint
redis-benchmark
/sbin/m5 exit
