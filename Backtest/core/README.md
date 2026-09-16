# core/ — le moteur

Tout ce qui ne dépend d'aucune stratégie. Un seul header, `l2.h`, découpé
en sept sections qui suivent l'ordre de ce document.

## 1. Format `.l2`

```
[ L2Header : 64 octets ][ Snap #0 : 656 o ][ Snap #1 ] ... [ Snap #N-1 ]
```

`Snap` **est** l'enregistrement sur disque, octet pour octet : `ts` (ns UTC),
`height`, puis 4 tableaux de 20 doubles (`bid_px` décroissant, `bid_sz`,
`ask_px` croissant, `ask_sz`). Un prix à 0 = niveau absent. Ouvrir un `.l2`
revient à mapper le fichier et caster : pas de parsing, pas de copie.

`L2Header` porte `magic="L2v1"`, `levels` (doit valoir `LEVELS`), `rec_size`
(doit valoir `sizeof(Snap)`), `n_rec`, `t_start`, `t_end`, `market`. Si
`LEVELS` change dans `l2.h`, les données sont à reconvertir ; `book_open`
refuse le fichier et le dit.

Lecture en Python :
```python
import numpy as np
dt = np.dtype([('ts','<i8'),('height','<i8'),('bid_px','<f8',20),('bid_sz','<f8',20),
               ('ask_px','<f8',20),('ask_sz','<f8',20)])
snaps = np.memmap('data/extracted/mkts-US500.l2', dtype=dt, mode='r', offset=64)
```

## 2. Lecture — `book.c`

- `book_open_range(&b, path, from, to)` : lit l'en-tête, trouve les bornes
  par recherche binaire sur le fichier (lectures de 8 octets), puis mappe
  **uniquement** la fenêtre. Indispensable sur des fichiers de plusieurs Go.
- `book_open` = tout le fichier. `book_close` démappe.
- `book_seek(&b, ts)` : premier index avec `ts >= t`.
- `book_slice(&b, i0, i1)` : vue sans copie sur une tranche.
- `l2_fmt_time(ts, buf, n)` : ISO 8601.
- `snap_walk(s, side, qty, &filled)` : VWAP d'un ordre marché qui mange le
  carnet, s'arrête quand la liquidité visible est épuisée (`filled` < qty).
- `snap_walk_full(s, side, qty)` : idem, mais ce qui dépasse les 20 niveaux
  est exécuté au prix du **dernier niveau visible**. C'est la convention du
  mode `--reverse`.
- `snap_depth(s, side, frac)` : liquidité cumulée à moins de `frac` du mid.

Formes de `from` / `to` : `2026-08-21`, `2026-08-21T16:30[:ss]`, `-3d`,
`-12h`, `-90m`, `40%`, `#250000`, ou ns brut.

## 3. Indicateurs — `indicators.c`

Tout est indexé sur le **temps**, pas sur le nombre de snapshots (cadence
irrégulière, trous).

- `Ema` : `alpha = 1 - exp(-dt/tau)`.
- `Sma` : fenêtre temporelle glissante (ring buffer).
- `Ctx` : l'état vu par une stratégie à chaque snapshot — `bid`, `ask`,
  `mid`, `spread`, `micro` (microprix), `imb` (imbalance au touch), `vol`
  (écart-type des log-rendements par √s, EWMA de τ=60 s), `ema`.
- Deux modes pour `ema` : continu (`--tau`) ou "bougie" (`--timeframe`,
  `--window`) qui reproduit `MarketHL.ema_close()` du bot, avec le même retard
  d'une bougie. `ema_tau_of()` donne le τ équivalent (N=5 sur 5 min → 740 s,
  pas 300).

## 4. Portefeuille

```c
typedef struct { double cash, size, equity, entry, fees, volume,
                 leverage, maker_fee, taker_fee; long fills;
                 double eq_max, dd_max; } Portfolio;
```
Le seul invariant : `equity == cash + size * mid`. Toute la comptabilité passe
par `do_fill()` dans `engine.c`, et uniquement par là.

## 5. Interface d'une stratégie

```c
typedef struct { double px, sz; } Quote;   /* sz > 0 : bid, sz < 0 : ask */

int quotes(void *st, const Ctx *c, const Portfolio *pf,
           const Quote *rest, int n_rest,     /* ordres encore en carnet */
           Quote *out, int max);              /* échelle désirée         */
```

La stratégie renvoie **toute** l'échelle qu'elle veut voir en carnet. Elle
reçoit `rest`, ce qui lui permet d'être incrémentale (`dumb` conserve `rest`
et ajoute ou déplace un ordre) ou de tout recalculer (`envelope`). Le
`StratDef` (options, init, print) est défini dans `strategies/strategy.h`,
pas ici : le core ne connaît que `Strat`.

## 6. Le moteur — `engine.c`

Pour chaque snapshot, dans cet ordre :

0. **sanité** : un saut de mid > `max_jump` entre deux snapshots à moins de
   60 s est écarté et compté (`bad_prints`) ;
1. **indicateurs** (`ctx_update`) ;
2. **matching** des ordres posés contre le nouveau carnet. `through` : un bid
   est rempli si `ask <= px` (le prix a traversé) ; `touch` : si `bid <= px`.
   Un ordre n'est actif qu'après `latency_s` ;
3. **comptabilité**, courbe d'equity, drawdown, **liquidation** si
   `equity < mmr * |notionnel|` (position soldée dans le carnet, taker) ;
4. **stratégie**, au plus une fois par `poll_s`, et seulement si l'EMA a
   bougé de plus de `threshold` ou si un fill a eu lieu depuis ;
5. **diff** prix à prix : même prix et même sens → ordre conservé (garde son
   ancienneté et sa latence) ; sinon annulé/reposé.

L'ordre 2-avant-4 interdit le look-ahead : la stratégie ne voit jamais un
carnet contre lequel ses ordres n'ont pas déjà été confrontés.

À la fin, la position restante est soldée **dans le carnet** en taker.

### Mode `--reverse 1`

Chaque fois qu'un ordre maker **aurait été rempli**, ce fill n'a pas lieu. À
la place le moteur exécute un **taker de même taille dans l'autre sens**,
dans le carnet du snapshot courant, via `snap_walk_full` : on consomme les
niveaux dans l'ordre, et ce qui dépasse la liquidité visible passe au prix du
dernier niveau touché. Le fill paye `taker_fee` et est compté dans
`reversed`.

Exemple : un bid de 5 rempli face à un carnet côté bid de 2 / 1 / 3 → on vend
2 au niveau 1, 1 au niveau 2, 2 au niveau 3. Pour 8 : 2 / 1 / 3 puis 2 de
plus au prix du niveau 3.

La stratégie n'est pas modifiée : elle continue de poser la même échelle.
Pour que ses règles de capacité, de take profit ou d'`--inv` restent
cohérentes, le moteur lui montre la **position miroir** (`size` négaté) :
de son point de vue, ses fills maker ont eu lieu. La position réelle est
l'opposée, l'equity est la même. Une échelle de retour à la moyenne devient
ainsi une stratégie de momentum, à volume identique et frais taker.

## 7. `Result`

`pf` (portefeuille final), `snaps`, `requotes`, `rejected` (ordres non
post-only refusés), `liquidations`, `bad_prints`, `reversed`, `ruined`,
`occupancy` (part de l'échelle effectivement en carnet), `t0`, `t1`.

## Exécutables du core

| binaire | source | rôle |
|---|---|---|
| `bin/extract` | `extract.c` | json.gz → `.l2`, parseur dédié, OpenMP |
| `bin/info` | `info.c` | ce que le moteur voit d'un `.l2` : période, cadence, trous, anomalies |
| `bin/probas` | `probabilities.c` | proba de fill / retour à D% de l'EMA, sans stratégie |
| `bin/backtest` | `backtest.c` | le backtest ; sélectionne une stratégie par son nom |
