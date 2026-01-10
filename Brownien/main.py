from market import Market
from portfolio import Portfolio
from strategies import Strategies

class Tester:
    def __init__(self, pf):
        self.portfolio = Portfolio(pf)
        self.market = Market()
        self.strategies = Strategies()

    def fill_clear(self, min, max, spread_max):
        mid = (min + max) / 2
        limit_min = mid * ( 1 - (spread_max / 2))
        limit_max = mid * ( 1 + (spread_max / 2))
        to_cancel = []
        for i in range(self.portfolio.orderbook.orders_count):
            order = self.portfolio.orderbook.orders[i]
            if order["active"]:
                if order["is_buy"]:
                    if order["price"] > min:
                        self.portfolio.fill(order["id"])
                    elif order["price"] < limit_min:
                        to_cancel.append(order["id"])
                else:
                    if order["price"] < max:
                        self.portfolio.fill(order["id"])
                    elif order["price"] > limit_max:
                        to_cancel.append(order["id"])
        self.portfolio.orderbook.bulk_remove(to_cancel)

    def simple_mm(self, spread):
        for price in self.market.prices:
            self.fill_clear(price, price, spread * 3)
            self.portfolio.limit_order(False, 0.1, price * ( 1 + (spread/2)))
            self.portfolio.limit_order(True, 0.1, price * ( 1 - (spread/2)))

t = Tester(1000)
t.simple_mm(0.001)

print(round(t.portfolio.value(t.market.prices[-1]), 2), round(t.portfolio.volume, 2 ))
