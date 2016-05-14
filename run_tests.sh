#!/bin/bash
set -x
set -o pipefail

LOCAL=172.16.40.43
REMOTE=172.16.40.37
BIND_IP=172.16.40.43

large_fd_tests=uvloop_proto,asyncio_proto,cpp_epoll,uvloop_sock,uvloop,asyncio,cpp_poll,cpp_th,selector,gevent,asyncio_sock

for IP in $REMOTE; do
    taskset -c 6 python3.5 main_new.py -i $BIND_IP -r 3 --runtime 30 -s 64 $IP 20 '*' | tee -a test_results3.txt
    taskset -c 6 python3.5 main_new.py -i $BIND_IP -r 3 --runtime 30 -s 64 $IP 200 '*' | tee -a test_results3.txt
    taskset -c 6 python3.5 main_new.py -i $BIND_IP -r 3 --runtime 30 -s 64 $IP 2000 '*' | tee -a test_results3.txt
    taskset -c 6 python3.5 main_new.py -i $BIND_IP -r 3 --runtime 30 -s 64 $IP 20000 $large_fd_tests | tee -a test_results3.txt
done

taskset -c 6 python3.5 main_new.py -i $BIND_IP -r 3 --runtime 30 -s 64 $LOCAL 20000 thread | tee -a test_results3.txt
taskset -c 6 python3.5 main_new.py -i $BIND_IP -r 3 --runtime 30 -s 64 $REMOTE 20000 thread | tee -a test_results3.txt


