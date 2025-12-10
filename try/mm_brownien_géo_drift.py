import numpy as np
import matplotlib.pyplot as plt

# Paramètres de la simulation
n = 120960                   # Nombre de bougies
dt = 1 / (365 * 24 * 60)     # Taille d'un pas (si 1 minute par bougie, dt en années)
S0 = 180                     # Prix initial
volatility = 0.14            # Volatilité annuelle (20 %)
min_drift = -0.11           # Drift min (−10%)
max_drift = 0.11            # Drift max (+10%)

# Sauts (jumps)
jump_min = 0.005            # 0.5%
jump_max = 0.01             # 1%
jump_prob = 0.0000006       # 0.00006 %

# Générer le drift aléatoire
mu = np.random.uniform(min_drift, max_drift)
returns = (mu - 0.5 * volatility**2) * dt + volatility * np.sqrt(dt) * np.random.randn(n)

# Gestion des jumps aléatoires
jumps = np.zeros(n)
jump_events = np.random.rand(n) < jump_prob
jumps[jump_events] = np.random.uniform(-jump_max, jump_max, size=jump_events.sum())

# Ajout des jumps aux rendements
total_returns = returns + jumps

# Calcul du prix
prices = S0 * np.exp(np.cumsum(total_returns))

print("hello")

# Affichage
plt.figure(figsize=(14, 5))
plt.plot(prices)
plt.title('Simulation de marché avec mouvement brownien géométrique + jumps')
plt.xlabel('Bougie')
plt.ylabel('Prix ($)')
plt.grid(True)
plt.show()
