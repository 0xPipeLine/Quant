/* touch.h — parametres par defaut de la strategie touch.
 *
 * Chaque valeur est surchargeable en ligne de commande (voir touch.md).
 * Modifier ici change le defaut pour tous les runs.
 */
#ifndef TOUCH_H
#define TOUCH_H

#define TOUCH_LAYERS    3       /* couches par cote */
#define TOUCH_TICK      0.1     /* espacement des couches (unites de prix) */
#define TOUCH_VALUE     50.0    /* notionnel par couche ($) */
#define TOUCH_CAP       0.90    /* fraction max du portefeuille */
#define TOUCH_MIN_VALUE 10.5    /* notionnel minimum ($) */
#define TOUCH_MAX_INV   0.5     /* inventaire max (fraction du capital) */
#define TOUCH_LEAN      0.0     /* decalage des prix par unite d'inventaire */

#endif
