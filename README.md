### network_ping_test

#### How to build:

Install:

 * g++
 * python3.5, python3.5-dev
 * python3.5-gevent
 * uvloop
 * golang 1.7+

For ubuntu 14.04:

    $ sudo add-apt-repository ppa:fkrull/deadsnakes
    $ sudo apt-get update
    $ sudo apt-get install python3.5 python3.5-dev g++ make
    $ wget https://bootstrap.pypa.io/get-pip.py
    $ sudo python3.5 get-pip.py
    $ sudo python3.5 -m pip install uvloop gevent

[install golang](https://golang.org/dl/)

Disable CPU throttling:

    $ sudo apt-get install cpufrequtils
    $ sudo cpufreq-set -r -g performance
    $ cpufreq-info     # << CHECK OUTPUT!

Clone code:

    $ git clone https://github.com/koder-ua/network_ping_test.git

Compile all:
    
	$ cd network_ping_test
    $ make

#### How to run:

Server:

    # ulimit -n 65536
    # echo 1024 65535 | tee /proc/sys/net/ipv4/ip_local_port_range
    # taskset -c .... ./bin/server_cpp

Client:

    # ulimit -n 65536
    # echo 1024 65535 | tee /proc/sys/net/ipv4/ip_local_port_range
    # taskset -c SOME_CPU_CORE_NUM python3.5 main.py SERVER_IP WORKER_COUNT TESTS_NAMES_OR_*

On single-node test be carefull to run client and server on different HW cores (not only on
different logical cores, as two logical cores == one hardware in case of HT)

where test names is coma separated name of test.

To get all available tests execute

    $ python3.5 main.py --list


#### Visualize

Visualization code is in plot_tests_results.py, but it doesn't support passing
parameters from CLI - you need to change a code, to get other results.
Also it requires texttable and mathplotlib modules.

