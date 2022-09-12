#!/bin/bash
/sbin/m5 checkpoint
/sbin/m5 resetstats
redis-benchmark