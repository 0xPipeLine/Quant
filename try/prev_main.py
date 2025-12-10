import numpy as np

class Order:
    def __init__(self, is_buy, size_usd, order_id):
        self.is_buy = is_buy
        self.size_usd = size_usd
        self.order_id = order_id

class PF:
    def __init__(self, capital, fees_limit, fees_market, strategy):
        self.capital = capital
        self.fees_limit = fees_limit
        self.fees_market = fees_market
        self.strategy = strategy
        self.orders = []
        self.orders_num = 0
        self.volume = 0

    def order(self, is_buy, size_usd):
        order = Order(is_buy, size_usd, self.orders_num)
        self.orders_num += 1
        self.orders.append(order)
        return self.orders_num - 1

    def bulk_remove(self, orders_ids):
        self.orders = [order for order in self.orders if order.order_id not in orders_ids]
