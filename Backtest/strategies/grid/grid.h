/* grid.h — parametres par defaut de la strategie grid.
 *
 * Chaque valeur est surchargeable en ligne de commande (voir grid.md).
 * Modifier ici change le defaut pour tous les runs.
 */
#ifndef GRID_H
#define GRID_H

#define GRID_LEVELS    24      /* niveaux par cote */
#define GRID_VALUE     100.5   /* notionnel fixe par ordre ($) */
#define GRID_DELTA     2.0     /* pas entre niveaux (unites de prix) */
#define GRID_GAP       0.6     /* ecart bid/ask au centre (unites de prix) */
#define GRID_CAP       0.80    /* fraction max du portefeuille */
#define GRID_MIN_VALUE 10.5    /* notionnel minimum ($) */

#endif
