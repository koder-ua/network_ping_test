#!/bin/bash
set -x
set -o pipefail

ROUNDS=7
RUNTIME=30
BIND_IP=172.16.40.43
RESULT_FILE=test_results.txt

THCOUNT=100
ROUNDS=3
for IP in 172.16.40.43 172.16.40.37; do
    for SIZE in 64 256 1024 4096 16384 65536 262144 1048576; do
        taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE -r $ROUNDS $IP $THCOUNT 'asyncio' 2>&1 | tee -a $RESULT_FILE
    done
done

# SIZE=64
# ROUNDS=7

# for IP in 172.16.40.43 172.16.40.37; do
#     for THCOUNT in 10 100 1000 20000; do
#         for i in $(seq 1 $ROUNDS); do
#             taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $IP $THCOUNT '*' 2>&1 | tee -a $RESULT_FILE
#         done
#     done
# done

