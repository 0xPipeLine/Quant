/* dumb.h — parametres par defaut de la strategie dumb.
 *
 * Chaque valeur est surchargeable en ligne de commande (voir dumb.md).
 * Modifier ici change le defaut pour tous les runs.
 */
#ifndef DUMB_H
#define DUMB_H

#define DUMB_GAMMA     2.0     /* distance au mid = gamma * maker_fee */
#define DUMB_CAP       0.90    /* fraction max du portefeuille */
#define DUMB_MIN_VALUE 10.5    /* notionnel de chaque ordre pose ($) */

#endif
