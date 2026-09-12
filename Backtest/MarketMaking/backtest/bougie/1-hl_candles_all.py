"""
Hyperliquid API - Historique + enregistrement en temps réel + bougie live
Token   : xyz:XYZ100 (HIP-3)
Interval: voir INTERVAL dans la configuration
Fichier : xyz_XYZ100_<interval>.csv  (créé/mis à jour automatiquement)

Fonctionnement :
  1. Charge les 50 dernières bougies fermées dans le CSV.
  2. Affiche en temps réel la bougie en cours (open, high, low, close, volume, trades)
     rafraîchie toutes les secondes sur la même ligne.
  3. Dès qu'une bougie se ferme → l'enregistre dans le CSV, affiche la ligne définitive.
  4. Boucle indéfiniment (Ctrl+C pour stopper).
"""

import csv
import os
import sys
import time
from datetime import datetime, timezone

import requests


# ══════════════════════════════════════════════════════════════════════════════
# ██  CONFIGURATION  ██
# ══════════════════════════════════════════════════════════════════════════════

COIN         = "xyz:SILVER"  #xyz:SP500"             #"xyz:XYZ100"   # token à suivre

# Intervalle des bougies — valeurs acceptées par Hyperliquid :
#   "1m"  "3m"  "5m"  "15m"  "30m"  "1h"  "2h"  "4h"  "8h"  "12h"  "1d"  "3d"  "1w"
INTERVAL     = "1m"

# Nombre de bougies à charger au démarrage (historique)
N_HISTORY    = 5000

# Période du SMA (moyenne mobile simple) — ex: 20 pour SMA20, 50 pour SMA50
SMA_PERIOD   = 50

# Rafraîchissement de la bougie en cours (secondes)
LIVE_REFRESH = 10

# Période des EMA (hausse / baisse / range) — en nombre de bougies
BOUGIE_PERIOD = 9

# ──────────────────────────────────────────────────────────────────────────────
# Ne pas modifier — calculé automatiquement depuis INTERVAL
# ──────────────────────────────────────────────────────────────────────────────
API_URL      = "https://api.hyperliquid.xyz/info"
_INTERVAL_MINUTES = {"1m":1,"3m":3,"5m":5,"15m":15,"30m":30,
                     "1h":60,"2h":120,"4h":240,"8h":480,"12h":720,
                     "1d":1440,"3d":4320,"1w":10080}
INTERVAL_MS  = _INTERVAL_MINUTES[INTERVAL] * 60 * 1000
CSV_FILE = f"../../data/{COIN.replace(':', '_')}_{INTERVAL}.csv"
HIST_CSV_FILE = f"../../data/{COIN.replace(':', '_')}_{INTERVAL}_indicateur.csv"
POLL_SECS    = max(5, _INTERVAL_MINUTES[INTERVAL] * 5)  # ~5% de l'intervalle
# ══════════════════════════════════════════════════════════════════════════════

CSV_HEADERS = ["open_ts", "close_ts", "open", "high", "low", "close", "volume", "trades"]

HIST_CSV_HEADERS = [
    "open_ts", "close_ts",
    "open", "high", "low", "close", "volume", "trades",
    f"sma{SMA_PERIOD}", f"smma{SMA_PERIOD}",
    f"ema_up{BOUGIE_PERIOD}", f"ema_down{BOUGIE_PERIOD}", f"ema_range{BOUGIE_PERIOD}",
]


# ── Helpers ────────────────────────────────────────────────────────────────────

def now_ms() -> int:
    return int(time.time() * 1000)


def candle_to_row(c: dict) -> dict:
    return {
        "open_ts"  : c["t"],
        "close_ts" : c["T"],
        "open"     : c["o"],
        "high"     : c["h"],
        "low"      : c["l"],
        "close"    : c["c"],
        "volume"   : c["v"],
        "trades"   : c["n"],
    }


# ── EMA ────────────────────────────────────────────────────────────────────────
# Formule : EMA_n = EMA_{n-1} * (1 - k) + valeur * k   avec k = 2 / (période + 1)
# EMA↑    : high - low  si midpoint_actuel > midpoint_précédent, sinon 0
# EMA↓    : high - low  si midpoint_actuel < midpoint_précédent, sinon 0
# EMArange: high - low  toujours (sans filtre)

def _ema_k() -> float:
    return 2 / (BOUGIE_PERIOD + 1)

def _midpoint(c: dict) -> float:
    return (float(c["h"]) + float(c["l"])) / 2

def _next_ema(prev: float | None, value: float) -> float:
    if prev is None:
        return value
    return prev * (1 - _ema_k()) + value * _ema_k()

def _candle_emas(c: dict, prev_c: dict | None,
                 ema_up: float | None, ema_down: float | None, ema_range: float | None):
    rang = float(c["h"]) - float(c["l"])
    if prev_c is None:
        val_up, val_down = 0.0, 0.0
    else:
        mid_curr = _midpoint(c)
        mid_prev = _midpoint(prev_c)
        val_up   = rang if mid_curr > mid_prev else 0.0
        val_down = rang if mid_curr < mid_prev else 0.0
    return (
        _next_ema(ema_up,    val_up),
        _next_ema(ema_down,  val_down),
        _next_ema(ema_range, rang),
    )


# ── API ────────────────────────────────────────────────────────────────────────

def fetch_candles(start_ms: int, end_ms: int) -> list[dict]:
    payload = {
        "type": "candleSnapshot",
        "req": {
            "coin"     : COIN,
            "interval" : INTERVAL,
            "startTime": start_ms,
            "endTime"  : end_ms,
        },
    }
    r = requests.post(API_URL, json=payload,
                      headers={"Content-Type": "application/json"}, timeout=10)
    r.raise_for_status()
    return r.json()


def closed_candles(start_ms: int, end_ms: int) -> list[dict]:
    """Bougies dont la clôture (T) est déjà passée."""
    return [c for c in fetch_candles(start_ms, end_ms) if c["T"] < end_ms]


def fetch_live_candle() -> dict | None:
    """
    Récupère la bougie actuellement ouverte en passant endTime dans le futur.
    L'API retourne la bougie dont T >= now, i.e. celle qui n'est pas encore fermée.
    """
    t_now = now_ms()
    # On prend la fenêtre : début du multiple de 5min en cours → +10min (futur)
    start = (t_now // INTERVAL_MS) * INTERVAL_MS
    end   = start + INTERVAL_MS * 2
    candles = fetch_candles(start, end)
    # La bougie live est celle dont T >= now (pas encore fermée)
    live = [c for c in candles if c["T"] >= t_now]
    return live[0] if live else None


# ── CSV helpers ────────────────────────────────────────────────────────────────

def load_known_timestamps() -> set[int]:
    if not os.path.exists(CSV_FILE):
        return set()
    with open(CSV_FILE, newline="") as f:
        reader = csv.DictReader(f)
        known = set()
        for row in reader:
            if "open_ts" in row:
                known.add(int(row["open_ts"]))
        return known


def append_rows(rows: list[dict]) -> None:
    new_file = not os.path.exists(CSV_FILE)
    with open(CSV_FILE, "a", newline="") as f:
        w = csv.DictWriter(f, fieldnames=CSV_HEADERS)
        if new_file:
            w.writeheader()
        w.writerows(rows)


# ── Export historique avec indicateurs ────────────────────────────────────────

def save_hist_csv(rows: list[dict]) -> None:
    """Écrit (ou écrase) le CSV historique complet avec tous les indicateurs."""
    with open(HIST_CSV_FILE, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=HIST_CSV_HEADERS)
        w.writeheader()
        w.writerows(rows)
    print(f"[HIST] {len(rows)} lignes avec indicateurs → «{HIST_CSV_FILE}»")


def append_hist_row(row: dict) -> None:
    """Ajoute une ligne au CSV historique (bougie nouvellement fermée)."""
    new_file = not os.path.exists(HIST_CSV_FILE)
    with open(HIST_CSV_FILE, "a", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=HIST_CSV_HEADERS)
        if new_file:
            w.writeheader()
        w.writerow(row)


def make_hist_row(c: dict, ma: float | None, smma: float | None,
                  ema_up: float | None, ema_down: float | None, ema_range: float | None) -> dict:
    """Construit un dict compatible HIST_CSV_HEADERS à partir d'une bougie et de ses indicateurs."""
    def _s(v): return f"{v:.8f}" if v is not None else ""
    return {
        "open_ts"                   : c["t"],
        "close_ts"                  : c["T"],
        "open"                      : c["o"],
        "high"                      : c["h"],
        "low"                       : c["l"],
        "close"                     : c["c"],
        "volume"                    : c["v"],
        "trades"                    : c["n"],
        f"sma{SMA_PERIOD}"          : _s(ma),
        f"smma{SMA_PERIOD}"         : _s(smma),
        f"ema_up{BOUGIE_PERIOD}"    : _s(ema_up),
        f"ema_down{BOUGIE_PERIOD}"  : _s(ema_down),
        f"ema_range{BOUGIE_PERIOD}" : _s(ema_range),
    }


# ── Affichage ──────────────────────────────────────────────────────────────────

HEADER_LINE = (
    f"{'#':<5}  {'open_ts':<15}  {'close_ts':<15}  "
    f"{'open':>12}  {'high':>12}  {'low':>12}  {'close':>12}  "
    f"{'volume':>14}  {'trades':<7}  "
    f"{'SMA'+str(SMA_PERIOD):>10}  {'SMMA'+str(SMA_PERIOD):>11}  "
    f"{'EMA↑':>10}  {'EMA↓':>10}  {'EMArange':>10}  tag"
)
SEP_LINE = "─" * len(HEADER_LINE)

counter = 0


def print_candle(c: dict, label: str, ma50: float | None = None, smma: float | None = None,
                 ema_up: float | None = None, ema_down: float | None = None, ema_range: float | None = None) -> None:
    """Imprime une ligne définitive (bougie fermée ou historique)."""
    global counter
    counter += 1
    def _f(v, w): return f"{v:>{w}.4f}" if v is not None else f"{'—':>{w}}"
    print(
        f"{counter:<5}  {c['t']:<15}  {c['T']:<15}  "
        f"{c['o']:>12}  {c['h']:>12}  {c['l']:>12}  {c['c']:>12}  "
        f"{float(c['v']):>14.4f}  {c['n']:<7}  {_f(ma50,10)}  {_f(smma,11)}  "
        f"{_f(ema_up,10)}  {_f(ema_down,10)}  {_f(ema_range,10)}  [{label}]"
    )


def print_live(c: dict, ma50: float | None, smma: float | None, remaining_s: float,
               ema_up: float | None = None, ema_down: float | None = None, ema_range: float | None = None) -> None:
    """
    Écrase la ligne courante avec les données live de la bougie en cours.
    Affiche aussi le temps restant avant fermeture.
    """
    def _f(v, w): return f"{v:>{w}.4f}" if v is not None else f"{'—':>{w}}"
    line = (
        f"LIVE   {c['t']:<15}  {c['T']:<15}  "
        f"{c['o']:>12}  {c['h']:>12}  {c['l']:>12}  {c['c']:>12}  "
        f"{float(c['v']):>14.4f}  {c['n']:<7}  {_f(ma50,10)}  {_f(smma,11)}  "
        f"{_f(ema_up,10)}  {_f(ema_down,10)}  {_f(ema_range,10)}  "
        f"[LIVE {remaining_s:>4.0f}s]"
    )
    sys.stdout.write(f"\r{line:<{len(HEADER_LINE) + 10}}")
    sys.stdout.flush()


# ── Étape 1 : historique ───────────────────────────────────────────────────────

def load_history() -> tuple[int, list[float]]:
    print(f"[INIT] Chargement des {N_HISTORY} dernières bougies fermées pour {COIN}…\n")

    t_now   = now_ms()
    start   = t_now - (N_HISTORY + 2) * INTERVAL_MS
    candles = closed_candles(start, t_now)[-N_HISTORY:]

    if not candles:
        raise RuntimeError("Aucune bougie retournée — vérifiez le nom du token.")

    known        = load_known_timestamps()
    new_rows     = []
    hist_rows    = []   # lignes avec indicateurs pour le CSV historique
    close_window = []
    smma_val     = None   # None tant que pas encore initialisée
    ema_up_val   = None
    ema_down_val = None
    ema_range_val= None
    prev_c       = None

    print(HEADER_LINE)
    print(SEP_LINE)

    for c in candles:
        close = float(c["c"])
        close_window.append(close)
        window = close_window[-SMA_PERIOD:]
        ma = sum(window) / len(window) if len(window) == SMA_PERIOD else None

        # SMMA : s'initialise dès qu'on a SMA_PERIOD closes (= première SMA),
        # puis se met à jour avec la formule de lissage
        if smma_val is None and len(close_window) == SMA_PERIOD:
            smma_val = sum(close_window) / SMA_PERIOD   # amorçage = SMA
        elif smma_val is not None:
            smma_val = (smma_val * (SMA_PERIOD - 1) + close) / SMA_PERIOD

        ema_up_val, ema_down_val, ema_range_val = _candle_emas(
            c, prev_c, ema_up_val, ema_down_val, ema_range_val
        )
        prev_c = c
        print_candle(c, label="HIST", ma50=ma, smma=smma_val,
                     ema_up=ema_up_val, ema_down=ema_down_val, ema_range=ema_range_val)
        row = candle_to_row(c)
        if row["open_ts"] not in known:
            new_rows.append(row)
        hist_rows.append(make_hist_row(c, ma, smma_val, ema_up_val, ema_down_val, ema_range_val))

    if new_rows:
        append_rows(new_rows)

    save_hist_csv(hist_rows)   # écrit/écrase le CSV historique complet

    print(SEP_LINE)
    print(f"[INIT] {len(candles)} bougies chargées — {len(new_rows)} écrites dans «{CSV_FILE}»\n")

    return candles[-1]["T"], close_window[-SMA_PERIOD:], smma_val, ema_up_val, ema_down_val, ema_range_val, prev_c


# ── Étape 2 : boucle temps réel ────────────────────────────────────────────────

def watch(last_close_ms: int, close_window: list[float], smma_val: float | None,
          ema_up_val: float | None, ema_down_val: float | None, ema_range_val: float | None,
          prev_c: dict | None) -> None:
    print("[LIVE] Surveillance en cours — Ctrl+C pour arrêter.\n")
    print(HEADER_LINE)
    print(SEP_LINE)

    while True:
        next_close_ms = last_close_ms + INTERVAL_MS

        # ── Boucle de rafraîchissement de la bougie en cours ──
        while now_ms() < next_close_ms:
            remaining_s = (next_close_ms - now_ms()) / 1000
            try:
                live = fetch_live_candle()
            except requests.RequestException:
                live = None

            if live:
                provisional_window = close_window[-(SMA_PERIOD-1):] + [float(live["c"])]
                ma = sum(provisional_window) / len(provisional_window) if len(provisional_window) == SMA_PERIOD else None
                # SMMA provisoire avec le close live (non définitif)
                if smma_val is not None:
                    smma_live = (smma_val * (SMA_PERIOD - 1) + float(live["c"])) / SMA_PERIOD
                else:
                    smma_live = None
                eu, ed, er = _candle_emas(live, prev_c, ema_up_val, ema_down_val, ema_range_val)
                print_live(live, ma, smma_live, remaining_s,
                           ema_up=eu, ema_down=ed, ema_range=er)
            else:
                sys.stdout.write(f"\r  … attente bougie (dans {remaining_s:.0f}s)          ")
                sys.stdout.flush()

            time.sleep(LIVE_REFRESH)

        # ── La bougie vient de se fermer : on la récupère ──
        print()   # saut de ligne après la dernière ligne LIVE
        time.sleep(2)  # petite marge pour que l'API publie la bougie fermée

        try:
            t_now   = now_ms()
            start   = last_close_ms - INTERVAL_MS
            candles = closed_candles(start, t_now)
        except requests.RequestException as e:
            print(f"\n[WARN] Erreur réseau : {e} — retente dans {POLL_SECS}s")
            time.sleep(POLL_SECS)
            continue

        new = [c for c in candles if c["T"] > last_close_ms]

        if not new:
            time.sleep(POLL_SECS)
            continue

        rows = [candle_to_row(c) for c in new]
        append_rows(rows)

        for c in new:
            close = float(c["c"])
            close_window.append(close)
            if len(close_window) > SMA_PERIOD:
                close_window.pop(0)
            ma = sum(close_window) / len(close_window) if len(close_window) == SMA_PERIOD else None
            # SMMA définitive
            if smma_val is None and len(close_window) == SMA_PERIOD:
                smma_val = sum(close_window) / SMA_PERIOD
            elif smma_val is not None:
                smma_val = (smma_val * (SMA_PERIOD - 1) + close) / SMA_PERIOD
            ema_up_val, ema_down_val, ema_range_val = _candle_emas(
                c, prev_c, ema_up_val, ema_down_val, ema_range_val
            )
            prev_c = c
            print_candle(c, label="NEW ", ma50=ma, smma=smma_val,
                         ema_up=ema_up_val, ema_down=ema_down_val, ema_range=ema_range_val)
            append_hist_row(make_hist_row(c, ma, smma_val, ema_up_val, ema_down_val, ema_range_val))

        last_close_ms = new[-1]["T"]


# ── Point d'entrée ─────────────────────────────────────────────────────────────

def main():
    try:
        last_close, close_window, smma_val, eu, ed, er, prev_c = load_history()
        watch(last_close, close_window, smma_val, eu, ed, er, prev_c)
    except KeyboardInterrupt:
        print(f"\n\n[STOP] Arrêt demandé. Données sauvegardées dans : {CSV_FILE}")
    except Exception as e:
        print(f"\n[ERROR] {e}")
        raise


if __name__ == "__main__":
    main()
