# Notes d'architecture

Le README explique l'utilisation et le fonctionnement. Ici : pourquoi c'est
fait comme ça, comment c'est validé, et quoi ajouter ensuite.

## Les décisions structurantes

| Décision | Pourquoi |
|---|---|
| L'enregistrement disque **est** la struct C | supprime tout le code de sérialisation, et fait de `mmap` la seule couche d'accès |
| Ingestion en Python, moteur en C | le parsing JSON est fait une fois et n'a pas besoin d'être rapide ; c'est la partie qu'on modifie le plus souvent (nouveaux champs, nouveau marché) |
| Prix en `double`, pas en entiers scalés | un format entier serait 40 % plus compact et exact au tick près, mais impose des conversions partout. À ce volume ça ne vaut pas la complexité |
| Horloge en ns, jamais d'index de bougie | les snapshots ne sont pas équidistants (~1,35 s, avec des trous) |
| EMA à décroissance temporelle par défaut | `α = 1 − e^(−Δt/τ)`. Une EMA « sur N points » saute d'un coup après un gap de 10 minutes. Le mode « bougie » (`--timeframe/--window`) reste disponible pour reproduire le bot à l'identique — voir README §7 |
| Volatilité en `r²/Δt` | donne un écart-type par √seconde, la seule unité qui rende l'écartement d'une grille comparable entre régimes. Une enveloppe se paramètre rarement en « 0,5 % fixe », plutôt en « k·σ » |
| Un seul invariant comptable | `equity = cash + size × mid`, tout le reste en découle |

## Diagnostic

Deux règles tenues dans tout le code, apprises à la dure :

- **Aucun chemin d'erreur ne retourne sans expliquer.** Un `return -1` muet dans
  l'ouverture du fichier donne un programme qui sort en silence avec le code 1,
  et rien pour comprendre pourquoi.
- **Sortie non bufferisée** (`_IONBF` sur `stdout` et `stderr` en tête de
  `main`). Sous MSYS2 la sortie passe par un tuyau, donc la libc bufferise par
  blocs ; un plantage avale alors tout l'affichage, messages d'erreur compris.

`./info book.l2` est l'outil à lancer en premier : il montre ce que le moteur
voit du fichier, y compris la taille des pointeurs du binaire.

## Comment c'est validé

- **Format** : la struct C et le dtype numpy font tous les deux 656 octets, et
  les valeurs relues côté C sont identiques à celles relues côté Python sur
  l'échantillon réel (prix, tailles type `260.652`, timestamp à la nanoseconde).
- **Comptabilité** : sur une marche aléatoire pure, l'enveloppe rend −0,06 bps
  par unité de volume, c'est-à-dire zéro moins les frais. C'est le seul test qui
  compte vraiment — un moteur qui gagne de l'argent sur du bruit blanc a un
  bug de signe ou de look-ahead quelque part.
- **Garde-fou permanent** : contrôle post-only (et non-NaN) à chaque appel de
  stratégie. Un ordre qui croiserait le carnet est refusé et compté (`rejected`)
  au lieu d'être exécuté — et le run continue, plutôt qu'un `abort()` qui
  emporterait tout l'affichage avec lui.
- **Passage à l'échelle** : vérifié sur un fichier de 2,2 Go (3,36 M snapshots)
  — ouverture instantanée, run complet en 0,5 s.

## Ce qu'il faut ajouter, par ordre d'utilité

1. **Distribution des déviations** `(mid − ema)/ema`, par quantiles et par
   régime de volatilité. Poser les paliers aux quantiles empiriques plutôt qu'à
   pas fixe est en général le gain le plus immédiat sur une grille.
2. **Matrice de transition entre paliers** : `P(atteindre le palier k+1 | palier
   k atteint)`. L'espérance de PnL d'une grille se calcule analytiquement à
   partir de cette matrice et du PnL moyen par palier — c'est l'objet exact pour
   dimensionner `LEVELS` et `SPREAD` au lieu de balayer à l'aveugle.
3. **Temps de séjour hors bande** `E[durée | |dev| > D]` → capital immobilisé,
   donc le vrai rendement annualisé plutôt que le PnL brut.
4. **Liquidité à D%** via `snap_depth()` → taille maximale par palier sans
   impact. C'est ce qui plafonne la taille du compte, et ça ne se voit sur
   aucune donnée en bougies.
5. **Modèle de fill à file d'attente** : à la pose, `Q` = taille présente au
   niveau ; entre deux snapshots, si le niveau est au meilleur prix et que le
   prix baisse ensuite, attribuer la diminution à des exécutions, sinon à des
   annulations. Approximation à ne sortir que si tu obtiens un jour la tape des
   trades pour la calibrer.

Chacune est une boucle `for (k = 0; k < b.n; k++)` de quelques dizaines de
lignes sur le modèle de `probabilities.c`.

## Balayages de paramètres

Un run est un parcours séquentiel d'un fichier mappé en lecture seule : `fork()`
de N workers, un par combinaison, le page cache est partagé entre eux. Pas de
threads, pas de verrous, pas de mémoire dupliquée. Un runner de 40 lignes suffit.

Penser aussi à découper par période (`book_seek()` donne l'index d'un
timestamp) : une stratégie qui ne gagne que sur un mois sur douze n'est pas une
stratégie.

## Rappel : ce qui restait à corriger dans le C précédent

- `double *close;` global **masque `close()` de la libc**.
- `int palier_close[nb_ligne] = 0;` — un VLA ne peut pas être initialisé.
- `strat_enveloppes` déclarée `double *` dans le `.h`, définie `double`.
- `abs()` (entier) appliqué à des `double` ; `int *palier_max = 0;` puis
  `palier_max++` incrémente un pointeur nul.
- `strat_crois` ne retourne rien alors qu'elle est déclarée `double *`.
- `new_pf` place les niveaux en `prix·(1 ± (i+1)·SPREAD)` alors que `update()`
  les reconstruit autour de `c.moy` : les deux échelles ne coïncident pas.
- La comptabilité mélangeait `value`, `usdc_wallet` et `size·entry` sans
  invariant vérifiable — le point qui coûtait le plus de temps de debug.
