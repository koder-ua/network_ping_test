# network_ping_test

How to build:

Install:

 * go compiler
 * g++
 * python3.5, python3.5-dev
 * python3.5-gevent
 * uvloop


Compile all:
    
    $ make


How to run:

server:

    $ export GOMAXPROCS=XXX
    $ taskset -c .... ./srv MSG_SEND_TIMEOUT_MS CONN_COMMUNICATION_TIME_MS

client:

    $ taskset -c SOME_CPU_CORE_NUM python3.5 main.py SERVER_IP WORKER_COUNT TESTS_NAMES_OR_*

where test names is coma separated name of test.

To get all available tests execute

    $ python3.5 main.py --list

