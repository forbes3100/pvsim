# Proof-of-principle for PVSim multiprocessing-based simulation

import multiprocessing as mp
from multiprocessing import managers
import time

class Signal(object):
    def __init__(self, name, x):
        self.set(name, x)
        self._y = 0

    def name(self):
        return self._name

    def x(self):
        return self._x

    def set(self, name, x):
        self._name = name
        self._x = x

    def __str__(self):
        return f"Signal('{self._name}', {self._x})"

    def __repr__(self):
        return self.__str__()

data = (
    ['a', '2'], ['b', '4'], ['c', '6'], ['d', '8'],
    ['e', '1'], ['f', '3'], ['g', '5'], ['h', '7']
)

def mp_worker(parms):
    (q, proc_name, cnt, want_sigs) = parms
    cnt = int(cnt)
    q.put(f"Process {proc_name}\t{cnt}M loops")
    acc = 1.
    for i in range(cnt*1000000):
        acc += i * 3.456
    q.put(f"Process {proc_name}\tDONE {acc}")
    sigs = [Signal(proc_name, i) for i in range(cnt)]
    return (None, sigs)[want_sigs]

results = []

def worker_done(result):
    global results
    results += result

def mp_handler(n=4):
    global results
    p = mp.Pool(n)
    q = mp.Manager().Queue()
    packets = [(q, name, cnt, i == len(data)-1) \
        for i, (name, cnt) in enumerate(data)]
    p.map_async(mp_worker, packets, callback=worker_done)
    while len(results) < len(data):
        msg = q.get(1)
        if msg:
            print(">", msg)
        time.sleep(0.1)
    p.close()
    p.join()
    print("final result=", results[-1])

if __name__ == '__main__':
    mp_handler()
