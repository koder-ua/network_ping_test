#!/bin/bash
set -x
set -o pipefail

SIZE=64
ROUNDS=15
RUNTIME=30
BIND_IP=172.16.40.43
RESULT_FILE=unified_test_results.txt

READY_10k=asyncio,asyncio_proto,asyncio_sock,uvloop,uvloop_sock,uvloop_proto,gevent,cpp_epoll,selector,thread,cpp_th
READY_60k=asyncio,asyncio_proto,asyncio_sock,uvloop,uvloop_sock,uvloop_proto,gevent,cpp_epoll,selector

IP=172.16.40.37
for THCOUNT in 10 100 1000 10000; do
    for i in $(seq 1 $ROUNDS); do
        date
        taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $IP $THCOUNT "$READY_10k" 2>&1 | tee -a $RESULT_FILE
    done
done

THCOUNT=60000
for i in $(seq 1 $ROUNDS); do
    date
    taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $IP $THCOUNT "$READY_60k" 2>&1 | tee -a $RESULT_FILE
done
