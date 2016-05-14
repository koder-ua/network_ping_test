import sys, time, threading

c = threading.Condition()

def runner_thread():
    with c:
        c.wait()

    while True:
        pass

for i in range(int(sys.argv[1])):
    th = threading.Thread(target=runner_thread)
    th.daemon = True
    th.start()

t1 = time.time()

with c:
    c.notify_all()

while True:
    time.sleep(1)
    t = int(time.time() - t1)
    print(t)
    if t >= 10:
        break
