!SLIDE
### План и цели
    * Sync vs Async
    * Существующие async системы
    * asyncio в сравнении с ними
    * Тесты производительности и наглая лож
    * Выводы

!SLIDE
### Sync vs Async
    * Кто переключает контекст?
    * [Concurrency is not parallelism](https://www.youtube.com/watch?v=f6kdp27TYZs)
    
!SLIDE
### Проблемы потоков
    * Гонки
    * Dead and live locks
    * Инверсия приоритетов
    * Переключение контекста в ОС дорого
    * GIL
    * [C10K](http://www.kegel.com/c10k.html)
    * Недетерминизм исполнения

!SLIDE
### Настоящие проблемы потоков
    * Любители сырых примитивов синхронизации
    * Непредсказуемость переключений (+плохие шедулеры)
    * Избыточные переключения контекста
    * Отладка, логирование, etc затруднены
    * GIL (+не честное разделение времени, +не работающие приоритеты)
    * (С10M)[https://www.youtube.com/watch?v=73XNtI0w7jA#!]
    * Нельзя шарить данные (например счетчик)

!SLIDE
### Настоящие проблемы потоков
~~~~{python}
def runner_thread():
    while True:
        pass

for i in range(TH_COUNT):
    threading.Thread(target=runner_thread).start()

t1 = time.time()
while time.time() - t1 < 10:
    time.sleep(1)
    print(time.time() - t1)


>>> 1
9
15
~~~~


!SLIDE
### Ручное переключение потоков
    * Кооперативное - блокирующие, долго исполняющиеся потоки
    * Вытеснящее - нужно писать свой шедулер, недетерминизм

!SLIDE
### Две модели асинхронности
    * Реактор
    * Множество акторов

!SLIDE
### Erlang
    * (Making reliable distributed systems in the presence of software errors)[http://ftp.nsysu.edu.tw/FreeBSD/ports/distfiles/erlang/armstrong_thesis_2003.pdf]
    * 99.999% < Ericsson AXD301 availability < 99.9999999%
    * Функциональный язык
    * Первый язык с массивной параллельностью
    * Процессы обмениваются сообщениями, никаких разделяемых модифицируемых ресурсов
    * Каждый актор это цикл по обработки сообщений
    * Акторы следят друг за другом
    * Вытесняющая многозадачность
    * Несколько потоков ОС

!SLIDE
### Stackless python
    * Erlang для python, но на реакторе
    * +переключение по блокирующим вызовам
    * +python-like sheduler

!SLIDE
### Scala + akka
    * Erlang-like

!SLIDE
### Haskel, C, ...

!SLIDE
### Python
    * Twisted
    * eventlet, gevent
    * cogen
    * Tornado,....

!SLIDE
### AsyncIO
Должен был быть универсальным event loop, для всех библиотек

!SLIDE
### AsyncIO
Но часть его разработчиков сошли с ума и сделали из него бога (с)

!SLIDE
### AsyncIO
    Есть три варианта API для работы с сетью

    * Сырые дескрипторы
    * Потоки
    * Протоколы

!SLIDE
### AsyncIO
~~~~{python}
async def tcp_echo_client(reader, writer):
    while True:
        data = await reader.read(params.msize)
        writer.write(data)
        await writer.drain()  # not nessesary
    writer.close()

loop = loop_cls()

coro = asyncio.start_server(tcp_echo_client, *addr, loop=loop)
server = loop.run_until_complete(coro)
loop.run_until_complete(server.wait_closed())
loop.close()
~~~~

!SLIDE
### AsyncIO raw sock
~~~~{python}
async def client(loop, sock):
    while True:
        data = await loop.sock_recv(sock, params.msize)
        await loop.sock_sendall(sock, data)

master_sock = socket.socket()
# setsockopt+bind+listen
sock, _ = master_sock.accept()

loop = asyncio.new_event_loop()
tasks = [loop.create_task(client(loop, sock))]
loop.run_until_complete(asyncio.gather(*tasks))
~~~~

!SLIDE
### AsyncIO proto
    Look ma I see Twisted!

~~~~{python}
class EchoProtocol(asyncio.Protocol):
    def connection_made(self, transport):
        self.transport = transport

    def connection_lost(self, exc):
        self.transport = None

    def data_received(self, data):
        self.transport.write(data)

loop = asyncio.new_event_loop()
coro = loop.create_server(EchoProtocol, *addr)
server = loop.run_until_complete(coro)
loop.run_until_complete(server.wait_closed())
~~~~

!SLIDE
### Где AsyncIO среди других асинхронных систем
    * Lingua franca для асинхронных систем
    * Чистый event loop
    * Минималистичный синхронно/асинхронный API
    * Попытка покрыть API основные виды блокирующих вызовов (сигналы, пайпы, etc)

!SLIDE
### AsyncIO as evloop
    twisted
    * https://github.com/itamarst/txtulip - NO
    * https://glyph.twistedmatrix.com/2014/05/the-report-of-our-death.html

!SLIDE
### Проблемы AsyncIO
    * Явные точки блокирующих вызовов - прощай, sqlalchemy
    * Необходимость отдельного API для всего, с чем не работает epoll
    * Нужно переписать все
    * "Просто вынеси в отдельный поток" не всегда работает
    * Сложность контроля уровня параллелизма

!SLIDE
### Ложь, Наглая ложь и тесты производительности
    * Постобработка
    * Контроль нагрузки
    * Адекватные результаты
    * Средние + девиация + доверительный интервал для данных
    * Персентили для латентностей

!SLIDE
### Тест номер раз
    python3.5 main_new.py --runtime 20 -s 64 127.0.0.1 200 '*'
              Kmps  stime   utime  
    asyncio    310   5.13   14.90
    selector  1900  12.86    7.10
    thread    1100  46.22   12.59

!SLIDE
### perf для 200 потоков
    > 100% CPU usage
    sudo perf stat python3.5 main_new.py ... threads
      50287.132418 task-clock (msec)
         3,645,408 context-switches 
           965,678 cpu-migrations   
             5,297 page-faults      
    87,957,887,922 cycles           
    45,364,602,467 instructions     
     8,902,403,577 branches         
       128,403,378 branch-misses    

!SLIDE
### perf для 200 потоков
    sudo perf stat -e 'syscalls:sys_enter_*' XXX

    python3.5 main_new.py ... threads
    messages: 38660

        38,861 syscalls:sys_enter_sendto                                   
        39,061 syscalls:sys_enter_recvfrom                                   
    3,087,328 syscalls:sys_enter_futex                                    

!SLIDE
### taskset!
    sudo perf stat taskset -c 6 python3.5 main_new.py ... threads
      20149.498132 task-clock (msec)
         1,992,539 context-switches 
                11 cpu-migrations   
             5,470 page-faults      
    60,754,818,298 cycles           
    47,858,395,058 instructions     
     9,076,082,207 branches         
       109,074,649 branch-misses    

!SLIDE
### taskset!
    taskset -c 6 python3.5 main_new.py ... threads
    messages: 2046010

    2,046,211 syscalls:sys_enter_sendto                                   
    2,046,411 syscalls:sys_enter_recvfrom                                   
      163,851 syscalls:sys_enter_futex                                    

!SLIDE
### perf для 200 asyncio
    python3.5 main_new.py ... asyncio
    messages: 406283
 
    406,485 syscalls:sys_enter_sendto                                   
    406,686 syscalls:sys_enter_recvfrom                                   
      4,395 syscalls:sys_enter_epoll_wait                                   
    406,542 syscalls:sys_enter_mremap                                   
    406,577 syscalls:sys_enter_munmap                                   
    406,670 syscalls:sys_enter_mmap                                     

!SLIDE
### perf для плюсов epoll
    python3.5 main_new.py ... cpp_epoll

    messages: 2921753

    2,921,954 syscalls:sys_enter_recvfrom                                   
       14,801 syscalls:sys_enter_epoll_wait                                   
    2,921,955 syscalls:sys_enter_write                                    

!SLIDE
### Итоги тестов (оч сырые) - 20 потоков
                         Kmps  stime  utime  
    asyncio_proto          170  11.9  18.1
    asyncio_sock           140   8.2  21.6
    asyncio                 62   4.2  25.8
    cpp_epoll              440  28.4   1.6
    cpp_poll               600  27.4   2.6
    cpp_th                 340  29.0   1.0
    gevent                 110   8.3  21.6
    selector               320  17.0  13.0
    thread                 290  23.1   6.3
    uvloop_proto           360  20.3   9.7
    uvloop_sock            150   9.4  20.6
    uvloop                 110   7.1  22.9

!SLIDE
### Итоги тестов (оч сырые) - 20000 потоков

                         Kmps  stime  utime
    asyncio_proto          140  12.9  17.7
    asyncio_sock           120   8.4  22.0
    asyncio                 39   3.7  28.2
    cpp_epoll              320  27.8   1.7
    cpp_poll               350  26.9   3.0
    cpp_th                 220  29.0   0.6
    gevent                  99   9.6  20.9
    selector               290  17.5  12.7
    uvloop_proto           300  20.4   9.1
    uvloop_sock            155   9.4  21.0
    uvloop                  63   6.1  24.7
    thread                 190  22.9   7.7
    

