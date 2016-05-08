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


MESSAGE_SIZE = 1024
MESSAGE = ('X' * MESSAGE_SIZE).encode()


def prepare_socket(sock, set_no_block=True):
    if set_no_block:
        sock.setblocking(False)

    try:
        sock.setsockopt(IPPROTO_TCP, TCP_NODELAY, 1)
    except (OSError, NameError):
        pass


ALL_TESTS = {}


def im_test(func):
    ALL_TESTS[func.__name__.replace('_test', '')] = func
    return func


@im_test
def selector_test(addr, count, before_test, after_test):
    sel = selectors.DefaultSelector()
    sockets = set()

    for _ in range(count):
        sock = socket.socket()
        sock.connect(addr)
        sockets.add(sock)
        prepare_socket(sock)
        sel.register(sock, selectors.EVENT_READ, None)

    msg_counter = 0
    before_test()
    while sockets:
        for key, mask in sel.select():
            try:
                data = key.fileobj.recv(MESSAGE_SIZE)
                if data:
                    if len(data) != MESSAGE_SIZE:
                        raise RuntimeError("Partial message")
                    else:
                        msg_counter += 1
                        key.fileobj.send(MESSAGE)
            except ConnectionResetError:
                data = b""

            if not data:
                sockets.remove(key.fileobj)
                sel.unregister(key.fileobj)
                key.fileobj.close()
    after_test()

    return msg_counter


@im_test
def asyncio_sock_test(addr, count, before_test, after_test,
                      loop_cls=asyncio.new_event_loop):

    counter = 0
    async def client(loop, sock):
        nonlocal counter
        try:
            while True:
                data = await loop.sock_recv(sock, MESSAGE_SIZE)
                if not data:
                    break
                elif len(data) != MESSAGE_SIZE:
                    raise RuntimeError("Partial message")
                counter += 1
                await loop.sock_sendall(sock, MESSAGE)
        except ConnectionResetError:
            pass
        finally:
            sock.close()

    socks = []
    for _ in range(count):
        sock = socket.socket()
        sock.connect(addr)
        prepare_socket(sock)
        socks.append(sock)

    loop = loop_cls()
    loop.set_debug(False)
    tasks = [loop.create_task(client(loop, sock)) for sock in socks]

    before_test()
    loop.run_until_complete(asyncio.gather(*tasks))
    after_test()

    loop.close()
    return counter


@im_test
def asyncio_test(addr, count, before_test, after_test,
                 loop_cls=asyncio.new_event_loop):
    async def connect_all(addr, count, loop):
        conns = []
        for i in range(count):
            r, w = await asyncio.open_connection(*addr, loop=loop)
            conns.append((r, w))
            wsock = w.get_extra_info('socket')
            prepare_socket(wsock, set_no_block=False)
        return conns

    loop = loop_cls()
    loop.set_debug(False)

    connect_task = loop.create_task(connect_all(addr, count, loop))
    loop.run_until_complete(connect_task)

    counter = 0
    async def tcp_echo_client(reader, writer):
        nonlocal counter
        try:
            while True:
                data = await reader.read(MESSAGE_SIZE)
                if not data:
                    break
                elif len(data) != MESSAGE_SIZE:
                    raise RuntimeError("Partial message")
                counter += 1
                writer.write(MESSAGE)
        except ConnectionResetError:
            pass
        finally:
            writer.close()

    tasks = []
    for r, w in connect_task.result():
        tasks.append(
            loop.create_task(tcp_echo_client(r, w)))

    before_test()
    loop.run_until_complete(asyncio.gather(*tasks))
    after_test()

    loop.close()
    return counter


@im_test
def uvloop_test(*params):
    import uvloop
    return asyncio_test(*params, uvloop.new_event_loop)


@im_test
def uvloop_sock_test(*params):
    import uvloop
    return asyncio_sock_test(*params, uvloop.new_event_loop)


@im_test
def thread_test(addr, count, before_test, after_test):
    started_count = 0
    msg_count = 0
    ready = False

    def tcp_echo_client(sock):
        nonlocal msg_count
        nonlocal started_count

        started_count += 1
        while not ready:
            time.sleep(0.1)

        try:
            while True:
                data = sock.recv(MESSAGE_SIZE)
                if not data:
                    break
                elif len(data) != MESSAGE_SIZE:
                    raise RuntimeError("Partial message")
                sock.send(MESSAGE)
                msg_count += 1
        except ConnectionResetError:
            pass
        finally:
            sock.close()

    threads = []
    for i in range(count):
        sock = socket.socket()
        sock.connect(addr)
        prepare_socket(sock, set_no_block=False)
        th = threading.Thread(target=tcp_echo_client,
                              args=(sock,))
        threads.append(th)
        th.daemon = True
        th.start()

    while started_count != count:
        time.sleep(1)

    before_test()
    ready = True

    for th in threads:
        th.join()

    after_test()

    return msg_count


@im_test
def gevent_test(addr, count, before_test, after_test):
    counter = 0

    def tcp_echo_client(sock):
        nonlocal counter
        try:
            while True:
                data = sock.recv(MESSAGE_SIZE)
                if not data:
                    break
                elif len(data) != MESSAGE_SIZE:
                    raise RuntimeError("Partial message")
                sock.send(MESSAGE)
                counter += 1
        except ConnectionResetError:
            pass
        finally:
            sock.close()

    gl = []
    for _ in range(count):
        sock = gevent_socket.socket()
        sock.connect(addr)
        prepare_socket(sock, set_no_block=False)
        gl.append(gevent.spawn(tcp_echo_client, sock))

    before_test()
    gevent.joinall(gl)
    after_test()

    return counter


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


@im_test
def cpp_poll_test(*params):
    return run_c_test("run_test_poll", *params)


@im_test
def cpp_epoll_test(*params):
    return run_c_test("run_test_epoll", *params)


@im_test
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
    if argv[1] == '--list':
        print(",".join(ALL_TESTS.keys()))
        return 0

    parser = argparse.ArgumentParser()

    parser.add_argument('server_ip')
    parser.add_argument('num_workers', type=int)
    parser.add_argument('tests')
    parser.add_argument('--port', '-p', type=int, default=33331)
    parser.add_argument('--rounds', '-r', type=int, default=1)
    opts = parser.parse_args(argv[1:])

    test_names = opts.tests.split(',')

    if test_names == ['*']:
        run_tests = ALL_TESTS.values()
    else:
        run_tests = []
        for test_name in test_names:
            if test_name not in ALL_TESTS:
                print("Can't found test {!r}.".format(test_name))
                return 1
            run_tests.append(ALL_TESTS[test_name])

    templ = "      - {{func: {:<15}, utime: {:.3f}, stime: {:.3f}, ctime: {:.3f}, messages: {}}}"

    print("-   workers: {}\n    data:".format(opts.num_workers))
    for func in run_tests:
        for i in range(opts.rounds):
            utime, stime, ctime, msg_precessed = get_run_stats(func, (opts.server_ip, opts.port),
                                                               opts.num_workers)
            print(templ.format(func.__name__.replace("_test", ''), utime, stime, ctime, msg_precessed))
    return 0


if __name__ == "__main__":
    exit(main(sys.argv))


# python3.5 main.py 172.18.200.44 1000 cpp
# perf stat -e cs python3.5 main.py 172.18.200.44 1000 cpp
# sudo perf stat -e 'syscalls:sys_enter_*'
# strace -f -o /tmp/th_res.txt python3.5 main.py 172.18.200.44 100

