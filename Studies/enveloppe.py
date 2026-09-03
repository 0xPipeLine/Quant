import numpy as np

W = 1.2  # Skew
T = 10000.0  # Notionnal value
S = 1.012  # Spread
n = 5  # Levels count
a = 1  # Price variation factor
M = 100.0  # Center


def price(i, M, S, a, n):
    if np.isclose(a, 1.0):
        if n == 1:
            return M
        return M * ((S - 1) * (i / (n - 1)) + 1)
    else:
        return M * ((S - 1) * (a**i - 1) / (a ** (n - 1) - 1) + 1)


def size(i, W, T, S, n, a, M):
    if n <= 1:
        raise ValueError("n must be > 1")
    b = W ** (1 / (n - 1))
    if np.isclose(b, 1.0):
        t = float(n)
    else:
        t = (1 - b**n) / (1 - b)
    ba = b * a
    if np.isclose(ba, 1.0):
        p = float(n)
    else:
        p = (1 - (ba) ** n) / (1 - ba)
    if np.isclose(a, 1.0):
        d = 0.0
    else:
        d = (S - 1) / (a ** (n - 1) - 1)
    denominateur_sz0 = M * (t * (1 - d) + d * p)
    Sz0 = T / denominateur_sz0 if denominateur_sz0 != 0 else 0.0
    return Sz0 * (b**i)


def barycentre(W, S, n, a, M):
    if n <= 1:
        raise ValueError("n must be > 1")
    b = W ** (1 / (n - 1))
    if np.isclose(b, 1.0):
        t = float(n)
    else:
        t = (1 - b**n) / (1 - b)
    ba = b * a
    if np.isclose(ba, 1.0):
        p = float(n)
    else:
        p = (1 - (ba) ** n) / (1 - ba)
    if np.isclose(a, 1.0):
        d = 0.0
    else:
        d = (S - 1) / (a ** (n - 1) - 1)
    G = (M / t) * (t * (1 - d) + d * p) if t != 0 else M
    return G


def lim_bary(W, S, M, a):
    if a > 1:
        return M
    elif np.isclose(a, 1.0):
        if W <= 0:
            raise ValueError("W must be positive")
        if np.isclose(W, 1.0):
            return M
        term = (W * (np.log(W) - 1) + 1) / ((W - 1) * np.log(W))
        return M * (1 + (S - 1) * term)
    else:
        return M * S

def bsr(W, S, n, a, M):
    D = M * (S - 1)
    return 100 * (barycentre(W, S, n, a, M) - M) / D if D != 0 else 0.0

print(f"--- Parameters ---")
print(f"W={W}, T={T}, S={S}, n={n}, a={a}, M={M}\n")

print("--- Levels ---")
for i in range(n):
    p_i = price(i, M, S, a, n)
    sz_i = size(i, W, T, S, n, a, M)
    print(f"level {i} -> (Pxi): {p_i:.4f} | (Szi): {sz_i:.4f}")

G = barycentre(W, S, n, a, M)
print(f"\nBarycentre G : {G:.4f}")
BSr = bsr(W, S, n, a, M)
print(f"Barycentre spread ratio (%) : {BSr:.2f}%")

lim_g = lim_bary(W, S, M, a)
print(f"Barycentre lim (n -> inf) : {lim_g:.4f}")
