#!/bin/bash
for rounds in 50 100 200 300 400; do
	taskset -c 6 python3.5 main.py -r 7 $1 $rounds '*'
done

# for rounds in 500 600 700 800 900 1000 2000 5000 10000 20000; do
# 	taskset -c 6 python3.5 main.py -r 7 $1 $rounds '*'
# done
