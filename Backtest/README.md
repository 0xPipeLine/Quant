# l2engine

Backtest de stratégies de market making sur carnets L2, en C.

```
src/extract.c             *.json.gz  ->  book.l2     binaire `extract`
src/info.c                inspection d'un .l2         binaire `info`
include/l2.h              format, types, API
src/book.c                ouverture du .l2 (mmap) + lecture du carnet
src/indicators.c          EMA / SMA / volatilité, incrémentaux
src/engine.c              matching, comptabilité, boucle principale
src/strategy_envelope.c   la stratégie enveloppe
src/backtest.c            binaire `backtest`
src/probabilities.c       binaire `probas`
l2_convert.py             même conversion en Python (secours / bricolage)
```

## Build

    make                # les trois binaires
    make OMP=1          # extracteur parallélisé (si plusieurs cœurs)

`gcc`, `-lm`, plus `zlib` pour `extract` seulement :

- MSYS2 / MinGW : `pacman -S mingw-w64-x86_64-zlib`
- Debian / Ubuntu : `apt install zlib1g-dev`

Le `Makefile` marche que les sources soient rangées dans `src/` + `include/` ou
posées à plat dans un seul dossier.

Le `mmap` a un équivalent Win32 déjà câblé dans `book.c`, et les `printf`
utilisent `PRId64` (`inttypes.h`) : le `printf` d'origine de MinGW ne connaît ni
`%lld` ni `%zu` et produirait des avertissements et des sorties fausses.

---

## 1. Le format `.l2`

```
[ en-tête 64 o ][ snapshot 0 : 656 o ][ snapshot 1 ] ... [ snapshot N-1 ]
```

Un snapshot :

```c
int64  ts          nanosecondes UTC
int64  height
double bid_px[20], bid_sz[20]      décroissant, 0 = niveau absent
double ask_px[20], ask_sz[20]      croissant
```

**Le point important** : cet enregistrement est *exactement* la struct `Snap`
du C, octet pour octet. Ouvrir un fichier revient donc à le mapper en mémoire
et à caster le pointeur — pas de parsing, pas de copie, pas de conversion de
type. `b.s[i].bid_px[0]` lit directement dans le fichier. L'OS charge les pages
à la demande : un fichier de 20 Go s'ouvre instantanément et ne consomme que ce
qui est réellement parcouru.

Coût : 656 o/snapshot, soit ~30 Mo par jour de données, ~11 Go par an. Un scan
complet d'une année prend quelques secondes, ce qui est le budget qui rend les
balayages de paramètres possibles.

Le fichier se relit depuis Python en deux lignes, `l2_convert.py` fournit le
dtype :

```python
from l2_convert import record_dtype
import numpy as np
a = np.memmap("book.l2", dtype=record_dtype(20), offset=64, mode="r")
a["bid_px"][:, 0].mean()
```

## 2. Extraction

    ./extract l2-data-mkts-US500 book.l2 [--jobs 8]

Parcourt récursivement l'arborescence, trie les fichiers par hauteur, rejette
les doublons (hauteur non croissante) et les carnets vides ou croisés, puis
écrit le binaire. Le rapport final donne la cadence moyenne et le **gap
maximum** — à regarder en premier : un trou de plusieurs heures signifie session
fermée ou données manquantes, et toute statistique qui l'ignore est fausse.

Ce qui le rend rapide : un parseur dédié au schéma (on ne cherche que les clés
connues, dans l'ordre connu), une conversion décimale maison au lieu de
`strtod`, aucune allocation par snapshot (on écrit directement dans un buffer
qui est déjà le format final), et la décompression des fichiers en parallèle
avec `make OMP=1`. L'écriture reste séquentielle pour garantir l'ordre
chronologique — la sortie est **identique octet pour octet** quel que soit le
nombre de threads, et identique à celle de `l2_convert.py`.

`l2_convert.py` est conservé : c'est le même format, pratique pour bricoler un
champ supplémentaire sans recompiler. Il est simplement ~10× plus lent.

## 3. Backtest

    ./backtest book.l2 --tau 300 --levels 24 --spread 0.005 --skew 2 
               --poll 10.1 --threshold 0.0001 --fill through --equity equity.csv

### Ce qui se passe à chaque snapshot

1. **Indicateurs.** `Ctx` est mis à jour en O(1) : mid, microprix, spread,
   déséquilibre du carnet, EMA, volatilité.
2. **Matching.** Les ordres déjà posés sont confrontés au nouveau carnet. Ceux
   qui sont touchés deviennent des fills.
3. **Comptabilité.** `equity = cash + size × mid`. C'est le seul invariant du
   moteur : le PnL réalisé n'est pas une variable séparée qui pourrait se
   désynchroniser, c'est la trajectoire du cash.
4. **Stratégie.** Elle est appelée et renvoie l'échelle d'ordres qu'elle veut.
5. **Diff.** On compare cette échelle aux ordres déjà posés. Prix identique →
   l'ordre est conservé tel quel, il garde son ancienneté donc sa place dans la
   file. Prix différent → annulé, reposé, et il repart pour un tour de latence.

L'ordre 2-avant-4 est ce qui interdit le look-ahead : la stratégie ne peut
jamais réagir à un carnet contre lequel ses ordres n'ont pas déjà été
confrontés.

### Le modèle de fill

Tu as des snapshots de carnet, pas la tape des trades : aucun modèle n'est
exact. On encadre donc.

| `--fill`            | Un achat à`p` est rempli si | Nature                                                                                |
| --------------------- | ------------------------------ | ------------------------------------------------------------------------------------- |
| `through` (défaut) | `ask ≤ p`                   | conservateur — le marché a traversé ton prix, quelqu'un a forcément dû te servir |
| `touch`             | `bid ≤ p`                   | optimiste — suppose que tu es en tête de file d'attente                             |

**Lance toujours les deux.** La réalité est entre. Si une stratégie n'est
profitable qu'en `touch`, elle n'existe pas.

### Choisir une zone du dataset

`--from` et `--to` acceptent, indifféremment :

| forme           | exemple                                          | sens                             |
| --------------- | ------------------------------------------------ | -------------------------------- |
| date            | `--from 2026-08-21`                            | minuit UTC ce jour-là           |
| date + heure    | `--to 2026-08-21T16:30`                        | les secondes sont optionnelles   |
| durée relative | `--from -3d`, `--from -12h`, `--from -90m` | avant la**fin** du fichier |
| pourcentage     | `--from 50%`                                   | moitié du fichier               |
| index           | `--from '#250000'`                             | numéro de snapshot              |

```bash
./backtest book.l2 --from 2026-08-01 --to 2026-09-01     # un mois
./backtest book.l2 --from -7d                            # 7 derniers jours
./backtest book.l2 --to 70%                              # in-sample
./backtest book.l2 --from 70%                            # out-of-sample
```

Techniquement, **seule la zone demandée est mappée**. Les bornes sont trouvées
par une recherche binaire qui ne lit que quelques timestamps de 8 octets dans le
fichier, puis `mmap` ne couvre que la fenêtre utile — alignée sur la granularité
d'allocation du système. Sur un fichier de 2 Go, tester un mois ne consomme donc
que l'espace d'adressage de ce mois. C'est ce qui permet de travailler même avec
un binaire 32 bits. La zone effectivement retenue est affichée en tête de sortie
avec ses vraies dates. Les mêmes options existent sur `probas`.

Une conséquence à connaître : la moyenne repart de zéro au début de la zone.
Le moteur ne trade donc pas pendant `5 × tau` par défaut, le temps qu'elle
converge (`--warmup` pour forcer une autre valeur). Sur une zone très courte,
vérifie que ce warmup n'en mange pas la moitié.

Pour tester la stabilité, ce qui compte plus que le résultat global :

```bash
for m in 01 02 03 04 05 06; do
  ./backtest book.l2 --from 2026-$m-01 --to 2026-$((m+1))-01 --timeframe 300 --window 5
done
```

Une stratégie qui ne gagne que sur un mois sur six n'est pas une stratégie.

### Options qui changent vraiment les résultats

- **La moyenne de référence**, deux modes exclusifs (voir §7) :
  `--tau 740` (EMA continue) ou `--timeframe 300 --window 5` (EMA bougie,
  identique au bot live). Le mode choisi est rappelé en tête de sortie.
- `--poll` / `--threshold` : cadence de re-quote, équivalents de `POLL_DELAY` et
  `THRESHOLD` du bot live. Sans eux le backtest repose des ordres 60 000 fois
  par jour et surestime massivement les fills.
- `--lat <ms>` : un ordre n'est actif qu'au premier snapshot après ce délai.
- `--maker` / `--taker` séparés. Les fills payent du maker, la clôture finale de
  position paye du taker et se fait *dans le carnet* (`snap_walk`), donc au coût
  réel de sortie, pas au mid.

## 4. La stratégie enveloppe

`src/strategy_envelope.c` est la transposition de ta classe Python. Mêmes
paramètres, même géométrie.

**Prix** — les paliers s'écartent de la moyenne d'un pas `raw = SPREAD·ema/geo`,
multiplié par `alpha^i`. `alpha = 1` donne des paliers régulièrement espacés,
`alpha > 1` de plus en plus espacés. `SPREAD` fixe l'écart total entre le
premier et le dernier ordre, `OFFSET` décale le premier palier de la moyenne.

**Tailles** — géométriques de raison `beta = SKEW^(1/(L-1))`. L'ordre le plus
éloigné de la moyenne est `SKEW` fois plus gros que le plus proche.

**Capacité** — `longable = (equity·levier − position)·CAP/mid`, et
symétriquement pour `shortable`. Une position longue réduit l'un et augmente
l'autre, donc les ordres rapetissent automatiquement à mesure qu'on se charge.

**INV** — ne quoter que le côté vers lequel le prix doit revenir. Si le mid est
au-dessus de la moyenne, seuls les bids (sous la moyenne) sont posés : on ne
parie pas contre le retour à la moyenne.

**Garde-fous** — post-only (un ordre qui croiserait le carnet est rejeté, et un
`assert` du moteur arrête net une stratégie qui en produirait un), et
`MIN_VALUE` contre la fragmentation.

## 5. Écrire une autre stratégie

Copier `strategy_envelope.c` et réécrire `quotes()` :

```c
static int quotes(void *st, const Ctx *c, const Portfolio *pf,
                  Quote *out, int max)
{
    Mienne *m = st;
    int n = 0;
    double band = c->ema.v * m->width;
    double sz   = pf->equity * pf->leverage * 0.5 / c->mid;

    out[n].px = c->ema.v - band; out[n].sz =  sz; n++;   // sz > 0 -> bid
    out[n].px = c->ema.v + band; out[n].sz = -sz; n++;   // sz < 0 -> ask
    return n;
}

Strat s = { "mienne", &state, quotes };
Result r = engine_run(&book, &s, &cfg, tau);
```

Disponible dans `Ctx` : `mid`, `bid`, `ask`, `micro` (le mid pondéré par les
tailles au touch — meilleur prédicteur du prochain mid), `spread`, `imb`
(déséquilibre du carnet), `vol` (écart-type par √seconde), `ema`, `dt`, et le
carnet complet via `c->s`, avec `snap_walk()` pour le coût réel d'un ordre
marché et `snap_depth()` pour la liquidité disponible à D% du mid.

## 6. Probabilités

    ./probas book.l2 --tau 300 --nd 40 --dmin 0.0002 --dmax 0.01 
             --horizon 3600 --out study.csv --dump episodes.csv

Ici on ne simule pas une stratégie, on **observe le marché**. Pour chaque
distance D d'une grille logarithmique et chaque côté, on suit un ordre passif
posé en permanence à `ref·(1−D)` (achat) ou `ref·(1+D)` (vente), avec `ref` =
EMA, ou SMA avec `--sma`. Cycle de vie d'un épisode :

```
armé --(fill)--> ouvert --(retour à la moyenne | stop | horizon)--> agrégé
```

`study.csv`, par côté et par distance :

| colonne               | lecture                                                        |
| --------------------- | -------------------------------------------------------------- |
| `fills_per_day`     | à quelle fréquence ce palier travaille                       |
| `p_return`          | probabilité que le prix revienne à la moyenne après le fill |
| `p_timeout`         | épisodes non résolus dans l'horizon — les cas gênants      |
| `mean_ttr_s`        | temps de retour moyen, donc le capital immobilisé             |
| `pnl_if_return_bps` | ce que rapporte un aller-retour réussi                        |
| `mean_mae_bps`      | jusqu'où ça part contre toi entre-temps                      |

Le `mean_mae_bps` est la colonne qui dimensionne la grille : si le MAE moyen
après un fill à 5 bps est de 25 bps, une grille de 3 paliers espacés de 5 bps
est sous-capitalisée, elle sera pleine avant le retour.

`episodes.csv` contient une ligne par épisode, pour tes quantiles et un
bootstrap par blocs.

### Trois précautions câblées

Sans elles, les chiffres sont faux tout en ayant l'air précis.

1. **Pas de chevauchement** : un seul épisode ouvert à la fois par (côté, D).
   Sinon 10 000 « observations » ne sont que 40 événements indépendants qui se
   recouvrent, et les probabilités semblent bien plus solides qu'elles ne sont.
2. **Pas de look-ahead** : le niveau vient de la moyenne du snapshot
   *précédent*, le fill est testé sur le snapshot courant. Ce décalage d'un pas
   suffit à transformer une stratégie perdante en gagnante.
3. **Censure** : les épisodes non résolus à l'horizon sont comptés, pas jetés.
   Ce sont justement les pires.

Pour des intervalles de confiance, un bootstrap i.i.d. ment (autocorrélation) :
utiliser un bootstrap par blocs de quelques heures sur `episodes.csv`.

---

## 7. Quand ça ne marche pas

Premier réflexe, toujours :

```bash
./info book.l2
```

Il dit ce que le moteur voit réellement : nombre de snapshots, période
couverte, cadence, prix min/max, carnets vides ou croisés, timestamps en
arrière, et la liste des plus gros trous. Il affiche aussi **si le binaire est
en 32 ou 64 bits**, ce qui est la première chose à vérifier sur un gros fichier.

Quelques cas et ce qu'ils signifient :

| Symptôme                                                          | Cause                                                                                                                                                               |
| ------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MapViewOfFile a echoue` + `binaire 32 bits`                   | un fichier de plus de ~1 Go ne peut pas être mappé en 32 bits. Compiler dans le shell**MINGW64** (pas MINGW32 ni MSYS), ou restreindre avec `--from/--to` |
| `n'est pas un fichier .l2`                                       | mauvais fichier, ou conversion qui n'est jamais arrivée au bout                                                                                                    |
| `l'en-tête annonce N snapshots, le fichier n'en contient que M` | conversion interrompue : l'en-tête est réécrit à la toute fin                                                                                                   |
| `écrit avec levels=X, moteur compilé avec LEVELS=Y`            | reconvertir, ou recompiler avec le même`LEVELS`                                                                                                                  |
| `la borne '…' est hors du fichier`                              | la date demandée n'existe pas dans les données ;`info` donne l'intervalle réel                                                                                 |

**Sur l'absence totale d'affichage** : sous MSYS2, la sortie passe par un
pseudo-terminal, que la libc Windows considère comme un tuyau et bufferise par
blocs de 4 Ko. Si le programme meurt, tout l'affichage en attente est perdu et
on ne voit rien du tout, même les messages d'erreur. Les binaires forcent donc
`setvbuf(..., _IONBF, 0)` sur `stdout` et `stderr` dès la première ligne de
`main` : ce qui est écrit s'affiche immédiatement, y compris juste avant un
plantage. Garde ce réflexe si tu ajoutes un outil.

## 8. La moyenne de référence

Trois modes, exclusifs.

### `--timeframe 300 --window 5` (défaut recommandé)

Reproduit exactement ton `MarketHL.ema_close()` :

```
ema_mémorisée  <- α·mid + (1-α)·ema_mémorisée     tous les `timeframe` secondes
référence lue  =  α·mid_courant + (1-α)·ema_mémorisée
```

avec `α = 2/(window+1)` (`--wilder` pour `1/N`). Le point important, et qui n'a
rien d'anodin : la valeur **lue** remélange le mid courant. Le centre de ta
grille suit donc le mid en permanence, à hauteur de α — avec `window=5`, un
tiers. Ce n'est pas une moyenne lente, c'est un mid amorti.

Conséquence directe : la distance entre le mid et le centre de la bande vaut
`(1-α)·(mid − ema_mémorisée)`, soit deux tiers seulement de l'écart apparent.
Tes paliers sont donc bien plus proches du prix que ce que suggère le
paramétrage, et tu te fais remplir beaucoup plus souvent.

### `--timeframe 300 --window 5 --staircase`

EMA de clôtures classique : elle ne bouge qu'à la fin de chaque bougie et reste
figée entre deux. C'est ce que fait la plupart des bibliothèques d'indicateurs
— mais **ce n'est pas ce que fait ton bot**. Utile uniquement pour comparer.

### `--tau 740`

EMA à décroissance temporelle, `α = 1 − exp(−Δt/τ)`, mise à jour à chaque
snapshot. C'est la version « propre » : elle s'adapte toute seule aux
intervalles irréguliers et aux trous de données, là où une EMA indexée sur des
pas fixes saute d'un coup après une interruption. À tester si tu envisages de
changer le bot.

Conversion entre les deux familles, en égalisant les coefficients sur un pas de
durée `T` : `τ = −T / ln(1 − α)`. Attention, `WINDOW=5` sur du `5m` ne fait pas
τ = 300 s mais **τ = 740 s**.

### Ça change beaucoup

Même config, 20 jours, même données :

```
--staircase    13 111 fills   320 k$    852 requotes/jour
défaut         19 912 fills   481 k$   1 914 requotes/jour
```

50 % de volume en plus et deux fois plus de re-quotes, uniquement à cause de la
formule de la moyenne. Le mode actif est rappelé en tête de chaque sortie.
