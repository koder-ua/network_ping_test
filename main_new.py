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
import uvloop


from pretty_yaml import 


class TestParams:
    def __init__(self):
        self.loader_addr = None
        self.count = None
        self.msize = None
        self.runtime = None
        self.timeout = None
        self.local_addr = None


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
def selector_test(params, ready_to_connect, before_test, after_test):
    message = ('X' * params.msize).encode('ascii')
    sel = selectors.DefaultSelector()
    sockets = set()
    master_sock = socket.socket()
    master_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    master_sock.bind(params.local_addr)
    master_sock.listen(100)

    ready_to_connect()

    while len(sockets) != params.count:
        sock, _ = master_sock.accept()
        prepare_socket(sock)
        sockets.add(sock)
        sel.register(sock, selectors.EVENT_READ, None)

    master_sock.close()

    before_test()
    while sockets:
        for key, mask in sel.select():
            try:
                data = key.fileobj.recv(params.msize)
                if data:
                    if len(data) != params.msize:
                        raise RuntimeError("Partial message")
                    else:
                        key.fileobj.send(message)
            except ConnectionResetError:
                data = b""

            if not data:
                sockets.remove(key.fileobj)
                sel.unregister(key.fileobj)
                key.fileobj.close()

    after_test()


@im_test
def asyncio_sock_test(params, ready_to_connect, before_test, after_test,
                      loop_cls=asyncio.new_event_loop):

    async def client(loop, sock):
        try:
            while True:
                data = await loop.sock_recv(sock, params.msize)
                if not data:
                    break
                elif len(data) != params.msize:
                    raise RuntimeError("Partial message")
                await loop.sock_sendall(sock, data)
        except ConnectionResetError:
            pass
        finally:
            sock.close()

    master_sock = socket.socket()
    master_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    master_sock.bind(params.local_addr)
    master_sock.listen(100)

    socks = []
    ready_to_connect()
    for _ in range(params.count):
        sock, _ = master_sock.accept()
        prepare_socket(sock, set_no_block=False)
        socks.append(sock)

    loop = loop_cls()
    loop.set_debug(False)
    tasks = [loop.create_task(client(loop, sock)) for sock in socks]

    before_test()
    loop.run_until_complete(asyncio.gather(*tasks))
    after_test()

    loop.close()


@im_test
def asyncio_test(params, ready_to_connect, before_test, after_test,
                 loop_cls=asyncio.new_event_loop):
    started = False
    finished = False
    server = []

    async def tcp_echo_client(reader, writer):
        nonlocal started
        nonlocal finished
        try:
            data = await reader.read(params.msize)
            if not started:
                started = True
                before_test()

            while True:
                if not data:
                    break
                elif len(data) != params.msize:
                    raise RuntimeError("Partial message")
                writer.write(data)
                data = await reader.read(params.msize)
        except ConnectionResetError:
            pass
        finally:
            writer.close()
            if not finished:
                finished = True
                server[0].close()

    loop = loop_cls()
    loop.set_debug(False)

    coro = asyncio.start_server(tcp_echo_client,
                                params.local_addr[0],
                                params.local_addr[1],
                                loop=loop,
                                reuse_address=True,
                                reuse_port=True)
    server.append(loop.run_until_complete(coro))
    ready_to_connect()
    loop.run_until_complete(server[0].wait_closed())
    after_test()
    loop.close()


@im_test
def asyncio_proto_test(params, ready_to_connect, before_test, after_test,
                       loop_cls=asyncio.new_event_loop):

    class EchoProtocol(asyncio.Protocol):
        started = False
        finished = False
        server = None

        def connection_made(self, transport):
            self.transport = transport

        def connection_lost(self, exc):
            self.transport = None
            if not self.finished:
                self.finished = True
                self.server.close()
                self.server = None

        def data_received(self, data):
            if len(data) != params.msize:
                self.transport.close()
            else:
                self.transport.write(data)
                if not self.started:
                    before_test()
                    self.started = True

    loop = loop_cls()
    loop.set_debug(False)
    coro = loop.create_server(EchoProtocol,
                              params.local_addr[0],
                              params.local_addr[1],
                              reuse_address=True,
                              reuse_port=True)

    server = loop.run_until_complete(coro)
    EchoProtocol.server = server
    ready_to_connect()
    loop.run_until_complete(server.wait_closed())
    after_test()
    loop.close()


@im_test
def uvloop_test(*params):
    return asyncio_test(*params, uvloop.new_event_loop)


@im_test
def uvloop_sock_test(*params):
    return asyncio_sock_test(*params, uvloop.new_event_loop)


@im_test
def uvloop_proto_test(*params):
    return asyncio_proto_test(*params, uvloop.new_event_loop)


@im_test
def thread_test(params, ready_to_connect, before_test, after_test):
    def tcp_echo_client(sock):
        try:
            while True:
                data = sock.recv(params.msize)
                if not data:
                    break
                elif len(data) != params.msize:
                    raise RuntimeError("Partial message")
                sock.send(data)
        except ConnectionResetError:
            pass
        finally:
            sock.close()

    master_sock = socket.socket()
    master_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    master_sock.bind(params.local_addr)
    master_sock.listen(100)

    threads = []
    ready_to_connect()
    for i in range(params.count):
        sock, _ = master_sock.accept()
        prepare_socket(sock, set_no_block=False)
        th = threading.Thread(target=tcp_echo_client,
                              args=(sock,))
        threads.append(th)
        th.daemon = True
        if i == params.count - 1:
            before_test()
        th.start()

    master_sock.close()

    for th in threads:
        th.join()
    after_test()


@im_test
def gevent_test(params, ready_to_connect, before_test, after_test):

    def tcp_echo_client(sock):
        try:
            while True:
                data = sock.recv(params.msize)
                if not data:
                    break
                elif len(data) != params.msize:
                    raise RuntimeError("Partial message")
                sock.send(data)
        except ConnectionResetError:
            pass
        finally:
            sock.close()

    master_sock = gevent_socket.socket()
    master_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    master_sock.bind(params.local_addr)
    master_sock.listen(100)

    ready_to_connect()
    gl = []

    while len(gl) != params.count:
        sock, _ = master_sock.accept()
        prepare_socket(sock, set_no_block=False)
        gl.append(gevent.spawn(tcp_echo_client, sock))

    master_sock.close()

    before_test()
    gevent.joinall(gl)
    after_test()


TIME_CB = ctypes.CFUNCTYPE(None)


def run_c_test(fname, params, ready_to_connect, before_test, after_test):
    so = ctypes.cdll.LoadLibrary("./bin/libclient2.so")
    func = getattr(so, fname)
    func.restype = ctypes.c_int
    func.argtypes = [ctypes.POINTER(ctypes.c_char),  # local ip
                     ctypes.c_int,                   # local port
                     ctypes.c_int,                   # params.count
                     ctypes.c_int,                   # msize
                     TIME_CB,
                     TIME_CB,
                     TIME_CB]

    func(params.local_addr[0].encode(),
         params.local_addr[1],
         params.count,
         params.msize,
         TIME_CB(ready_to_connect),
         TIME_CB(before_test),
         TIME_CB(after_test))


@im_test
def cpp_poll_test(*params):
    return run_c_test("run_test_poll", *params)


@im_test
def cpp_epoll_test(*params):
    return run_c_test("run_test_epoll", *params)


@im_test
def cpp_th_test(*params):
    return run_c_test("run_test_th", *params)


def get_run_stats(func, params):
    times = []
    s = socket.socket()
    s.connect(params.loader_addr)

    def ready_func():
        s.send(("{0.local_addr[0]} {0.local_addr[1]} {0.count} " +
                "{0.runtime} {0.timeout} {0.msize}").format(params).encode('ascii'))

    def stamp():
        times.append(os.times())

    func(params, ready_func, stamp, stamp)

    utime = times[1].user - times[0].user
    stime = times[1].system - times[0].system
    ctime = times[1].elapsed - times[0].elapsed

    result = s.recv(1024)
    s.close()

    results = list(map(int, result.split()))
    msg_processed = results[0]
    lat_distribution = results[1:]
    return utime, stime, ctime, msg_processed, lat_distribution


def main(argv):
    if len(argv) == 2 and argv[1] == '--list':
        print(",".join(ALL_TESTS.keys()))
        return 0

    parser = argparse.ArgumentParser()

    parser.add_argument('loader_ip')
    parser.add_argument('count', type=int)
    parser.add_argument('tests')
    parser.add_argument('--loader-port', '-p', type=int, default=33331)
    parser.add_argument('--bind-port', '-b', type=int, default=33332)
    parser.add_argument('--bind-ip', '-i', default='0.0.0.0')
    parser.add_argument('--rounds', '-r', type=int, default=1)
    parser.add_argument('--msize', '-s', type=int, default=1024)
    parser.add_argument('--meta', '-m', type=str, nargs='*', default=[])
    parser.add_argument('--runtime', type=int, default=30)
    parser.add_argument('--timeout', '-t', type=int, default=0)

    opts = parser.parse_args(argv[1:])

    params = TestParams()
    params.loader_addr = (opts.loader_ip, opts.loader_port)
    params.local_addr = (opts.bind_ip, opts.bind_port)
    params.msize = opts.msize
    params.count = opts.count
    params.runtime = opts.runtime
    params.timeout = opts.timeout

    test_names = opts.tests.split(',')

    if test_names == ['*']:
        run_tests = list(ALL_TESTS.values())
    else:
        run_tests = []
        for test_name in test_names:
            if test_name not in ALL_TESTS:
                print("Can't found test {!r}.".format(test_name))
                return 1
            run_tests.append(ALL_TESTS[test_name])

    run_tests.sort(key=lambda x: x.__name__)

    results_struct = dict(
        workers=opts.count,
        server="{0.loader_ip}:{0.loader_port}".format(opts),
        msize=opts.msize,
        runtime=opts.runtime,
        timeout=opts.timeout,
        data=[]
    )

    # templ = "      - {{func: {:<15}, utime: {:>6.2f}, stime: {:>6.2f}, ctime: {:>6.2f}, messages: {:>8d}}}"
    # print("-   workers: {0.count}".format(opts))
    # print("    server: {0.loader_ip}:{0.loader_port}".format(opts))
    # print("    msize: {0.msize}".format(opts))
    # print("    runtime: {0.runtime}".format(opts))
    # print("    timeout: {0.timeout}".format(opts))
    # for data in opts.meta:
    #     print("    {}: {}".format(*data.split('=', 1)))
    # print("    data:")

    for func in run_tests:
        for i in range(opts.rounds):
            utime, stime, ctime, msg_precessed, lat_distribution = get_run_stats(func, params)
            # print(templ.format(func.__name__.replace("_test", ''), utime, stime, ctime, msg_precessed))
            curr_res = dict(
                func=func.__name__.replace("_test", ''),
                utime=utime,
                stime=stime,
                ctime=ctime,
                messages=msg_precessed,
                meta=meta)
            results_struct['data'].append(curr_res)
    print(yaml.dumps(results_struct))
    return 0


if __name__ == "__main__":
    exit(main(sys.argv))


# python3.5 main.py 172.18.200.44 1000 cpp
# perf stat -e cs python3.5 main.py 172.18.200.44 1000 cpp
# sudo perf stat -e 'syscalls:sys_enter_*'
# strace -f -o /tmp/th_res.txt python3.5 main.py 172.18.200.44 100