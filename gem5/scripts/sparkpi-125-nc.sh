#!/bin/bash
#preparacion del benchmark
cd
echo "export SPARK_HOME=/opt/spark" | tee -a .profile
echo "export PATH=$PATH:$SPARK_HOME/bin:$SPARK_HOME/sbin" | tee -a %.profile
echo "export PYSPARK_PYTHON=/usr/bin/python3" | tee -a .profile
source .profile
cd /opt/spark
/sbin/m5 resetstats
#/sbin/m5 checkpoint
#inicio del benchmark
./bin/run-example SparkPi 125
/sbin/m5 exit
