import numpy as np

class Indicators:
    def __init__(self, prices, short_sz=9, long_sz=24, rsi_sz=14):
        self.prices = prices.astype(np.float64)
        self.emas_short = self.emas(short_sz)
        self.emas_long = self.emas(long_sz)
        self.rsis = self.rsis(rsi_sz)

    def emas(self, n):
        alpha = 2 / (n + 1)
        emas = np.zeros_like(self.prices)
        emas[0] = self.prices[0]
        for i in range(1, len(self.prices)):
            emas[i] = alpha * self.prices[i] + (1 - alpha) * emas[i - 1]
        return emas

    def rsis(self, n):
        delta = np.diff(self.prices)
        gain = np.where(delta > 0, delta, 0.0)
        loss = np.where(delta < 0, -delta, 0.0)
        gain = np.concatenate(([0.0], gain))
        loss = np.concatenate(([0.0], loss))
        avg_gain = np.empty_like(self.prices, dtype=np.float64)
        avg_loss = np.empty_like(self.prices, dtype=np.float64)
        avg_gain[:n] = np.nan
        avg_loss[:n] = np.nan
        avg_gain[n] = np.mean(gain[:n])
        avg_loss[n] = np.mean(loss[:n])
        for i in range(n + 1, len(self.prices)):
            avg_gain[i] = (avg_gain[i - 1] * (n - 1) + gain[i]) / n
            avg_loss[i] = (avg_loss[i - 1] * (n - 1) + loss[i]) / n
        rs = avg_gain / avg_loss
        rsis = 100 - (100 / (1 + rs))
        rsis[:n] = np.nan
        return rsis
