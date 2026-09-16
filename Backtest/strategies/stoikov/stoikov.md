# stoikov — Avellaneda-Stoikov, version relative

## Description

Market making d'inventaire, d'après Avellaneda & Stoikov (2008), en unités
relatives. Deux idées :

1. **prix de réservation** : le mid "pour nous" dépend de l'inventaire
   `q = position / capital` :
   `r = mid · (1 − q · gamma · σ²T)`.
   Long → `r` sous le mid : on vend plus facilement, on achète moins. C'est ce
   qui recentre l'inventaire sans jamais payer de taker.
2. **demi-spread optimal** autour de `r` :
   `δ = gamma·σ²T / 2 + ln(1 + gamma/k) / gamma`.
   Le premier terme paie le risque d'inventaire (plus la vol est haute, plus on
   s'écarte), le second est la prime de liquidité (`k` grand = flux dense = on
   peut serrer). `δ` est borné dans `[min-spread, max-spread]`.

`σ` est la vol du moteur (écart-type des rendements par √s), `T` l'horizon en
secondes : `σ²T` est une variance relative sur l'horizon. Les couches
supplémentaires (`layers`, `step`) s'écartent de `δ·(1 + i·step)`.

Contrairement à l'enveloppe, aucun pari sur un retour à une moyenne : le
spread se serre, l'inventaire pilote les prix. Volume élevé, inventaire bas,
très sensible à `gamma`.

## Commande complète

```sh
bin/backtest data/extracted/mkts-US500.l2 --strategy stoikov --reverse 0 \
    --gamma 100 --k 1000 --horizon 300 --value 50 --layers 2 --step 1 \
    --min-spread 2 --max-spread 30 --cap 0.90 --min-value 10.5 \
    --tau 300 --poll 10.1 --threshold 0.0001 --fill through \
    --maker 0.00003 --taker 0.00009 --lev 1 --initial 1000 \
    --from 2026-08-21 --to -1d
```

## Paramètres

| option | défaut | rôle |
|---|---|---|
| `--gamma F` | 100 | aversion au risque d'inventaire ; grand = prix très penchés par l'inventaire, spread plus large |
| `--k F` | 1000 | intensité du flux d'ordres ; grand = spread serré (avec gamma=100 : 9,5 bps de demi-spread) |
| `--horizon S` | 300 | horizon de risque, en secondes |
| `--value $` | 50 | notionnel de chaque couche |
| `--layers N` | 2 | couches par côté |
| `--step F` | 1 | écart entre couches, en multiples du demi-spread |
| `--min-spread B` | 2 | demi-spread plancher, en bps (doit couvrir ≥ 2 × maker_fee) |
| `--max-spread B` | 30 | demi-spread plafond, en bps |
| `--cap F` | 0.90 | fraction max du portefeuille engagée par côté |
| `--min-value $` | 10.5 | notionnel minimum d'un ordre |

Réglage : commencer par `--k` pour obtenir le demi-spread voulu au repos
(`ln(1+gamma/k)/gamma`), puis `--gamma` pour la force du recentrage.
