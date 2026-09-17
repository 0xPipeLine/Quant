/* volband.h — parametres par defaut de la strategie volband.
 *
 * Chaque valeur est surchargeable en ligne de commande (voir volband.md).
 * Modifier ici change le defaut pour tous les runs.
 */
#ifndef VOLBAND_H
#define VOLBAND_H

#define VOLBAND_LEVELS    12      /* paliers par cote */
#define VOLBAND_INV       1       /* 1 = cote du retour a la moyenne seulement */
#define VOLBAND_K1        0.5     /* 1er palier (ecarts-types) */
#define VOLBAND_K2        3.0     /* dernier palier (ecarts-types) */
#define VOLBAND_HORIZON   300.0   /* horizon de la vol (s) */
#define VOLBAND_VALUE     50.0    /* notionnel par palier ($) */
#define VOLBAND_CAP       0.85    /* fraction max du portefeuille */
#define VOLBAND_MIN_VALUE 10.5    /* notionnel minimum ($) */
#define VOLBAND_MIN_BAND  2.0     /* ecart-type plancher (bps) */

#endif
