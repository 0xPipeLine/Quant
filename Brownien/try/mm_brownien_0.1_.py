import numpy as np
import matplotlib.pyplot as plt

# Paramètres de la simulation
n = 120960                   # Nombre de bougies
dt = 1 / (365 * 24 * 60)     # Taille d'un pas (si 1 minute par bougie, dt en années)
S0 = 180                     # Prix initial
volatility = 0.12            # Volatilité annuelle (20 %)
min_drift = -0.10           # Drift min (−10%)
max_drift = 0.10            # Drift max (+10%)

# Sauts (jumps)
jump_min = 0.005            # 0.5%
jump_max = 0.01             # 1%
jump_prob = 0.0000006       # 0.00006 %

# Générer le drift aléatoire
mu = np.random.uniform(min_drift, max_drift)

# Génération du mouvement brownien géométrique
returns = (mu - 0.5 * volatility**2) * dt + volatility * np.sqrt(dt) * np.random.randn(n)

# Gestion des jumps aléatoires
jumps = np.zeros(n)
jump_events = np.random.rand(n) < jump_prob
jumps[jump_events] = np.random.uniform(-jump_max, jump_max, size=jump_events.sum())

# Ajout des jumps aux rendements
total_returns = returns + jumps

# Calcul du prix
prices = S0 * np.exp(np.cumsum(total_returns))

# Affichage
plt.figure(figsize=(14, 5))
plt.plot(prices)
plt.title('Simulation de marché avec mouvement brownien géométrique + jumps')
plt.xlabel('Bougie')
plt.ylabel('Prix ($)')
plt.grid(True)
plt.show()


# ---------- Stratégie market making équilibrée (500$ cash + 500$ actif) ----------
initial_cash = 1000
cash = 500
holdings_qty = 500 / prices[0]  # investissement initial en actif

order_size_usd = 11
fees = 0.0004

wallet_value = []
trade_count = 0
volume_total = 0

for i in range(1, len(prices)):
    price = prices[i]

    bid_price = price * (1 - 0.001)
    ask_price = price * (1 + 0.001)

    # Si on est all-in en cash, on ne fait que acheter
    if holdings_qty == 0:
        if cash >= order_size_usd:
            qty = order_size_usd / bid_price
            total_cost = qty * bid_price * (1 + fees)
            if total_cost <= cash:
                cash -= total_cost
                holdings_qty += qty
                trade_count += 1
                volume_total += qty * bid_price

    # Si on est all-in en actif, on ne fait que vendre
    elif cash == 0:
        qty = order_size_usd / ask_price
        if qty <= holdings_qty:
            total_gain = qty * ask_price * (1 - fees)
            cash += total_gain
            holdings_qty -= qty
            trade_count += 1
            volume_total += qty * ask_price

    # Sinon, on fait les deux ordres
    else:
        # Achat
        if cash >= order_size_usd:
            qty = order_size_usd / bid_price
            total_cost = qty * bid_price * (1 + fees)
            if total_cost <= cash:
                cash -= total_cost
                holdings_qty += qty
                trade_count += 1
                volume_total += qty * bid_price

        # Vente
        qty = order_size_usd / ask_price
        if qty <= holdings_qty:
            total_gain = qty * ask_price * (1 - fees)
            cash += total_gain
            holdings_qty -= qty
            trade_count += 1
            volume_total += qty * ask_price

    total_wallet = cash + holdings_qty * price
    wallet_value.append(total_wallet)


# -------- Résultat --------
print(f"✅ Résultat final :")
print(f"💰 Valeur finale du wallet : {wallet_value[-1]:.2f} $")
print(f"📉 Valeur la plus basse : {min(wallet_value):.2f} $")
print(f"🚀 Valeur la plus haute : {max(wallet_value):.2f} $")
print(f"🔁 Nombre total de trades : {trade_count}")
print(f"📦 Volume total échangé : {volume_total:.2f} $")

# -------- Graphique --------
plt.figure(figsize=(14, 5))
plt.plot(wallet_value)
plt.title("Évolution de la valeur du wallet - MM (-0.1% / +0.1%) - Démarrage 50/50")
plt.xlabel("Bougie")
plt.ylabel("Valeur du wallet ($)")
plt.grid(True)
plt.show()
