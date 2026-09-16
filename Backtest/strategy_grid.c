/* strategy_grid.c — grille a pas absolu, ancree au prix de depart.
 *
 * Transposition de ta classe Grid (grid.py) :
 *   - l'ancre est le mid au premier appel (ton `self.initial`) ;
 *   - les niveaux sont a ancre + i*DELTA -/+ GAP/2, en unites de prix
 *     ABSOLUES, pas en pourcentage ;
 *   - chaque ordre a un notionnel fixe VALUE ;
 *   - les prix sont ramenes au touch : bid = min(niveau, best_bid),
 *     ask = max(niveau, best_ask), comme ton `place()`.
 *
 * Une difference assumee : ton bot ajoute une paire par cycle de 10 s et
 * remplit donc la grille progressivement ; ici on renvoie directement tous les
 * niveaux que la capacite autorise. Le regime permanent est le meme, seule la
 * montee en charge des premieres minutes differe.
 */
#include <stdlib.h>
#include <math.h>
#include "l2.h"

typedef struct {
    GridCfg c;
    double  anchor;                  /* prix de reference, fige au demarrage */
} GridSt;

static int quotes(void *st, const Ctx *c, const Portfolio *pf, Quote *out, int max)
{
    GridSt *g = st;
    double mid = c->mid;
    if (mid <= 0) return 0;
    if (g->anchor <= 0) g->anchor = mid;          /* self.initial */

    double delta = g->c.delta, gap = g->c.gap;
    if (delta <= 0) return 0;

    double power    = pf->equity * pf->leverage;
    double position = pf->size * mid;
    double factor   = g->c.cap / mid;
    double longable  = fmax(power - position, 0) * factor;   /* en unites */
    double shortable = fmax(power + position, 0) * factor;

    int n = 0;
    double used;

    /* --- bids : du niveau le plus haut sous le mid, puis en descendant --- */
    long i = (long)floor((mid - g->anchor + gap / 2) / delta);
    used = 0;
    for (int k = 0; k < g->c.levels && n < max; k++) {
        double px = g->anchor + (double)(i - k) * delta - gap / 2;
        if (px > c->bid) px = c->bid;             /* min(niveau, best_bid) */
        if (px <= 0 || px >= c->ask) continue;    /* post-only */
        double sz = g->c.value / px;              /* notionnel fixe */
        if (used + sz > longable) break;          /* capacite epuisee */
        if (px * sz < g->c.min_value) continue;
        out[n].px = px; out[n].sz = sz; n++;
        used += sz;
    }

    /* --- asks : du niveau le plus bas au-dessus du mid, puis en montant --- */
    long j = (long)ceil((mid - g->anchor - gap / 2) / delta);
    used = 0;
    for (int k = 0; k < g->c.levels && n < max; k++) {
        double px = g->anchor + (double)(j + k) * delta + gap / 2;
        if (px < c->ask) px = c->ask;             /* max(niveau, best_ask) */
        if (px <= 0 || px <= c->bid) continue;
        double sz = g->c.value / px;
        if (used + sz > shortable) break;
        if (px * sz < g->c.min_value) continue;
        out[n].px = px; out[n].sz = -sz; n++;
        used += sz;
    }
    return n;
}

Strat grid_new(const GridCfg *cfg)
{
    GridSt *g = calloc(1, sizeof(GridSt));
    g->c = *cfg;
    if (g->c.levels > MAXQ) g->c.levels = MAXQ;
    if (g->c.levels < 1)    g->c.levels = 1;
    Strat s = { "grid", g, quotes };
    return s;
}

void grid_free(Strat *s) { free(s->st); s->st = NULL; }
