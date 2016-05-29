#!/bin/bash
set -x
set -o pipefail

SIZE=64
ROUNDS=55
RUNTIME=30
BIND_IP=172.16.40.43
RESULT_FILE=unified_test_results2.txt

READY_10k=asyncio,asyncio_proto,asyncio_sock,uvloop,uvloop_sock,uvloop_proto,gevent,cpp_epoll,selector,thread,cpp_th
READY_30k=asyncio,asyncio_proto,asyncio_sock,uvloop,uvloop_sock,uvloop_proto,gevent,cpp_epoll,selector,cpp_th
READY_60k=asyncio,asyncio_proto,asyncio_sock,uvloop,uvloop_sock,uvloop_proto,gevent,cpp_epoll,selector

SERVER_IP=172.16.40.37
for i in $(seq 1 $ROUNDS); do
    for THCOUNT in 10 32 100 316 1000 3162 10000; do
        date
        echo "round $i"
        timeout -k 5s 5m taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $SERVER_IP $THCOUNT thread 2>&1 | tee -a $RESULT_FILE
        taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $SERVER_IP $THCOUNT "$READY_30k" 2>&1 | tee -a $RESULT_FILE
    done

    THCOUNT=31623
    date
    echo "round $i"
    taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $SERVER_IP $THCOUNT "$READY_30k" 2>&1 | tee -a $RESULT_FILE

    THCOUNT=60000
    date
    echo "round $i"
    taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $SERVER_IP $THCOUNT "$READY_60k" 2>&1 | tee -a $RESULT_FILE
done
