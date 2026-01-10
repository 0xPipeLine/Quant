import numpy as np
import matplotlib.pyplot as plt
from time import time as t
from indicators import Indicators

class Market:
    def __init__(self, n = 120960, dt=1/(365*24*60), S0=180, volatility=0.14, min_drift=-0.11, max_drift=0.11, jump_min=0.005, jump_max=0.01, jump_prob=0.0000006, ema_short = 9, ema_long = 26):
        self.n = n
        self.dt = dt
        self.S0 = S0
        self.volatility = volatility
        self.min_drift = min_drift
        self.max_drift = max_drift
        self.jump_min = jump_min
        self.jump_max = jump_max
        self.jump_prob = jump_prob
        self.mu = np.random.uniform(self.min_drift, self.max_drift)
        self.returns = (self.mu - 0.5 * self.volatility**2) * self.dt + self.volatility * np.sqrt(self.dt) * np.random.randn(self.n)
        self.jumps = np.zeros(self.n)
        self.jump_events = np.random.rand(self.n) < self.jump_prob
        self.jumps[self.jump_events] = np.random.uniform(-self.jump_max, self.jump_max, size=self.jump_events.sum())
        self.total_returns = self.returns + self.jumps
        self.prices = S0 * np.exp(np.cumsum(self.total_returns))
        self.i = Indicators(self.prices)

    def plot(self):
        plt.figure(figsize=(14, 5))
        plt.plot(self.prices)
        plt.title('Simulation de marché avec mouvement brownien géométrique + jumps')
        plt.xlabel('Bougie')
        plt.ylabel('Prix ($)')
        plt.grid(True)
        plt.show()

