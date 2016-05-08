#!/bin/bash
for rounds in 10 50 100 200 500 1000 2000; do
	taskset -c 6 python3.5 main.py -r 7 $1 $rounds '*'
done

# 5000 10000 20000
