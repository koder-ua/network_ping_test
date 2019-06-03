import os
import sys
import ctypes
import socket
import asyncio
import argparse
import traceback
import selectors
import threading

import gevent
from gevent import socket as gevent_socket
import uvloop


import pretty_yaml


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
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    except (OSError, NameError):
        pass


ALL_TESTS = {}


def get_listen_param(count):
    if count < 15:
        return int(count // 5)
    if count < 100:
        return max(count // 10, 3)
    return max(count // 20, 10)


def im_test(func):
    func.test_name = func.__name__.replace('_test', '')
    ALL_TESTS[func.test_name] = func
    return func


@im_test
def selector_test(params, ready_to_connect, before_test, after_test):
    message = ('X' * params.msize).encode('ascii')
    sel = selectors.DefaultSelector()
    sockets = set()
    master_sock = socket.socket()
    master_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    master_sock.bind(params.local_addr)
    master_sock.listen(get_listen_param(params.count))

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
    master_sock.listen(get_listen_param(params.count))

    socks = []
    ready_to_connect()
    for _ in range(params.count):
        sock, _ = master_sock.accept()
        prepare_socket(sock)
        socks.append(sock)

    master_sock.close()

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
                                backlog=get_listen_param(params.count))
    server.append(loop.run_until_complete(coro))
    ready_to_connect()
    loop.run_until_complete(server[0].wait_closed())
    after_test()
    loop.close()


@im_test
def asyncio_proto_test(params, ready_to_connect, before_test, after_test,
                       loop_cls=asyncio.new_event_loop):

    started = False
    finished = False
    e_server = []

    class EchoProtocol(asyncio.Protocol):
        def connection_made(self, transport):
            self.transport = transport

        def connection_lost(self, exc):
            nonlocal finished
            self.transport = None
            if not finished:
                finished = True
                e_server.pop().close()

        def data_received(self, data):
            nonlocal started
            if not started:
                before_test()
                started = True
            if len(data) == params.msize:
                self.transport.write(data)
            elif not data:
                self.transport.close()
            else:
                raise RuntimeError("Partial message")

    loop = loop_cls()
    loop.set_debug(False)
    coro = loop.create_server(EchoProtocol,
                              params.local_addr[0],
                              params.local_addr[1],
                              reuse_address=True,
                              backlog=get_listen_param(params.count))

    server = loop.run_until_complete(coro)
    e_server.append(server)
    ready_to_connect()
    loop.run_until_complete(server.wait_closed())
    after_test()
    loop.close()


@im_test
def uvloop_test(*params):
    return asyncio_test(*params, loop_cls=uvloop.new_event_loop)


@im_test
def uvloop_sock_test(*params):
    return asyncio_sock_test(*params, loop_cls=uvloop.new_event_loop)


@im_test
def uvloop_proto_test(*params):
    return asyncio_proto_test(*params, loop_cls=uvloop.new_event_loop)


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

    listen_queue_sz = get_listen_param(params.count)
    master_sock.listen(listen_queue_sz)

    threads = []
    ready_to_connect()
    inited = False

    for i in range(params.count):
        if i >= params.count - listen_queue_sz - 1 and not inited:
            before_test()
            inited = True

        sock, _ = master_sock.accept()
        prepare_socket(sock, set_no_block=False)
        th = threading.Thread(target=tcp_echo_client,
                              args=(sock,))
        threads.append(th)
        th.daemon = True
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
    master_sock.listen(get_listen_param(params.count))

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


@im_test
def go_test(params, ready_to_connect, before_test, after_test):
    so = ctypes.cdll.LoadLibrary("./bin/libclient.go.so")
    func = getattr(so, "RunTest")
    func.restype = ctypes.c_int
    func.argtypes = [ctypes.POINTER(ctypes.c_char),  # local ip
                     ctypes.c_int,                   # local port
                     ctypes.c_int,                   # params.count
                     ctypes.c_int,                   # msize
                     ctypes.c_int,                   # listen value
                     TIME_CB,
                     TIME_CB,
                     TIME_CB]

    func(params.local_addr[0].encode(),
         params.local_addr[1],
         params.count,
         params.msize,
         get_listen_param(params.count),
         TIME_CB(ready_to_connect),
         TIME_CB(before_test),
         TIME_CB(after_test))


def run_c_test(fname, params, ready_to_connect, before_test, after_test):
    so = ctypes.cdll.LoadLibrary("./bin/libclient.so")
    func = getattr(so, fname)
    func.restype = ctypes.c_int
    func.argtypes = [ctypes.POINTER(ctypes.c_char),  # local ip
                     ctypes.c_int,                   # local port
                     ctypes.c_int,                   # params.count
                     ctypes.c_int,                   # msize
                     ctypes.c_int,                   # listen value
                     TIME_CB,
                     TIME_CB,
                     TIME_CB]

    func(params.local_addr[0].encode(),
         params.local_addr[1],
         params.count,
         params.msize,
         get_listen_param(params.count),
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
        s.send((f"{params.local_addr[0]} {params.local_addr[1]} {params.count} " +
                f"{params.runtime} {params.timeout[0]} {params.timeout[1]} {params.msize}").encode('ascii'))

    def stamp():
        times.append(os.times())

    func(params, ready_func, stamp, stamp)

    utime = times[1].user - times[0].user
    stime = times[1].system - times[0].system
    ctime = times[1].elapsed - times[0].elapsed

    result = s.recv(1024 * 64)
    s.close()

    msg_processed, lat_base, *lat_distribution_and_percentiles_s = result.split()
    lat_distribution_and_percentiles = list(map(int, lat_distribution_and_percentiles_s))

    lats_size = lat_distribution_and_percentiles[0]
    lat_distribution_raw = lat_distribution_and_percentiles[1: 1 + lats_size * 2]
    raw_msg_percentiles = lat_distribution_and_percentiles[1 + lats_size * 2:]

    lat_distribution = dict(zip(lat_distribution_raw[::2], lat_distribution_raw[1::2]))
    perc_size = raw_msg_percentiles[0]
    assert len(raw_msg_percentiles) == perc_size + 1
    percentiles = raw_msg_percentiles[1:]

    return utime, stime, ctime, int(msg_processed), float(lat_base), lat_distribution, percentiles


def print_lat_stats(lats, log_base):
    print("Lats:")
    for pos, i in enumerate(lats):
        if i > 100:
            print(f"    {ns_to_readable(log_base ** pos):<8s}: {i}")


def get_lats(lats, log_base, percs=(0.5, 0.75, 0.95)):

    all_mess = sum(lats)
    if 0 == all_mess:
        return [0] * len(percs)

    curr = 0
    res = [None] * len(percs)
    assert list(sorted(percs)) == list(percs)

    for idx, val in sorted(lats.items()):
        curr += val
        for res_idx, perc in enumerate(percs):
            if curr >= all_mess * perc and res[res_idx] is None:
                res[res_idx] = log_base ** idx

    return res


def ns_to_readable(val):
    for limit, ext in ((1E9, ''), (1E6, 'm'), (1E3, 'u'), (1, 'n')):
        if val >= limit:
            return f"{int(val / limit)}{ext}s"


def main(argv):
    if len(argv) == 2 and argv[1] == '--list':
        print(",".join(sorted(ALL_TESTS.keys())))
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
    parser.add_argument('--max-timeout', type=int, default=None)
    parser.add_argument('--min-timeout', type=int, default=None)

    opts = parser.parse_args(argv[1:])

    params = TestParams()
    params.loader_addr = (opts.loader_ip, opts.loader_port)
    params.local_addr = (opts.bind_ip, opts.bind_port)
    params.msize = opts.msize
    params.count = opts.count
    params.runtime = opts.runtime

    if opts.timeout and (opts.max_timeout or opts.min_timeout):
        print("--runtime option is conflict with --max-timeout/--min-timeout")
        return 1

    if (opts.max_timeout or opts.min_timeout) and not (opts.max_timeout and opts.min_timeout):
        print("--max-timeout requires --min-timeout and vice versa")
        return 1

    if opts.max_timeout and (opts.max_timeout < opts.min_timeout):
        print("--max-timeout should be >= --min-timeout")
        return 1

    if opts.max_timeout:
        params.timeout = (opts.min_timeout, opts.max_timeout)
    elif opts.timeout:
        params.timeout = (opts.timeout, opts.timeout)
    else:
        params.timeout = (0, 0)

    test_names = opts.tests.split(',')

    if test_names == ['*']:
        run_tests = list(ALL_TESTS.values())
    else:
        run_tests = []
        for test_name in test_names:
            if test_name not in ALL_TESTS:
                print(f"Can't found test {test_name!r}.")
                return 1
            run_tests.append(ALL_TESTS[test_name])

    run_tests.sort(key=lambda x: x.__name__)

    results_struct = dict(
        workers=opts.count,
        server=f"{opts.loader_ip}:{opts.loader_port}",
        bind_addr=f"{opts.bind_ip}:{opts.bind_port}",
        msize=opts.msize,
        runtime=opts.runtime,
        timeout=opts.timeout,
        data=[],
    )

    if opts.meta:
        results_struct['meta'] = {}
        for data in opts.meta:
            key, val = data.split('=', 1)
            results_struct['meta'][key] = val

    # templ = "      - {{func: {:<15}, utime: {:>6.2f}, stime: {:>6.2f}, ctime: {:>6.2f}, messages: {:>8d}}}"
    # print("-   workers: {0.count}".format(opts))
    # print("    server: {0.loader_ip}:{0.loader_port}".format(opts))
    # print("    msize: {0.msize}".format(opts))
    # print("    runtime: {0.runtime}".format(opts))
    # print("    timeout: {0.timeout}".format(opts))
    # print("    data:")

    for func in run_tests:
        for i in range(opts.rounds):
            try:
                utime, stime, ctime, msg_processed, lat_base, \
                    lat_distribution, msg_percentiles = get_run_stats(func, params)

                assert len(msg_percentiles) == 19

                lat_50, lat_75, lat_95 = get_lats(lat_distribution, lat_base)

                curr_res = dict(
                    func=func.__name__.replace("_test", ''),
                    utime=f"{utime:.2f}",
                    stime=f"{stime:.2f}",
                    ctime=f"{ctime:.2f}",
                    lat_50=ns_to_readable(lat_50),
                    lat_95=ns_to_readable(lat_95),
                    msg_5perc=msg_percentiles[0],
                    msg_95perc=msg_percentiles[-1],
                    messages=msg_processed)
                results_struct['data'].append(curr_res)
            except Exception as exc:
                traceback.print_exc()
                curr_res = dict(func=func.test_name,
                                err=str(exc))
                results_struct['data'].append(curr_res)
    print(pretty_yaml.dumps([results_struct], width=200))
    return 0


if __name__ == "__main__":
    exit(main(sys.argv))


# python3.5 main.py 172.18.200.44 1000 cpp
# perf stat -e cs python3.5 main.py 172.18.200.44 1000 cpp
# sudo perf stat -e 'syscalls:sys_enter_*'
# strace -f -o /tmp/th_res.txt python3.5 main.py 172.18.200.44 100
