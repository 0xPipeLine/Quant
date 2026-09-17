# volband — échelle dont la largeur suit la volatilité

## Description

L'enveloppe pose ses paliers à une distance **fixe** en pourcentage de l'EMA :
trop large en marché calme (rien ne se remplit), trop serrée en marché agité
(tout se remplit, on se charge). Ici les paliers sont posés en unités
d'écart-type attendu sur un horizon `horizon` :

```
band = σ · √horizon                 déplacement typique en `horizon` secondes
bids : ema · (1 − band · k_i)       k_i régulièrement espacés de k1 à k2
asks : ema · (1 + band · k_i)
```

En marché calme l'échelle se resserre et continue de tourner ; en marché
agité elle s'écarte toute seule. Le notionnel est **fixe** par palier
(`value`), pas réparti : le nombre de paliers effectivement posés est borné
par la capacité, ce qui limite l'inventaire sans skew. `min-band` évite une
échelle collée au mid quand la vol s'effondre. `inv 1` : comme l'enveloppe,
seul le côté du retour à la moyenne est coté.

Les valeurs par défaut sont dans `volband.h` ; toute option de la ligne de
commande les surcharge.

## Commande complète

```sh
bin/backtest data/extracted/xyz-XYZ100.l2 --strategy volband \
    --levels 12 --k1 0.5 --k2 3.0 --horizon 300 --value 50 --min-band 2 \
    --cap 0.85 --min-value 10.5 --inv 1 \
    --tau 300
```

Les paramètres généraux (fichier, `--reverse`, frais, levier, `--poll`,
`--threshold`, `--from/--to`, etc.) ne sont pas listés ici : leurs défauts sont
dans `core/parameters.h` et `bin/backtest --help` les énumère.

## Paramètres

| option | défaut | rôle |
|---|---|---|
| `--levels N` | 12 | paliers par côté |
| `--k1 F` | 0.5 | premier palier, en écarts-types |
| `--k2 F` | 3.0 | dernier palier, en écarts-types |
| `--horizon S` | 300 | horizon sur lequel la vol est projetée, en secondes |
| `--value $` | 50 | notionnel fixe de chaque palier |
| `--min-band B` | 2 | écart-type plancher, en bps |
| `--cap F` | 0.85 | fraction max du portefeuille engagée par côté |
| `--min-value $` | 10.5 | notionnel minimum d'un ordre |
| `--inv 0\|1` | 1 | 1 = ne coter que le côté du retour à la moyenne |

## Moyenne de référence

L'EMA est le centre de l'échelle. Deux modes exclusifs (le dernier parsé gagne) :

| option | défaut | rôle |
|---|---|---|
| `--tau S` | 300 | EMA continue, constante de temps en secondes |
| `--timeframe S` | 0 (off) | EMA "bougie" comme `MarketHL` : durée d'une bougie en secondes |
| `--window N` | 5 | EMA bougie : fenêtre N (alpha = 2/(N+1)) |
| `--wilder` | off | EMA bougie : alpha = 1/N (RMA / SMMA) |
| `--staircase` | off | EMA bougie : valeur en escalier, sans mélange au mid courant |

Variante bougie de la commande : remplacer `--tau 300` par
`--timeframe 300 --window 5 [--wilder] [--staircase]`.

La vol `σ` vient du moteur (EWMA des `r²/dt`, τ = `DEF_VOL_TAU` = 60 s dans `core/parameters.h`).
