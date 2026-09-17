# Backtest L2 — aide-mémoire

Moteur de backtest sur carnets d'ordres L2 (20 niveaux), en C, zéro dépendance
hors zlib. Ce fichier est le point d'entrée : compilation, commandes, où sont
les choses. Le détail du moteur est dans [`core/README.md`](core/README.md),
chaque stratégie a sa fiche dans [`strategies/`](strategies/).

## Arborescence

```
.
├── Makefile
├── README.md              ce fichier
├── core/                  moteur : format .l2, lecture, indicateurs, boucle
│   ├── README.md          documentation du core
│   ├── l2.h               le seul header du moteur
│   ├── parameters.h       défauts des paramètres généraux (frais, levier, cadence, EMA)
│   ├── book.c             mapping mémoire du .l2, dates, marche dans le carnet
│   ├── indicators.c       EMA / SMA / vol / microprix
│   ├── engine.c           la boucle : matching, comptabilité, diff, --reverse
│   ├── backtest.c         main de bin/backtest
│   ├── probabilities.c    main de bin/probas
│   ├── info.c             main de bin/info
│   └── extract.c          main de bin/extract (json.gz -> .l2)
├── strategies/            une stratégie = un dossier : <nom>.c, <nom>.h (défauts), <nom>.md
│   ├── strategy.h         interface StratDef + helpers partagés
│   ├── registry.c         LA liste des stratégies
│   ├── envelope/          envelope.c + envelope.h (défauts) + envelope.md
│   └── ...
├── bin/                   exécutables (générés, non versionnés)
├── build/                 objets (générés, non versionnés)
└── data/
    ├── raw/<MARCHE>/          les json.gz bruts, un sous-dossier par marché
    └── extracted/<MARCHE>.l2  les fichiers convertis (défaut : xyz-XYZ100.l2)
```

Pourquoi ces choix :

- **`bin/` séparé de `core/`** : les binaires sont des artefacts de build,
  ils ne se versionnent pas et se régénèrent en une commande. Les mélanger aux
  sources oblige à des règles de `.gitignore` par extension et rend le
  `make clean` fragile. Un dossier = une règle.
- **`data/raw/` et `data/extracted/`** : les json.gz dans `data/raw/` (un
  sous-dossier par marché, `bin/extract` parcourt récursivement), les `.l2`
  dans `data/extracted/`. Tout `data/` est ignoré par git : les `.l2` pèsent
  des Go. **`data/extracted/xyz-XYZ100.l2` est le fichier par défaut** de
  `bin/backtest`, `bin/info` et `bin/probas` : si le premier argument est
  absent ou commence par `--`, c'est lui qui est ouvert (constante
  `L2_DEFAULT` dans `core/l2.h`).
- **une stratégie = un fichier** : `backtest.c` ne connaît aucune stratégie.
  Il délègue le parsing des options au `StratDef` choisi. Ajouter une stratégie
  ne touche que `strategies/`.

## Compilation

```sh
make                # bin/backtest bin/info bin/probas bin/extract
make OMP=1          # extract multi-thread (OpenMP)
make DEBUG=1        # -O0 -g
make clean
```

Prérequis : gcc/clang, make, zlib (`apt install zlib1g-dev` ou, sous MSYS2
MINGW64, `pacman -S mingw-w64-x86_64-zlib`). Sous Windows utiliser le shell
**MINGW64** (64 bits) : un binaire 32 bits ne peut pas mapper un `.l2` de plus
de ~1 Go, et le header le signale à la compilation.

## Pipeline

```sh
# 1. json.gz -> .l2   (une fois par marché ; --jobs N si compilé avec OMP=1)
bin/extract data/raw/XYZ100 data/extracted/xyz-XYZ100.l2 --jobs 8
#   --market NOM : étiquette écrite dans l'en-tête (défaut : nom du fichier), relue par bin/info

# 2. vérifier ce que le moteur voit : période, cadence, trous, anomalies
bin/info data/extracted/xyz-XYZ100.l2 --gaps 20

# 3. étude de marché sans stratégie : proba de fill / retour à D% de l'EMA
bin/probas data/extracted/xyz-XYZ100.l2 --tau 300 --nd 40 --dmin 0.0002 --dmax 0.01 \
           --horizon 3600 --out study.csv

# 4. backtest
bin/backtest --list                          # stratégies disponibles
bin/backtest --help envelope                 # options d'une stratégie
bin/backtest --strategy envelope --tau 300 --levels 24 --spread 0.005 \
             --from 2026-08-21 --to -1d --equity equity.csv     # fichier par défaut
bin/backtest data/extracted/mkts-US500.l2 --strategy dumb          # autre fichier
```

## Options du moteur (communes à toutes les stratégies)

Les défauts sont dans `core/parameters.h` (modifier là change tous les runs).
Les fiches de `strategies/` ne listent que les paramètres propres à chaque
stratégie, plus ceux de l'EMA quand elle s'en sert.

| option | défaut | rôle |
|---|---|---|
| `book.l2` (1er argument) | `data/extracted/xyz-XYZ100.l2` | fichier de données ; optionnel |
| `--strategy NOM` | envelope | voir `--list` |
| `--reverse 0\|1` | 0 | 1 : chaque fill maker est remplacé par un **taker de même taille dans l'autre sens** (voir `core/README.md`) |
| `--tau S` | 300 | EMA continue, constante de temps en secondes |
| `--timeframe S --window N` | — | EMA "bougie" comme le bot live (`--wilder` : alpha=1/N, `--staircase` : sans mélange au mid) |
| `--fill through\|touch` | through | modèle de fill : through = le prix doit traverser (conservateur) |
| `--maker F` / `--taker F` | 0.00003 / 0.00009 | frais |
| `--lev F` | 20 | levier ; `--mmr F` marge de maintenance (défaut 0.5/lev, soit 2,5 % à 20x) |
| `--initial $` | 1000 | capital |
| `--poll S` | 10.1 | délai min entre deux re-quotes |
| `--threshold F` | 0.0001 | re-quote seulement si l'EMA a bougé de plus de F (ou après un fill). **Mettre 0 pour `touch`** |
| `--lat MS` | 0 | latence avant qu'un ordre posé soit actif |
| `--warmup S` | 5·tau | pas de trading avant (convergence de l'EMA) |
| `--max-jump F` | 0.10 | saut de mid jugé aberrant entre deux snapshots proches ; 0 = off |
| `--equity FILE` | — | courbe d'equity en CSV (une ligne par minute) |
| `--from B --to B` | bords | zone : `2026-08-21`, `2026-08-21T16:30`, `-3d`, `-12h`, `40%`, `#250000` |

Toute option non reconnue par le moteur est transmise à la stratégie ; une
option inconnue des deux fait échouer le run avec la liste des options valides.

## Lire les résultats

```
fichier        : data/extracted/xyz-XYZ100.l2
zone           : 2026-07-03T00:00:00 -> 2026-08-20T17:01:03 (48.71 jours) (2929673 snapshots sur 3078902, 95.2%)
strategie      : envelope
  levels=24 spread=0.0050 ... tp-spread=0.0100
parametres generaux
  fill=through reverse=0 lev=20 mmr=0.0250 initial=1000 maker=0.00003 taker=0.00009
  poll=10.1s threshold=0.0001 lat=0ms warmup=1500s max-jump=0.1
moyenne        : EMA continue tau=300s (demi-vie 208s)
requotes       : 19004   (390.2/jour)
fills          : 3829 (79/jour)
drawdown max   : 34.63%
rotation       : 1510.9x du capital (31.0x/jour)
equity finale  : 653.69$  (-34.63%, -95.86%/an)
PnL            : -346.31$
frais          : 45.34$
volume         : 1 510 944$
par M$ traite  : -229$ de PnL   dont -30$ de frais
```

- **par M$ traité** : le seul chiffre comparable entre stratégies et avec le
  réel, quel que soit le capital.
- Lignes conditionnelles, absentes quand tout va bien : `fills inverses`
  (mode reverse), `ordres refuses` (post-only), `snaps ecartes`, `echelle
  posee` (< 99 % : trous après les fills, volume sous-estimé),
  `LIQUIDATIONS`, `RUINE` — ces deux dernières rendent le run inexploitable,
  réduire `--lev` ou `--cap`.

## Ajouter une stratégie

1. `mkdir strategies/mienne && cp strategies/envelope/envelope.{c,h} strategies/mienne/` puis renommer, réécrire `quotes()`, mettre les défauts dans `mienne.h`.
2. Dans `strategies/registry.c` : `extern const StratDef strat_mienne;` et
   l'ajouter au tableau.
3. `strategies/mienne/mienne.md` : description, commande complète, paramètres.
4. `make`. Le Makefile ramasse `strategies/*/*.c` tout seul.
