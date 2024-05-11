# Proof-of-principle for PVSim multiprocessing-based simulation

import multiprocessing as mp
from multiprocessing import managers
import time

class Signal(object):
    def __init__(self, name, x):
        """Initialize a new Signal with a name and a value."""
        self.set(name, x)
        self._y = 0  # Unused in current context, potential for future use.

    def name(self):
        """Return the name of the signal."""
        return self._name

    def x(self):
        """Return the current value of the signal."""
        return self._x

    def set(self, name, x):
        """Set the signal's name and value."""
        self._name = name
        self._x = x

    def __str__(self):
        """String representation of the Signal."""
        return f"Signal('{self._name}', {self._x})"

    def __repr__(self):
        """Formal string representation, used for debugging."""
        return self.__str__()

# List of data items used to simulate input to the processes
data = (
    ['a', '2'], ['b', '4'], ['c', '6'], ['d', '8'],
    ['e', '1'], ['f', '3'], ['g', '5'], ['h', '7']
)

def mp_worker(parms):
    """
    Worker function for processing each item in parallel.
    Processes a set number of loops and optionally returns signal objects.
    """
    (q, proc_name, cnt, want_sigs) = parms
    cnt = int(cnt)
    q.put(f"Process {proc_name}\t{cnt}M loops")
    acc = 1.0
    for i in range(cnt*1000000):
        acc += i * 3.456  # Simulated computation
    q.put(f"Process {proc_name}\tDONE {acc}")
    sigs = [Signal(proc_name, i) for i in range(cnt)] if want_sigs else None
    return sigs

results = []  # Global list to store results from workers

def worker_done(result):
    """Callback function that aggregates results from all worker processes."""
    global results
    results += result

def mp_handler(n=4):
    """Set up and manage a pool of worker processes."""
    global results
    p = mp.Pool(n)
    q = mp.Manager().Queue()
    packets = [(q, name, cnt, i == len(data)-1) for i, (name, cnt) in enumerate(data)]
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
    mp_handler()  # Start the multiprocessing handler
