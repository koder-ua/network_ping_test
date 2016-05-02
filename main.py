import os
import sys
import time
import queue
import ctypes
import socket
import asyncio
import argparse
import selectors
import threading

from concurrent.futures import ThreadPoolExecutor, wait

import gevent
from gevent import socket as gevent_socket


MAX_MESSAGE_SIZE = 1024
MESSAGE = 'Hello World!'.encode()


def selector_test(addr, count, before_test, after_test):
    sel = selectors.DefaultSelector()
    sockets = set()

    for i in range(count):
        sock = socket.socket()
        sock.connect(addr)
        sock.setblocking(False)
        sockets.add(sock)
        sel.register(sock, selectors.EVENT_READ, None)

    msg_counter = 0
    before_test()
    while len(sockets) != 0:
        events = sel.select()
        for key, mask in events:
            try:
                data = key.fileobj.recv(MAX_MESSAGE_SIZE)
            except ConnectionResetError:
                done = True
            else:
                if len(data) == 0:
                    done = True
                else:
                    msg_counter += 1
                    key.fileobj.send(MESSAGE)
                    done = False

            if done:
                sockets.remove(key.fileobj)
                sel.unregister(key.fileobj)
    after_test()
    return msg_counter


def asyncio_test(addr, count, before_test, after_test):
    q = []
    async def connect_all(addr, count, loop):
        socks = []
        for i in range(count):
            socks.append(await asyncio.open_connection(*addr, loop=loop))
        return socks

    async def tcp_echo_client(reader, writer):
        counter = 0
        data = " "
        while len(data) != 0:
            try:
                data = await reader.read(MAX_MESSAGE_SIZE)
                if len(data) != 0:
                    counter += 1
                writer.write(MESSAGE)
            except ConnectionResetError:
                break

        writer.close()
        q.append(counter)

    loop = asyncio.new_event_loop()
    connect_task = loop.create_task(connect_all(addr, count, loop))
    loop.run_until_complete(connect_task)

    before_test()
    tasks = []
    for rw in connect_task.result():
        tasks.append(
            loop.create_task(tcp_echo_client(*rw)))
    loop.run_until_complete(asyncio.gather(*tasks))
    after_test()

    loop.close()
    return sum(q)


def thread_test(addr, count, before_test, after_test):
    q = queue.Queue()

    socks = []
    for i in range(count):
        sock = socket.socket()
        sock.connect(addr)
        socks.append(sock)

    def tcp_echo_client(sock, q):
        counter = 0
        data = " "
        while len(data) != 0:
            try:
                data = sock.recv(MAX_MESSAGE_SIZE)
                if len(data) != 0:
                    sock.send(MESSAGE)
                    counter += 1
            except ConnectionResetError:
                break
        sock.close()
        q.put(counter)

    tasks = []
    executor = ThreadPoolExecutor(max_workers=count)

    before_test()
    for sock in socks:
        tasks.append(executor.submit(tcp_echo_client, sock, q))
    wait(tasks)
    after_test()
    executor.shutdown()

    counter = 0
    while not q.empty():
        counter += q.get()
    return counter


def gevent_test(addr, count, before_test, after_test):
    q = []

    socks = []
    for _ in range(count):
        sock = gevent_socket.socket()
        sock.connect(addr)
        socks.append(sock)

    def tcp_echo_client(sock, q):
        counter = 0
        data = " "
        while len(data) != 0:
            try:
                data = sock.recv(MAX_MESSAGE_SIZE)
                if len(data) != 0:
                    sock.send(MESSAGE)
                    counter += 1
            except ConnectionResetError:
                break
        sock.close()
        q.append(counter)
        return sum(q)

    before_test()
    gl = [gevent.spawn(tcp_echo_client, sock, q) for sock in socks]
    gevent.joinall(gl)
    after_test()

    return sum(q)


def cpp_th_test(addr, count, before_test, after_test):
    pass


TIME_CB = ctypes.CFUNCTYPE(None)


def run_c_test(fname, addr, count, before_test, after_test):
    so = ctypes.cdll.LoadLibrary("./libclient.so")
    func = getattr(so, fname)
    func.restype = ctypes.c_int
    func.argtypes = [ctypes.POINTER(ctypes.c_char),
                     ctypes.c_int,
                     ctypes.c_int,
                     ctypes.POINTER(ctypes.c_int),
                     TIME_CB,
                     TIME_CB]
    counter = ctypes.c_int()
    func(addr[0].encode(),
         addr[1],
         count,
         ctypes.byref(counter),
         TIME_CB(before_test),
         TIME_CB(after_test))
    return counter.value


def cpp_poll_test(*params):
    return run_c_test("run_test_poll", *params)


def cpp_epoll_test(*params):
    return run_c_test("run_test_epoll", *params)


def cpp_th_test(*params):
    return run_c_test("run_test_th", *params)


def get_run_stats(func, *params):
    times = []

    def stamp():
        times.append(os.times())

    msg_processed = func(*params, stamp, stamp)

    utime = times[1].user - times[0].user
    stime = times[1].system - times[0].system
    ctime = times[1].elapsed - times[0].elapsed
    return utime, stime, ctime, msg_processed


def main(argv):
    parser = argparse.ArgumentParser()

    parser.add_argument('server_ip')
    parser.add_argument('num_workers', type=int)
    parser.add_argument('tests')
    parser.add_argument('--port', '-p', type=int, default=33331)
    parser.add_argument('--rounds', '-r', type=int, default=1)
    opts = parser.parse_args(argv[1:])

    test_names = opts.tests.split(',')
    all_tests = (thread_test, asyncio_test, selector_test, gevent_test,
                 cpp_poll_test, cpp_epoll_test, cpp_th_test)
    run_tests = []

    for test in all_tests:
        if test.__name__.replace('_test', '') in test_names or opts.tests == '*':
            run_tests.append(test)

    templ = "      - {{func: {:<15}, utime: {:.3f}, stime: {:.3f}, ctime: {:.3f}, messages: {}}}"

    print("-   workers: {}\n    data:".format(opts.num_workers))
    for func in run_tests:
        for i in range(opts.rounds):
            utime, stime, ctime, msg_precessed = get_run_stats(func, (opts.server_ip, opts.port),
                                                               opts.num_workers)
            print(templ.format(func.__name__, utime, stime, ctime, msg_precessed))

if __name__ == "__main__":
    exit(main(sys.argv))


# python3.5 main.py 172.18.200.44 1000 cpp
# perf stat -e cs python3.5 main.py 172.18.200.44 1000 cpp
# sudo perf stat -e 'syscalls:sys_enter_*'
# strace -f -o /tmp/th_res.txt python3.5 main.py 172.18.200.44 100

