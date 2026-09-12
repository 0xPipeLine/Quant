from typing import Iterable

def remaining(l1: list[float], l2: list[float]) -> list[float]: # both l1 and l2 in ascending order
    res = []
    j = 0
    for x in l1:
        while j < len(l2) and l2[j] < x:
            res.append(l2[j])
            j += 1
        if j < len(l2) and (not res or abs(l2[j] - x) <= abs(x - res[-1])):
            j += 1
        elif res:
            res.pop()
    return res + l2[j:]

class Grid:
    def __init__(self, levels: int, spread: float, offset: float, alpha: float):
        self.running = False
        self.levels = levels
        self.spread = spread
        self.offset = offset
        self.alpha = alpha
        self.bids = []
        self.asks = []
        if self.levels <= 1:
            self.geo = 1.0
        elif self.alpha == 1:
            self.geo = self.levels - 1
        else:
            self.geo = (self.alpha ** (self.levels - 1) - 1) / (self.alpha - 1)

    def update(self, ema: float):
        raw = self.spread * ema / self.geo
        offset = self.offset * raw
        bids = []
        px = ema - offset
        for i in range(self.levels):
            bids.append(px)
            px -= raw * (self.alpha ** i)
        self.bids = list(reversed(bids))
        asks = []
        px = ema + offset
        for i in range(self.levels):
            asks.append(px)
            px += raw * (self.alpha ** i)
        self.asks = asks

class EnveloppeInvExp: # one-sided (inv) grid with exponential spacing (ALPHA) and exponential sizes derived from SKEW
    def __init__(self):
        # ==== CONSTANTS ====
        self.MAKER_FEES = 0.00003 # 0.003%
        self.TAKER_FEES = 0.00009 # 0.009%
        self.SPREAD = 0.02        # 4.5% | minimum distance between the first and the last placed orders
        self.OFFSET = 0.0001      # 0.01% spread from ema for first
        self.ALPHA = 3.6          # exponential price factor between levels (>1 -> more and more spaced)
        self.SKEW = 10            # size ratio between last and first order (SKEW=10 -> last order is 10x bigger than first)
        self.LEVELS = 3           # orders count of each side
        self.CAP = 1              # 85% | max capacity of portfolio used
        self.MIN_VALUE = 10.5     # minimum order notional value in $ to avoid overfragmentation / fees inefficiency
        self.POLL_DELAY = 10.1    # delay in seconds between each grid evaluation cycle
        self.THRESHOLD = 0.0001   # 0.01% | relative EMA change threshold to trigger rebalancing
        self.TIMEFRAME = "5m"     # market data candle timeframe used for indicators (SMA/EMA)
        self.WINDOW = 40          # rolling window size for indicator (SMA/EMA period)
        self.INV = False          # stop place / modify in the part against mean if activated
        self.ACCOUNT = 100
        self.LEVERAGE = 10
        # ==== STATS ====
        self.cycles = 0           # nb of cycles for stats
        self.placed = 0           # nb of place for stats
        self.modified = 0         # nb of modify for stats
        self.passed = 0           # nb of pass for stats
        self.placed_error = 0     # nb of errors while place for stats
        self.modified_error = 0   # nb of errors while modify for stats
        # ==== UTILS ====
        self.task = None
        self.running = False
        self.last_ema = None
        self.beta = self.SKEW ** (1 / (self.LEVELS - 1)) if self.LEVELS > 1 else 1.0
        self.geo = (1 - self.beta ** self.LEVELS) / (1 - self.beta) if self.beta != 1.0 else float(self.LEVELS)
        self.grid = Grid(self.LEVELS, self.SPREAD, self.OFFSET, self.ALPHA)

    def available(self, mid: float, pos_value, pos_size, bids_value: float = 0.0, asks_value: float = 0.0):
        position_value = pos_value if pos_size >= 0 else -pos_value
        leveraged_account_value = self.ACCOUNT * self.LEVERAGE
        longable = max(leveraged_account_value - position_value - bids_value, 0) # TODO adapt for multiple coins
        shortable = max(leveraged_account_value + position_value - asks_value, 0) # TODO adapt for multiple coins
        factor = self.CAP / mid # normalize $ to get [COIN]
        return factor * longable, factor * shortable # max_sz_bid, max_sz_ask

    def valid(self, is_buy, px, sz, best_bid, best_ask):
        value = px * sz
        if is_buy:
            return (value > self.MIN_VALUE) and (px <= best_ask)
        elif not is_buy:
            return (value > self.MIN_VALUE) and (px >= best_bid)
        else:
            return False

    def sizes(self, available: float) -> list[float]:
        if self.LEVELS == 0: return []
        if self.beta == 1.0 or self.LEVELS == 1:
            unit = available / self.LEVELS
            return [unit] * self.LEVELS
        s0 = available / self.geo
        return [s0 * (self.beta ** i) for i in range(self.LEVELS)]

    def compute_place(self, bids: list, asks: list, best_bid, best_ask, ema, pos_value, pos_size):
        mid = (best_bid + best_ask) / 2
        bids_px, asks_px = [b["limitPx"] for b in bids], [a["limitPx"] for a in asks]
        self.grid.update(ema)
        longable, shortable = self.available(mid, pos_value, pos_size, sum([sz * px for sz, px in bids]), sum([sz * px for sz, px in asks]))
        orders = []
        def process(existing, existing_px, grid_px, available, is_buy):
            sizes = self.sizes(available)
            size = 0
            nominal = list(reversed(sizes)) if is_buy else sizes
            missing_px = remaining(existing_px, grid_px)
            missing, ref, m = [], 0.0, 0
            for k, px in enumerate(grid_px):
                if m < len(missing_px) and px == missing_px[m]:
                    missing.append(k)
                    m += 1
                else:
                    ref += nominal[k]
            used = sum(o[0] for o in existing)
            scale = min(used / ref, 1.0) if ref > 0 else 1.0
            for k in missing:
                sz = scale * nominal[k]
                if self.valid(is_buy, grid_px[k], sz, best_bid, best_ask):
                    size += sz
                    orders.append((sz, grid_px[k]))
        if self.INV:
            if ema and ema < mid: process(bids, bids_px, self.grid.bids, longable, True)
            else: process(asks, asks_px, self.grid.asks, shortable, False)
        else:
            process(bids, bids_px, self.grid.bids, longable, True)
            process(asks, asks_px, self.grid.asks, shortable, False)
        return orders

    def compute_modify(self, bids: list, asks: list, best_bid, best_ask, ema, pos_value, pos_size): # both bids and asks in ascending order
        mid = (best_bid + best_ask) / 2
        self.grid.update(ema)
        modifies = []
        longable, shortable = self.available(mid, pos_value, pos_size, 0, 0) # disregard bids and asks value because they gonna be modified
        def process(orders: Iterable, n: int, available: float, price_fn, is_buy):
            sizes = self.sizes(available)
            size = 0
            scale = max(available / sum(sizes[-n:]), 1)
            for i, o in enumerate(orders):
                sz = scale * sizes[-(i+1)]
                size += sz
                if self.valid(is_buy, price_fn(i), sz, best_bid, best_ask): modifies.append((o, (sz, price_fn(i))))
            print(available, size, sum(sizes[-n:]))
        if self.INV:
            if ema and ema < mid: process(bids, len(bids), longable, lambda i: min(self.grid.bids[i], (1 - self.MAKER_FEES) * mid), True)
            else: process(reversed(asks), len(asks), shortable, lambda i: max(self.grid.asks[-(i + 1)], (1 + self.MAKER_FEES) * mid), False)
        else:
            process(bids, len(bids), longable, lambda i: min(self.grid.bids[i], (1 - self.MAKER_FEES) * mid), True)
            process(reversed(asks), len(asks), shortable, lambda i: max(self.grid.asks[-(i + 1)], (1 + self.MAKER_FEES) * mid), False)
        return modifies

eie = EnveloppeInvExp()

# print(remaining([5], [4, 5, 6])) # -> [4, 6]
# print(remaining([5, 6], [4, 5, 6])) # -> [4]
# print(remaining([5, 9], [4, 5, 6, 8, 9, 10, 11])) # -> [4, 6, 8, 10, 11]
# print(remaining([], [4, 5, 6])) # -> [4, 5, 6]
# print(remaining([5], [4, 7])) # -> [7]
# print(remaining([5], [4, 6])) # -> [4]
# print(remaining([5, 7, 8, 9], [4, 5, 6])) # -> []
# print(remaining([5, 8, 9], [2, 3, 4, 5, 5.5, 5.7, 6])) # -> [2, 3, 4, 5.5]

# eie.compute(bids, bids_px, asks, asks_px, asks_size, mid, ema, pos_value, pos_size)
# print(eie.compute([], [], [], [], 100, 100, 0, 0)) # -> [(5.233728905610283, 98.0), (3.0216947925196225, 99.0), (1.7445763018700946, 100.0), (1.7445763018700946, 100.0), (3.0216947925196225, 101.0), (5.233728905610283, 102.0)]
# print(eie.compute([(5.233728905610283, 98.0), (3.0216947925196225, 99.0)], [98, 99], [(3.0216947925196225, 101.0), (5.233728905610283, 102.0)], [101, 102], 100, 100, 0, 0)) # -> [(1.7445763018700946, 100.0), (1.7445763018700946, 100.0)]
# print(eie.compute([(2.61686445281, 98.0), (1.5, 99.0)], [98, 99], [(1.5, 101.0), (2.61686445281, 102.0)], [101, 102], 100, 100, 0, 0)) # -> [(0.87228815093, 100.0), (0.87228815093, 100.0)]
# print(eie.compute([(5.233728905610283, 98.0)], [98], [(4, 102.0)], [102], 100, 100, 0, 0)) # -> [(1.7445763018700946, 100.0), (1.7445763018700946, 100.0)]
# print(eie.compute([(2.61686445281, 98.0), (0.5, 99.0)], [98, 99], [(1, 101.0), (2.61686445281, 102.0)], [101, 102], 100, 100, 0, 0)) # -> [(un peu moins que 0.87228815093 avec la moyenne du 0.5 diminuée, 100.0), (un peu moins que 0.87228815093 avec la moyenne du 0.5 diminué, 100.0)]
# print(eie.compute([], [], [], [], 100, 100, 1000, 10)) # -> [(3.489152603740189, 100.0), (6.043389585039245, 101.0), (10.467457811220566, 102.0)]
# print(eie.compute([], [], [], [], 100, 100, 500, 5)) # -> [(2.6168644528051415, 98.0), (1.5108473962598112, 99.0), (0.8722881509350473, 100.0), (2.616864452805142, 100.0), (4.532542188779434, 101.0), (7.850593358415424, 102.0)]
# print(eie.available(100, 1000, 10, 0, 1000))
# print(eie.compute([], [], [(5, 100.0), (6, 101.0)], [100, 101], 100, 100, 1000, 10))


# print(eie.available(100, 1000, 10, 0, 0))
print(eie.compute_place([], [], 100, 100, 100, 0, 0))
# print(eie.modify([(3, 98), (2, 99)], [(1, 100), (2, 101), (3, 102)], 99.5, 100, 0, 0))
