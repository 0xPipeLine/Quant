import numpy as np

class OrderBook:
    def __init__(self, max_orders=100_000):
        self.max_orders = max_orders
        self.orders_dtype = np.dtype([
            ('is_buy', np.bool_),
            ('size', np.float32),
            ('id', np.int32),
            ('price', np.float32),
            ('active', np.bool_) ])
        self.orders = np.zeros(max_orders, dtype=self.orders_dtype)
        self.orders_count = 0
        self.free_ids = []

    def order(self, is_buy: bool, size: float, price: float) -> int:
        if self.free_ids:
            idx = self.free_ids.pop()
        elif self.orders_count < self.max_orders:
            idx = self.orders_count
            self.orders_count += 1
        else:
            raise RuntimeError("Carnet saturé : pas assez de slots dispo")
        self.orders[idx] = (is_buy, size, idx, price, True)
        return idx

    def bulk_remove(self, ids):
        self.orders['active'][ids] = False
        self.free_ids.extend(ids)

    def get_active_orders(self):
        active_mask = self.orders['active']
        return self.orders[active_mask]

    def __len__(self):
        return np.sum(self.orders['active'])

class Portfolio:
    def __init__(self, capital, fees_limit = 0.0004, fees_market = 0.0007, max_orders=100_000, min_order=10):
        self.capital = capital
        self.usd = capital
        self.coin = 0
        self.busd = 0 # bothered usd
        self.bcoin = 0 # bothered coin
        self.orderbook = OrderBook(max_orders)
        self.min_order = min_order
        self.fees_limit = fees_limit
        self.fees_market = fees_market
        self.volume = 0

    def limit_order(self, is_buy: bool, size: float, price: float) -> int:
        size_usd = size * price
        if (size_usd > self.min_order):
            if is_buy:
                if (size_usd < (self.usd - self.busd)):
                    id = self.orderbook.order(is_buy, size, price)
                    self.busd += size_usd
                    return id
            else:
                if (size < (self.coin - self.bcoin)):
                    id = self.orderbook.order(is_buy, size, price)
                    self.bcoin += size
                    return id

    def fill(self, order_id: int):
        order = self.orderbook.orders[order_id]
        if not order['active']:
            return
        order['active'] = False
        self.orderbook.free_ids.append(order_id)
        size_usd = order['size'] * order['price']
        fee = size_usd * self.fees_limit
        if order['is_buy']:
            self.usd -= size_usd + fee
            self.coin += order['size']
            self.busd -= size_usd
        else:
            self.usd += size_usd - fee
            self.coin -= order['size']
            self.bcoin -= order['size']
        self.volume += size_usd

    def value(self, price):
        return self.usd + (self.coin * price)
