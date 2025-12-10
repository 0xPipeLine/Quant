import numpy as np
from market import Market

class Strategies:
    def __init__(self, fees_limit = 0.0004, fees_market = 0.0007, min_order = 10):
        self.fees_limit = fees_limit
        self.fees_market = fees_market
        self.min_order = min_order

    def simple_mm_t(self, spread, size, market_t, orders_t):
        pass
