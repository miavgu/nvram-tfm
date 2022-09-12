#!/bin/bash
#preparacion del benchmark
sysbench fileio --file-total-size=15G --file-test-mode=rndrw --time=300 --max-requests=0 prepare
#inicio del benchmark
/sbin/m5 resetstats
sysbench fileio --file-total-size=15G --file-test-mode=rndrw --time=300 --max-requests=0 run
sysbench fileio --file-total-size=15G --file-test-mode=rndrw --time=300 --max-requests=0 cleanup
sysbench --test=memory --memory-oper=read --memory-block-size=64 --memory-total-size=250G --memory-access-mode=rnd run
sysbench --test=memory --memory-oper=write --memory-block-size=64 --memory-total-size=250G --memory-access-mode=rnd run
/sbin/m5 exit
