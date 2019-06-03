#!/usr/bin/env bash
set -o pipefail

SIZE=64
ROUNDS=1
RUNTIME=60
BIND_IP=192.168.0.107
RESULT_FILE=10k_60k.yaml

# READY_10k=asyncio,asyncio_proto,asyncio_sock,uvloop,uvloop_sock,uvloop_proto,gevent,cpp_epoll,selector,thread,cpp_th
# READY_30k=asyncio,asyncio_proto,asyncio_sock,uvloop,uvloop_sock,uvloop_proto,gevent,cpp_epoll,selector,cpp_th
# READY_60k=asyncio,asyncio_proto,asyncio_sock,uvloop,uvloop_sock,uvloop_proto,gevent,cpp_epoll,selector
# FUNCS=selector,cpp_epoll,uvloop_proto,go
FUNCS=selector,asyncio
# THCOUNTS="15000 20000 25000 30000 35000 40000 45000 50000 55000"
THCOUNTS="1000"

SERVER_IP=192.168.0.107
for i in $(seq 1 $ROUNDS); do
    for THCOUNT in $THCOUNTS; do
        date
        echo "round $i"
        taskset -c 0 python main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $SERVER_IP $THCOUNT $FUNCS 2>&1 | tee -a $RESULT_FILE
    done

    # for THCOUNT in 10 32 100 316 1000 3162 10000; do
    #     date
    #     echo "round $i"
    #     timeout -k 5s 5m taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $SERVER_IP $THCOUNT thread 2>&1 | tee -a $RESULT_FILE
    #     taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $SERVER_IP $THCOUNT "$READY_30k" 2>&1 | tee -a $RESULT_FILE
    # done

    # THCOUNT=31623
    # date
    # echo "round $i"
    # taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $SERVER_IP $THCOUNT "$READY_30k" 2>&1 | tee -a $RESULT_FILE

    # THCOUNT=60000
    # date
    # echo "round $i"
    # taskset -c 0 python3.5 main.py -i $BIND_IP --runtime $RUNTIME -s $SIZE $SERVER_IP $THCOUNT "$READY_60k" 2>&1 | tee -a $RESULT_FILE
done
