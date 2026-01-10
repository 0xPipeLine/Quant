import numpy as np

# Paramètres de la simulation
n_simulations = 50
n = 120960
dt = 1 / (365 * 24 * 60)
S0 = 180
volatility = 0.12
min_drift = -0.10
max_drift = 0.10
jump_min = 0.005
jump_max = 0.01
jump_prob = 0.0000006

# Stratégie
initial_cash = 1000
drop_trigger = 0.005
profit_target = 0.005
fees = 0.0004

# Résultats stockés
final_values = []
trade_counts = []
volumes = []

for sim in range(n_simulations):
    # --- Génération d'une série de prix ---
    mu = np.random.uniform(min_drift, max_drift)
    returns = (mu - 0.5 * volatility**2) * dt + volatility * np.sqrt(dt) * np.random.randn(n)
    jumps = np.zeros(n)
    jump_events = np.random.rand(n) < jump_prob
    jumps[jump_events] = np.random.uniform(-jump_min, jump_max, size=jump_events.sum())
    total_returns = returns + jumps
    prices = S0 * np.exp(np.cumsum(total_returns))

    # --- Application de la stratégie ---
    cash = initial_cash
    holdings = []
    last_buy_price = prices[0]
    trade_count = 0
    total_volume = 0

    for i in range(1, len(prices)):
        price = prices[i]

        # Achat
        if price <= last_buy_price * (1 - drop_trigger) and cash > 0:
            invest_cash = cash * 0.5
            price_with_fee = price * (1 + fees)
            quantity = invest_cash / price_with_fee

            if quantity > 0:
                trade_amount = quantity * price_with_fee
                total_volume += trade_amount
                cash -= trade_amount
                target_price = price * (1 + profit_target)
                holdings.append({'qty': quantity, 'buy_price': price, 'target_price': target_price})
                last_buy_price = price
                trade_count += 1

        # Vente
        new_holdings = []
        for pos in holdings:
            if price >= pos['target_price']:
                sell_price_with_fee = price * (1 - fees)
                sell_amount = pos['qty'] * sell_price_with_fee
                total_volume += sell_amount
                cash += sell_amount
                trade_count += 1
            else:
                new_holdings.append(pos)
        holdings = new_holdings

    final_value = cash + sum(pos['qty'] * prices[-1] for pos in holdings)
    final_values.append(final_value)
    trade_counts.append(trade_count)
    volumes.append(total_volume)

# --- Résultats finaux ---
final_values = np.array(final_values)
trade_counts = np.array(trade_counts)
volumes = np.array(volumes)

print("✅ Résultats sur 100 simulations :\n")
print(f"📊 Valeur moyenne finale du wallet : {final_values.mean():.2f} $")
print(f"📉 Valeur la plus basse atteinte : {final_values.min():.2f} $")
print(f"🚀 Valeur la plus haute atteinte : {final_values.max():.2f} $")
print(f"🔁 Nombre moyen de trades effectués : {trade_counts.mean():.2f}")
print(f"📦 Volume moyen échangé : {volumes.mean():.2f} $")
print(f"📦 Volume minimum observé : {volumes.min():.2f} $")
print(f"📦 Volume maximum observé : {volumes.max():.2f} $")
