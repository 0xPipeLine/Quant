#ifndef BOUGIE_CLASSIQUE_H
#define BOUGIE_CLASSIQUE_H


// ============================================================
// VARIABLES GLOBALES (déclarées dans bougie.c)
// ============================================================

extern struct bougie*ma_bougie;
extern double *close;
extern double *high;
extern double *low;
extern int line_count;


// ============================================================
// FONCTIONS
// ============================================================

// Charge le CSV principal dans ma_bougie, retourne le nb de lignes
int charger_bougies(const char *chemin);

// Libère toute la mémoire allouée
void liberer_bougies(void);

#endif
