# envelope — échelle géométrique autour de l'EMA, avec take profit optionnel

## Description

Une échelle de `levels` ordres par côté, centrée sur l'EMA. Les paliers
s'écartent de la moyenne d'un pas de base multiplié par `alpha^i` ; `spread`
fixe l'écart total entre le premier et le dernier ordre, `offset` décale le
premier. Les tailles sont géométriques : l'ordre le plus loin de la moyenne est
`skew` fois plus gros que le plus proche (on charge plus quand le prix s'est
plus éloigné). La capacité de chaque côté est déduite du levier et de la
position courante, ce qui fait décroître les tailles à mesure qu'on se charge.
Avec `inv 1`, seul le côté du retour à la moyenne est coté : mid au-dessus de
l'EMA → bids uniquement. C'est la transposition de la classe Python du bot.

**Take profit** (`tp 1`) : un ordre maker de la taille entière de la position,
placé à `tp-spread` du prix moyen d'entrée (vente au-dessus pour un long,
achat en dessous pour un court). Le point délicat est le partage de capacité :
sans TP, le côté qui réduit dispose de `power + |position|` (fermeture +
retournement). Avec TP, la partie fermeture (`|position|`) est réservée à
l'ordre de TP et l'échelle ne reçoit que `power`. L'exposition maximale est
inchangée ; on décide seulement où se place la partie qui solde. Sans ce
partage, TP et échelle vendraient la même position et on serait short du double.

## Commande complète

```sh
bin/backtest data/extracted/mkts-US500.l2 --strategy envelope --reverse 0 \
    --levels 24 --spread 0.005 --offset 0.0001 --alpha 1 --skew 2 \
    --cap 0.85 --min-value 10.5 --inv 1 --tp 0 --tp-spread 0.01 \
    --tau 300 --poll 10.1 --threshold 0.0001 --fill through \
    --maker 0.00003 --taker 0.00009 --lev 1 --initial 1000 \
    --from 2026-08-21 --to -1d --equity equity.csv
```

## Paramètres

| option | défaut | rôle |
|---|---|---|
| `--levels N` | 24 | ordres par côté |
| `--spread F` | 0.005 | écart total premier → dernier ordre, en fraction de l'EMA (0.005 = 0,5 %) |
| `--offset F` | 0.0001 | décalage du premier ordre, en fraction d'un pas de base |
| `--alpha F` | 1 | espacement géométrique : 1 = paliers réguliers, > 1 = de plus en plus espacés |
| `--skew F` | 2 | taille du dernier ordre / taille du premier |
| `--cap F` | 0.85 | fraction max du portefeuille (× levier) engagée par côté |
| `--min-value $` | 10.5 | notionnel minimum d'un ordre ; en dessous, l'ordre est sauté |
| `--inv 0\|1` | 1 | 1 = ne coter que le côté du retour à la moyenne |
| `--tp 0\|1` | 0 | 1 = poser un take profit de la taille de la position |
| `--tp-spread F` | 0.01 | distance du TP au prix d'entrée, en fraction (0.01 = 1 %) |

En `--reverse 1` (moteur), chaque fill de l'échelle devient un taker inverse ;
la stratégie voit la position miroir, donc son TP "vend" une position qu'elle
croit longue et le moteur exécute un achat taker qui réduit le short réel.
Retour à la moyenne → momentum, logique inchangée.
