/* grid.c — grille a pas absolu, ancree au prix de depart (grid.py).
 *
 *   - l'ancre est le mid au premier appel ;
 *   - les niveaux sont a ancre + i*DELTA -/+ GAP/2, en unites de prix
 *     ABSOLUES, pas en pourcentage ;
 *   - chaque ordre a un notionnel fixe VALUE ;
 *   - les prix sont ramenes au touch : bid = min(niveau, best_bid),
 *     ask = max(niveau, best_ask).
 */
#include <stdlib.h>
#include <string.h>
#include "strategy.h"

typedef struct {
    int    levels;
    double value, delta, gap, cap, min_value;
    double anchor;
} GridSt;

static int quotes(void *st, const Ctx *c, const Portfolio *pf,
                  const Quote *rest, int n_rest, Quote *out, int max)
{
    (void)rest; (void)n_rest;
    GridSt *g = st;
    double mid = c->mid;
    if (mid <= 0) return 0;
    if (g->anchor <= 0) g->anchor = mid;
    double delta = g->delta, gap = g->gap;
    if (delta <= 0) return 0;

    double longable, shortable;
    strat_capacity(c, pf, g->cap, 0, 0, &longable, &shortable);

    int n = 0;
    double used = 0;
    long i = (long)floor((mid - g->anchor + gap / 2) / delta);
    for (int k = 0; k < g->levels && n < max; k++) {
        double px = g->anchor + (double)(i - k) * delta - gap / 2;
        if (px > c->bid) px = c->bid;
        if (px <= 0) continue;
        double sz = g->value / px;
        if (used + sz > longable) break;
        if (strat_push(out, &n, max, c, px, sz, g->min_value)) used += sz;
    }
    used = 0;
    long j = (long)ceil((mid - g->anchor - gap / 2) / delta);
    for (int k = 0; k < g->levels && n < max; k++) {
        double px = g->anchor + (double)(j + k) * delta + gap / 2;
        if (px < c->ask) px = c->ask;
        double sz = g->value / px;
        if (used + sz > shortable) break;
        if (strat_push(out, &n, max, c, px, -sz, g->min_value)) used += sz;
    }
    return n;
}

static Strat create(void)
{
    GridSt *g = calloc(1, sizeof *g);
    g->levels = 24; g->value = 100.5; g->delta = 2.0; g->gap = 0.6;
    g->cap = 0.80; g->min_value = 10.5;
    Strat s = { "grid", g, quotes };
    return s;
}

static int option(void *st, const char *a, const char *v)
{
    GridSt *g = st;
    OPT_I("--levels",    g->levels);
    OPT_D("--value",     g->value);
    OPT_D("--delta",     g->delta);
    OPT_D("--gap",       g->gap);
    OPT_D("--cap",       g->cap);
    OPT_D("--min-value", g->min_value);
    return 0;
}

static void init(void *st)
{
    GridSt *g = st;
    if (g->levels > MAXQ) g->levels = MAXQ;
    if (g->levels < 1)    g->levels = 1;
}

static void print(const void *st, FILE *f)
{
    const GridSt *g = st;
    fprintf(f, "  levels=%d value=%.1f$ delta=%.3f gap=%.3f cap=%.2f min-value=%.1f\n",
            g->levels, g->value, g->delta, g->gap, g->cap, g->min_value);
}

const StratDef strat_grid = {
    "grid", "grille a pas absolu ancree au prix de depart, notionnel fixe", create,
    option, init, print,
    "  --levels N      niveaux par cote (24)\n"
    "  --value $       notionnel fixe de chaque ordre (100.5)\n"
    "  --delta P       pas entre deux niveaux, en unites de PRIX (2.0)\n"
    "  --gap P         ecart bid/ask au centre, en unites de PRIX (0.6)\n"
    "  --cap F         fraction max du portefeuille engagee (0.80)\n"
    "  --min-value $   notionnel minimum d'un ordre (10.5)\n"
};
