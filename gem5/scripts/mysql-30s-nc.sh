#!/bin/bash
#preparacion del benchmark
cd
mysql -u root -e 'CREATE DATABASE test;'
sysbench oltp_read_write --table-size=25000 --mysql-db=test --mysql-user=root --db-driver=mysql prepare
#inicio del benchmark
/sbin/m5 resetstats
#/sbin/m5 checkpoint
sysbench oltp_read_write --table-size=25000 --db-driver=mysql --mysql-db=test --mysql-user=root --time=30 --max-requests=0 --threads=1 run
/sbin/m5 exit
