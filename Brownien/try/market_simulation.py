import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from time import time as t

class Market:
    def __init__(self, S0=180, dt=1/(365*24*60), volatility=0.14,
                 min_drift=-0.11, max_drift=0.11, jump_min=0.005, jump_max=0.01, jump_prob=0.0000006,
                 sma_short_period=9, sma_long_period=35,
                 ema_short_period=9, ema_long_period=35,
                 rsi_period=14):

        self.dt = dt
        self.volatility = volatility
        self.mu = np.random.uniform(min_drift, max_drift)
        self.jump_min = jump_min
        self.jump_max = jump_max
        self.jump_prob = jump_prob

        self.price = S0
        self.t = 0
        self.prices = []

        self.sma_short_period = sma_short_period
        self.sma_long_period = sma_long_period
        self.ema_short_period = ema_short_period
        self.ema_long_period = ema_long_period
        self.rsi_period = rsi_period

        self.drift = (self.mu - 0.5 * self.volatility**2) * self.dt

        self.sma_short_queue = []
        self.sma_long_queue = []

        self.ema_short = None
        self.ema_long = None

        self.prev_price = S0
        self.avg_gain = None
        self.avg_loss = None
        self.rsi = None

        self.history = []

    def step(self):
        shock = self.volatility * np.sqrt(self.dt) * np.random.randn()
        jump = np.random.uniform(-self.jump_max, self.jump_max) if np.random.rand() < self.jump_prob else 0
        ret = self.drift + shock + jump

        self.price *= np.exp(ret)
        self.prices.append(self.price)
        self.t += 1

        # SMA
        self.sma_short_queue.append(self.price)
        if len(self.sma_short_queue) > self.sma_short_period:
            self.sma_short_queue.pop(0)
        sma_short = np.mean(self.sma_short_queue) if len(self.sma_short_queue) == self.sma_short_period else None

        self.sma_long_queue.append(self.price)
        if len(self.sma_long_queue) > self.sma_long_period:
            self.sma_long_queue.pop(0)
        sma_long = np.mean(self.sma_long_queue) if len(self.sma_long_queue) == self.sma_long_period else None

        # EMA
        alpha_short = 2 / (self.ema_short_period + 1)
        if self.ema_short is None and len(self.prices) >= self.ema_short_period:
            self.ema_short = np.mean(self.prices[-self.ema_short_period:])
        elif self.ema_short is not None:
            self.ema_short = alpha_short * self.price + (1 - alpha_short) * self.ema_short

        alpha_long = 2 / (self.ema_long_period + 1)
        if self.ema_long is None and len(self.prices) >= self.ema_long_period:
            self.ema_long = np.mean(self.prices[-self.ema_long_period:])
        elif self.ema_long is not None:
            self.ema_long = alpha_long * self.price + (1 - alpha_long) * self.ema_long

        # RSI
        delta = self.price - self.prev_price
        gain = max(delta, 0)
        loss = max(-delta, 0)

        if self.avg_gain is None and len(self.prices) > self.rsi_period:
            gains = [max(self.prices[i+1] - self.prices[i], 0) for i in range(-self.rsi_period-1, -1)]
            losses = [max(self.prices[i] - self.prices[i+1], 0) for i in range(-self.rsi_period-1, -1)]
            self.avg_gain = np.mean(gains)
            self.avg_loss = np.mean(losses)
        elif self.avg_gain is not None:
            self.avg_gain = (self.avg_gain * (self.rsi_period - 1) + gain) / self.rsi_period
            self.avg_loss = (self.avg_loss * (self.rsi_period - 1) + loss) / self.rsi_period

        if self.avg_gain is not None and self.avg_loss is not None:
            rs = self.avg_gain / (self.avg_loss + 1e-10)
            self.rsi = 100 - (100 / (1 + rs))

        self.prev_price = self.price

        data = {
            'price': self.price,
            'sma_short': sma_short,
            'sma_long': sma_long,
            'ema_short': self.ema_short,
            'ema_long': self.ema_long,
            'rsi': self.rsi
        }

        self.history.append(data)
        return data

    def run(self, steps):
        for _ in range(steps):
            self.step()

    def to_df(self):
        return pd.DataFrame(self.history)

if __name__ == '__main__':
    m = Market()
    t0 = t()
    m.run(100000)
    print(t()-t0)
    df = m.to_df()