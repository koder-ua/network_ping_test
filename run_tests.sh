#!/bin/bash
set -x
set -o pipefail

LOCAL=172.16.40.43
REMOTE=172.16.40.37

# 10 50 100 200 500 1000 

# for IP in $LOCAL $REMOTE; do
#     for rounds in 2000; do
#         taskset -c 0 python3.5 main.py -s 64 -r 7 $IP $rounds '*' 2>&1 | tee -a run_results.txt
#     done
# done


large_fd_tests=uvloop_proto,asyncio_proto,cpp_epoll,uvloop_sock,uvloop,asyncio,cpp_poll,cpp_th,selector,gevent,asyncio_sock

for IP in $LOCAL $REMOTE; do
    for rounds in 5000 10000 20000; do
    	echo "# $IP $rounds" >> run_results.txt
        taskset -c 0 stdbuf -oL -eL python3.5 main.py -s 64 -r 7 $IP $rounds $large_fd_tests 2>&1 | tee -a run_results.txt
    done
done

# 5000 10000 20000
