"""Extend MicroPython wrapping ticks into the monotonic clock used by pure logic."""
class MonotonicClock:
    def __init__(self, time_module):
        self.time = time_module
        self.previous = time_module.ticks_ms()
        self.elapsed = 0

    def now_ms(self):
        current = self.time.ticks_ms()
        delta = self.time.ticks_diff(current,self.previous)
        if delta < 0:
            raise RuntimeError('clock sampled beyond ticks_diff half-period')
        self.elapsed += delta
        self.previous = current
        return self.elapsed
