/* strategy.h — registre des strategies et helpers communs.
 *
 * Chaque strategie est un dossier strategies/<nom>/ contenant <nom>.c (qui
 * expose un StratDef) et <nom>.md (sa fiche).
 * backtest.c ne connait AUCUNE strategie en particulier : il selectionne un
 * StratDef par son nom, lui delegue le parsing de ses propres options, puis
 * confie le Strat au moteur.
 *
 * Pour ajouter une strategie :
 *   1. copier strategies/envelope/envelope.c en strategies/<nom>/<nom>.c, reecrire quotes()
 *   2. declarer `extern const StratDef strat_<nom>;` ci-dessous
 *   3. l'ajouter au tableau dans registry.c
 *   4. ecrire strategies/<nom>/<nom>.md
 */
#ifndef STRATEGY_H
#define STRATEGY_H

#include <math.h>
#include "l2.h"

typedef struct {
    const char *name;                       /* valeur de --strategy          */
    const char *summary;                    /* une ligne, pour --list        */
    /* Alloue l'etat avec ses valeurs par defaut. */
    Strat (*create)(void);
    /* Une option de la ligne de commande. `v` = argument suivant (ou NULL).
     * Renvoie 0 : inconnue ; 1 : drapeau consomme ; 2 : option + valeur. */
    int  (*option)(void *st, const char *a, const char *v);
    /* Appele apres le parsing : calcule les derives, borne les parametres. */
    void (*init)(void *st);
    /* Affiche les parametres effectifs. */
    void (*print)(const void *st, FILE *f);
    /* Aide : liste des options, une par ligne. */
    const char *help;
} StratDef;

const StratDef *strat_find(const char *name);
void            strat_list(FILE *f);

/* ---------------- helpers partages par les strategies ---------------- */

/* Capacite de chaque cote, en unites de l'actif, une fois retirees les
 * valeurs deja engagees en carnet (bids_value / asks_value, en $).
 *   longable  : ce qu'on peut encore acheter avant de saturer le levier
 *   shortable : idem a la vente
 * Une position longue reduit l'un et augmente l'autre : les tailles
 * decroissent naturellement a mesure qu'on se charge. */
static inline void strat_capacity(const Ctx *c, const Portfolio *pf, double cap,
                                  double bids_value, double asks_value,
                                  double *longable, double *shortable)
{
    double power    = pf->equity * pf->leverage;
    double position = pf->size * c->mid;
    double factor   = cap / c->mid;
    *longable  = fmax(power - position - bids_value, 0) * factor;
    *shortable = fmax(power + position - asks_value, 0) * factor;
}

/* Somme des notionnels deja en carnet, par cote. */
static inline void strat_rest_value(const Quote *rest, int n,
                                    double *bids_value, double *asks_value)
{
    double bv = 0, av = 0;
    for (int i = 0; i < n; i++) {
        double v = rest[i].px * fabs(rest[i].sz);
        if (rest[i].sz > 0) bv += v; else av += v;
    }
    *bids_value = bv; *asks_value = av;
}

/* Ajoute un ordre s'il est post-only et au-dessus du notionnel minimum. */
static inline int strat_push(Quote *out, int *n, int max, const Ctx *c,
                             double px, double sz, double min_value)
{
    if (*n >= max || px <= 0 || sz == 0) return 0;
    if (sz > 0 ? px >= c->ask : px <= c->bid) return 0;   /* post-only */
    if (px * fabs(sz) < min_value) return 0;              /* fragmentation */
    out[*n].px = px; out[*n].sz = sz; (*n)++;
    return 1;
}

/* Parsing d'options : `a` est l'option lue, `name` celle qu'on attend. */
#define OPT_D(name, field) \
    if (!strcmp(a, name)) { if (!v) return 0; (field) = atof(v); return 2; }
#define OPT_I(name, field) \
    if (!strcmp(a, name)) { if (!v) return 0; (field) = atoi(v); return 2; }
#define OPT_FLAG(name, field) \
    if (!strcmp(a, name)) { (field) = 1; return 1; }

#endif
