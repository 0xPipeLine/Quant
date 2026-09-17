# grid — grille à pas absolu, ancrée au prix de départ

## Description

Transposition de `grid.py`. L'ancre est le mid au premier appel. Les niveaux
sont à `ancre + i·delta ∓ gap/2`, en **unités de prix absolues**, pas en
pourcentage : la grille ne bouge pas avec le marché. Chaque ordre a un
notionnel fixe `value`. Les prix sont ramenés au touch : `bid = min(niveau,
best_bid)`, `ask = max(niveau, best_ask)`. Le nombre de niveaux posés est
borné par la capacité (`cap` × levier).

Différence assumée avec le bot : il ajoute une paire toutes les 10 s et
remplit la grille progressivement ; ici tous les niveaux autorisés sont posés
d'un coup. Le régime permanent est le même.

Les valeurs par défaut sont dans `grid.h` ; toute option de la ligne de
commande les surcharge.

## Commande complète

```sh
bin/backtest data/extracted/xyz-XYZ100.l2 --strategy grid \
    --levels 24 --value 100.5 --delta 2.0 --gap 0.6 \
    --cap 0.80 --min-value 10.5
```

Les paramètres généraux (fichier, `--reverse`, frais, levier, `--poll`,
`--threshold`, `--from/--to`, etc.) ne sont pas listés ici : leurs défauts sont
dans `core/parameters.h` et `bin/backtest --help` les énumère.

## Paramètres

| option | défaut | rôle |
|---|---|---|
| `--levels N` | 24 | niveaux par côté |
| `--value $` | 100.5 | notionnel fixe de chaque ordre |
| `--delta P` | 2.0 | pas entre deux niveaux, en unités de prix |
| `--gap P` | 0.6 | écart bid/ask au centre de la grille, en unités de prix |
| `--cap F` | 0.80 | fraction max du portefeuille engagée par côté |
| `--min-value $` | 10.5 | notionnel minimum d'un ordre |
