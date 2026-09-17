# touch — coller au meilleur bid / ask

## Description

Le market making le plus brut : on rejoint le touch des deux côtés, en
`layers` couches espacées de `tick`, avec un notionnel `value` par couche. Pas
de moyenne, pas de modèle : on est toujours en tête de file et on ramasse tout
le flux qui traverse. C'est la stratégie qui génère le plus de volume maker par
dollar de capital, et celle qui souffre le plus de la sélection adverse :
chaque fill est par définition un mouvement qui vient de traverser notre prix.

Deux garde-fous :

- `max-inv` : au-delà de cette fraction du capital en position, on ne cote
  plus que le côté qui réduit ;
- `lean` : les deux prix sont décalés de `−lean × inventaire`, ce qui
  rapproche le côté qui réduit et éloigne l'autre (0 = désactivé).

**Cette stratégie doit suivre le touch** : lancer avec `--threshold 0` pour
que le moteur la rappelle à chaque `--poll` même si l'EMA n'a pas bougé. Le
modèle `--fill through` est essentiel ici ; `touch` donnerait des fills à
chaque snapshot.

Les valeurs par défaut sont dans `touch.h` ; toute option de la ligne de
commande les surcharge.

## Commande complète

```sh
bin/backtest data/extracted/xyz-XYZ100.l2 --strategy touch \
    --layers 3 --tick 0.1 --value 50 --cap 0.90 --min-value 10.5 \
    --max-inv 0.5 --lean 0
```

Les paramètres généraux (fichier, `--reverse`, frais, levier, `--poll`,
`--threshold`, `--from/--to`, etc.) ne sont pas listés ici : leurs défauts sont
dans `core/parameters.h` et `bin/backtest --help` les énumère.

## Paramètres

| option | défaut | rôle |
|---|---|---|
| `--layers N` | 3 | couches par côté |
| `--tick P` | 0.1 | espacement des couches, en unités de prix (mettre le tick du marché) |
| `--value $` | 50 | notionnel de chaque couche |
| `--cap F` | 0.90 | fraction max du portefeuille engagée par côté |
| `--min-value $` | 10.5 | notionnel minimum d'un ordre |
| `--max-inv F` | 0.5 | inventaire max, en fraction du capital, avant de ne coter que le côté qui réduit |
| `--lean F` | 0 | décalage des prix en fraction du mid par unité d'inventaire (ex. 0.0005) |

**Lancer avec `--threshold 0`** : la stratégie doit suivre le touch à chaque poll, pas seulement quand l'EMA bouge (`--threshold` est un paramètre général, voir `core/parameters.h`).
