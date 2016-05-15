#!/bin/bash
set -x
set -o pipefail

LOCAL=172.16.40.43
REMOTE=172.16.40.37
BIND_IP=172.16.40.43
RESULT_FILE=test_results.txt

large_fd_tests=uvloop_proto,asyncio_proto,cpp_epoll,uvloop_sock,uvloop,asyncio,cpp_poll,cpp_th,selector,gevent,asyncio_sock

# for IP in $REMOTE $LOCAL; do
#     taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 30 -s 64 $IP 10 '*' 2>&1 | tee -a test_results.txt
#     taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 30 -s 64 $IP 100 '*' 2>&1 | tee -a test_results.txt
#     taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 30 -s 64 $IP 1000 '*' 2>&1 | tee -a test_results.txt
#     taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 30 -s 64 $IP 10000 $large_fd_tests 2>&1 | tee -a test_results.txt
# done

taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 30 -s 64 $REMOTE 10 'thread' 2>&1 | tee -a test_results.txt
taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 30 -s 64 $LOCAL 10 'thread' 2>&1 | tee -a test_results.txt
taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 30 -s 64 $REMOTE 10000 $large_fd_tests 2>&1 | tee -a test_results.txt

taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 30 -s 64 $LOCAL 10000 thread 2>&1 | tee -a test_results.txt
taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 30 -s 64 $REMOTE 10000 thread 2>&1 | tee -a test_results.txt

taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 90 -s 64 $LOCAL 30000 '*' 2>&1 | tee -a test_results.txt
taskset -c 0 python3.5 main.py -i $BIND_IP -r 3 --runtime 90 -s 64 $REMOTE 30000 '*' 2>&1 | tee -a test_results.txt
