# network_ping_test

How to build:

Install:

 * go compiler
 * g++
 * python3.5, python3.5-dev
 * python3.5-gevent
 * uvloop

For ubuntu 14.04:

    $ sudo add-apt-repository ppa:fkrull/deadsnakes
    $ sudo apt-get update
    $ sudo apt-get install python3.5 python3.5-dev g++ make
    $ wget https://bootstrap.pypa.io/get-pip.py
    $ sudo python3.5 get-pip.py
    $ sudo python3.5 -m pip install uvloop gevent

    $ curl -O https://storage.googleapis.com/golang/go1.6.linux-amd64.tar.gz
    $ tar -xvf go1.6.linux-amd64.tar.gz
    $ sudo chown -R root.root go
    $ sudo mv go /usr/local
    $ sudo ln -s /usr/local/go/bin/go /usr/local/bin/go

Disable CPU throttling:

    $ sudo apt-get install cpufrequtils
    $ sudo cpufreq-set -r -g performance
    $ cpufreq-info   << check output

Clone code:

    $ git clone https://github.com/koder-ua/network_ping_test.git

Compile all:
    
	$ cd network_ping_test
    $ make

How to run:

Server:

    $ export GOMAXPROCS=XXX
    $ taskset -c .... ./srv MSG_SEND_TIMEOUT_MS CONN_COMMUNICATION_TIME_MS

Client:

    $ taskset -c SOME_CPU_CORE_NUM python3.5 main.py SERVER_IP WORKER_COUNT TESTS_NAMES_OR_*

On single-node test be carefull to run client and server on different HW cores (not only on
different logical cores, as two logical cores == one hardware in case of HT)

where test names is coma separated name of test.

To get all available tests execute

    $ python3.5 main.py --list

