#!/bin/bash
set -x
set -o pipefail

ROUNDS=7
RUNTIME=30
BIND_IP=172.16.40.43
RESULT_FILE=test_results.txt

for IP in 172.16.40.43 172.16.40.37; do
    for THCOUNT in 10 100 1000 10000 20000; do
        for SIZE in 64 8192; do
            taskset -c 0 python3.5 main.py -i $BIND_IP -r $ROUNDS --runtime $RUNTIME -s $SIZE $IP $THCOUNT '*' 2>&1 | tee -a $RESULT_FILE
        done
    done
done
