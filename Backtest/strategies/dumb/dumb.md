# dumb — pile d'ordres au touch, extrémités recyclées

## Description

Transposition de la classe Python `Dumb`. À chaque cycle :

- s'il reste au moins `min-value` de capacité **des deux côtés**, on **pose**
  une paire bid/ask de `min-value` chacune, au plus près du mid sans croiser le
  touch : `bid = min(best_bid, mid·(1 − gamma·maker_fee))`, symétrique pour
  l'ask ;
- sinon on **déplace** le bid le plus bas et l'ask le plus haut vers ces mêmes
  prix (le `modify()` du bot).

Les ordres déjà en carnet sont conservés. Il en résulte une pile de petits
ordres qui s'accumule près du mid jusqu'à saturer la capacité, puis dont les
extrémités sont recyclées en permanence vers le touch. Beaucoup de petits fills
maker, rotation très élevée, aucun modèle de prix.

C'est la première stratégie incrémentale : elle utilise `rest` (les ordres
encore en carnet) et renvoie `rest` modifié. Le bot bouge les ordres par
modify ; ici le moteur voit un prix différent et fait cancel/replace.

## Commande complète

```sh
bin/backtest data/extracted/mkts-US500.l2 --strategy dumb --reverse 0 \
    --gamma 2 --cap 0.90 --min-value 10.5 \
    --tau 300 --poll 10.1 --threshold 0.0001 --fill through \
    --maker 0.00003 --taker 0.00009 --lev 1 --initial 1000 \
    --from 2026-08-21 --to -1d
```

## Paramètres

| option | défaut | rôle |
|---|---|---|
| `--gamma F` | 2 | distance au mid = `gamma × maker_fee` (2 → 0,6 bp avec 3 bps de frais) |
| `--cap F` | 0.90 | fraction max du portefeuille engagée par côté |
| `--min-value $` | 10.5 | notionnel de chaque ordre posé (la taille est calculée sur le prix pour que le notionnel soit ≥ `min-value`) |

`--poll` compte ici : c'est la cadence à laquelle une paire est ajoutée
(10,1 s dans le bot).
