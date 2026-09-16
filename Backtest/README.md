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
│   ├── book.c             mapping mémoire du .l2, dates, marche dans le carnet
│   ├── indicators.c       EMA / SMA / vol / microprix
│   ├── engine.c           la boucle : matching, comptabilité, diff, --reverse
│   ├── backtest.c         main de bin/backtest
│   ├── probabilities.c    main de bin/probas
│   ├── info.c             main de bin/info
│   └── extract.c          main de bin/extract (json.gz -> .l2)
├── strategies/            une stratégie = un dossier avec son .c et son .md
│   ├── strategy.h         interface StratDef + helpers partagés
│   ├── registry.c         LA liste des stratégies
│   ├── envelope/          envelope.c + envelope.md
│   └── ...
├── bin/                   exécutables (générés, non versionnés)
├── build/                 objets (générés, non versionnés)
└── data/
    ├── raw/<MARCHE>/      les json.gz bruts, un sous-dossier par marché
    └── l2/<MARCHE>.l2     les fichiers convertis, un par marché
```

Pourquoi ces choix :

- **`bin/` séparé de `core/`** : les binaires sont des artefacts de build,
  ils ne se versionnent pas et se régénèrent en une commande. Les mélanger aux
  sources oblige à des règles de `.gitignore` par extension et rend le
  `make clean` fragile. Un dossier = une règle.
- **`data/raw/` et `data/extracted/`** : les json.gz existants vont dans `data/raw/`
  (un sous-dossier par marché, `bin/extract` parcourt récursivement). Les `.l2`
  vont dans `data/extracted/`, nommés par marché. Tout `data/` est ignoré par git :
  les `.l2` pèsent des Go. Si tu préfères ne pas déplacer les json.gz, laisse-les
  où ils sont et crée seulement `data/extracted/` — rien dans le code ne dépend du
  chemin.
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
bin/extract data/raw/mkts-US500 data/extracted/mkts-US500.l2 --market mkts:mkts-US500 --jobs 8

# 2. vérifier ce que le moteur voit : période, cadence, trous, anomalies
bin/info data/extracted/mkts-US500.l2 --gaps 20

# 3. étude de marché sans stratégie : proba de fill / retour à D% de l'EMA
bin/probas data/extracted/mkts-US500.l2 --tau 300 --nd 40 --dmin 0.0002 --dmax 0.01 \
           --horizon 3600 --out study.csv

# 4. backtest
bin/backtest --list                          # stratégies disponibles
bin/backtest --help envelope                 # options d'une stratégie
bin/backtest data/extracted/mkts-US500.l2 --strategy envelope --tau 300 \
             --levels 24 --spread 0.005 --skew 2 --inv 1 \
             --from 2026-08-21 --to -1d --equity equity.csv
```

## Options du moteur (communes à toutes les stratégies)

| option                        | défaut           | rôle                                                                                                                 |
| ----------------------------- | ----------------- | --------------------------------------------------------------------------------------------------------------------- |
| `--strategy NOM`            | envelope          | voir`--list`                                                                                                        |
| `--reverse 0\|1`             | 0                 | 1 : chaque fill maker est remplacé par un**taker de même taille dans l'autre sens** (voir `core/README.md`) |
| `--tau S`                   | 300               | EMA continue, constante de temps en secondes                                                                          |
| `--timeframe S --window N`  | —                | EMA "bougie" comme le bot live (`--wilder` : alpha=1/N, `--staircase` : sans mélange au mid)                     |
| `--fill through\|touch`      | through           | modèle de fill : through = le prix doit traverser (conservateur)                                                     |
| `--maker F` / `--taker F` | 0.00003 / 0.00009 | frais                                                                                                                 |
| `--lev F`                   | 1                 | levier ;`--mmr F` marge de maintenance (défaut 0.5/lev)                                                            |
| `--initial $`               | 1000              | capital                                                                                                               |
| `--poll S`                  | 10.1              | délai min entre deux re-quotes                                                                                       |
| `--threshold F`             | 0.0001            | re-quote seulement si l'EMA a bougé de plus de F (ou après un fill).**Mettre 0 pour `touch`**               |
| `--lat MS`                  | 0                 | latence avant qu'un ordre posé soit actif                                                                            |
| `--warmup S`                | 5·tau            | pas de trading avant (convergence de l'EMA)                                                                           |
| `--max-jump F`              | 0.10              | saut de mid jugé aberrant entre deux snapshots proches ; 0 = off                                                     |
| `--equity FILE`             | —                | courbe d'equity en CSV (une ligne par minute)                                                                         |
| `--from B --to B`           | bords             | zone :`2026-08-21`, `2026-08-21T16:30`, `-3d`, `-12h`, `40%`, `#250000`                                   |

Toute option non reconnue par le moteur est transmise à la stratégie ; une
option inconnue des deux fait échouer le run avec la liste des options valides.

## Lire les résultats

- **PnL / volume (bps)** et **par M$ traité** : les deux seuls chiffres
  comparables entre stratégies et avec le réel, quel que soit le capital.
- **échelle posée** : < 100 % signifie que le moteur laisse des trous après
  les fills ; volume sous-estimé par rapport au live.
- **fills inverses** (mode reverse) : nombre de takers déclenchés.
- **LIQUIDATIONS / RUINE** : le run n'est pas exploitable, réduire `--lev` ou
  `--cap`.

## Ajouter une stratégie

1. `mkdir strategies/mienne && cp strategies/envelope/envelope.c strategies/mienne/mienne.c`, réécrire `quotes()`.
2. Dans `strategies/registry.c` : `extern const StratDef strat_mienne;` et
   l'ajouter au tableau.
3. `strategies/mienne/mienne.md` : description, commande complète, paramètres.
4. `make`. Le Makefile ramasse `strategies/*/*.c` tout seul.
