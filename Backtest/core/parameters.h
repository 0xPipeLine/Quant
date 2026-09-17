/* parameters.h — parametres generaux par defaut du moteur.
 *
 * Tout ce qui est commun a toutes les strategies : frais, levier, cadence,
 * modele de fill, moyenne de reference. Chaque valeur est surchargeable en
 * ligne de commande (bin/backtest --help). Les parametres propres a une
 * strategie sont dans strategies/<nom>/<nom>.h.
 */
#ifndef PARAMETERS_H
#define PARAMETERS_H

/* ---- execution ---- */
#define DEF_FILL        FILL_THROUGH  /* FILL_THROUGH (conservateur) ou FILL_TOUCH */
#define DEF_REVERSE     0             /* 1 : fills maker -> takers inverses        */
#define DEF_MAKER_FEE   0.00003       /* fraction du notionnel                      */
#define DEF_TAKER_FEE   0.00009
#define DEF_LATENCY_MS  0.0           /* delai avant qu'un ordre pose soit actif    */

/* ---- capital ---- */
#define DEF_LEVERAGE    20.0
#define DEF_INITIAL     1000.0        /* $ */
#define DEF_MMR         (-1.0)        /* < 0 : 0.5 / levier, comme Hyperliquid      */

/* ---- cadence de la strategie (calquee sur le bot live) ---- */
#define DEF_POLL_S      10.1          /* delai min entre deux appels                */
#define DEF_THRESHOLD   0.0001        /* variation relative min de l'EMA            */
#define DEF_WARMUP_TAU  5.0           /* warmup = N constantes de temps de l'EMA    */

/* ---- donnees ---- */
#define DEF_MAX_JUMP    0.10          /* saut de mid aberrant ; 0 = pas de filtre   */
#define DEF_EQUITY_EVERY_S 60.0       /* cadence du CSV d'equity                    */

/* ---- moyenne de reference (EMA sur le mid) ---- */
#define DEF_EMA_TAU     300.0         /* mode continu, secondes                     */
#define DEF_EMA_TF      0.0           /* > 0 : mode bougie, duree en secondes       */
#define DEF_EMA_WINDOW  5
#define DEF_EMA_WILDER  0             /* 1 : alpha = 1/N                            */
#define DEF_EMA_BLEND   1             /* 1 : melange au mid courant (MarketHL)      */
#define DEF_VOL_TAU     60.0          /* EWMA de la variance, secondes              */

#endif
