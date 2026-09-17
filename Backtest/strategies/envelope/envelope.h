/* envelope.h — parametres par defaut de la strategie envelope.
 *
 * Chaque valeur est surchargeable en ligne de commande (voir envelope.md).
 * Modifier ici change le defaut pour tous les runs.
 */
#ifndef ENV_H
#define ENV_H

#define ENV_LEVELS    24      /* ordres par cote */
#define ENV_SPREAD    0.005   /* ecart total 1er -> dernier ordre (fraction) */
#define ENV_OFFSET    0.0001  /* decalage du 1er ordre (fraction d'un pas) */
#define ENV_ALPHA     1.0     /* espacement geometrique */
#define ENV_SKEW      2.0     /* taille dernier / premier */
#define ENV_CAP       0.85    /* fraction max du portefeuille */
#define ENV_MIN_VALUE 10.5    /* notionnel minimum ($) */
#define ENV_INV       1       /* 1 = cote du retour a la moyenne seulement */
#define ENV_TP        0       /* 1 = take profit */
#define ENV_TP_SPREAD 0.01    /* distance du TP a l'entree (fraction) */

#endif
