/* stoikov.h — parametres par defaut de la strategie stoikov.
 *
 * Chaque valeur est surchargeable en ligne de commande (voir stoikov.md).
 * Modifier ici change le defaut pour tous les runs.
 */
#ifndef STOIKOV_H
#define STOIKOV_H

#define STOIKOV_GAMMA      100.0   /* aversion au risque d'inventaire */
#define STOIKOV_K          1000.0  /* intensite du flux */
#define STOIKOV_HORIZON    300.0   /* horizon de risque (s) */
#define STOIKOV_VALUE      50.0    /* notionnel par couche ($) */
#define STOIKOV_CAP        0.90    /* fraction max du portefeuille */
#define STOIKOV_MIN_VALUE  10.5    /* notionnel minimum ($) */
#define STOIKOV_MIN_SPREAD 2.0     /* demi-spread plancher (bps) */
#define STOIKOV_MAX_SPREAD 30.0    /* demi-spread plafond (bps) */
#define STOIKOV_LAYERS     2       /* couches par cote */
#define STOIKOV_STEP       1.0     /* ecart entre couches (multiples du demi-spread) */

#endif
