/* dumb.c — transposition de la classe Python Dumb.
 *
 * A chaque cycle :
 *   - s'il reste de la capacite des deux cotes (au moins MIN_VALUE en $) :
 *     on POSE une paire bid/ask de MIN_VALUE chacune, au plus pres du mid
 *     (mid * (1 -/+ GAMMA * maker_fee)), sans jamais croiser le touch ;
 *   - sinon : on DEPLACE le bid le plus bas et l'ask le plus haut vers ces
 *     memes prix (le `modify()` du bot).
 * Les ordres deja en carnet sont conserves tels quels. Le resultat est une
 * pile d'ordres qui s'accumule pres du mid et dont les extremites sont
 * recyclees en permanence : beaucoup de petits fills maker, rotation elevee.
 *
 * C'est la premiere strategie qui utilise `rest` : elle est incrementale.
 * Le bot Python bouge les ordres via modify ; ici le moteur voit un prix
 * different et fait un cancel/replace, ce qui revient au meme.
 */
#include <stdlib.h>
#include <string.h>
#include "strategy.h"

typedef struct { double gamma, cap, min_value; } Dumb;

static int quotes(void *st, const Ctx *c, const Portfolio *pf,
                  const Quote *rest, int n_rest, Quote *out, int max)
{
    Dumb *d = st;
    double mid = c->mid;
    if (mid <= 0) return 0;

    /* 1. on garde tout ce qui est en carnet, en reperant les extremites */
    int n = 0, lo_bid = -1, hi_ask = -1;
    double bids_value, asks_value;
    strat_rest_value(rest, n_rest, &bids_value, &asks_value);
    for (int i = 0; i < n_rest && n < max; i++) {
        out[n] = rest[i];
        if (rest[i].sz > 0 && (lo_bid < 0 || rest[i].px < out[lo_bid].px)) lo_bid = n;
        if (rest[i].sz < 0 && (hi_ask < 0 || rest[i].px > out[hi_ask].px)) hi_ask = n;
        n++;
    }

    double longable, shortable;
    strat_capacity(c, pf, d->cap, bids_value, asks_value, &longable, &shortable);

    double bid_px = fmin(c->bid, mid * (1 - d->gamma * pf->maker_fee));
    double ask_px = fmax(c->ask, mid * (1 + d->gamma * pf->maker_fee));
    /* taille sur le prix, pour que le notionnel soit >= min_value */
    double bid_sz = d->min_value / bid_px, ask_sz = d->min_value / ask_px;

    /* 2. place() ou modify() */
    if (longable * mid > d->min_value && shortable * mid > d->min_value) {
        strat_push(out, &n, max, c, bid_px,  bid_sz, 0);
        strat_push(out, &n, max, c, ask_px, -ask_sz, 0);
    } else {
        if (lo_bid >= 0) { out[lo_bid].px = bid_px; out[lo_bid].sz =  bid_sz; }
        if (hi_ask >= 0) { out[hi_ask].px = ask_px; out[hi_ask].sz = -ask_sz; }
    }
    return n;
}

static Strat create(void)
{
    Dumb *d = calloc(1, sizeof *d);
    d->gamma = 2.0; d->cap = 0.90; d->min_value = 10.5;
    Strat s = { "dumb", d, quotes };
    return s;
}

static int option(void *st, const char *a, const char *v)
{
    Dumb *d = st;
    OPT_D("--gamma",     d->gamma);
    OPT_D("--cap",       d->cap);
    OPT_D("--min-value", d->min_value);
    return 0;
}

static void init(void *st) { (void)st; }

static void print(const void *st, FILE *f)
{
    const Dumb *d = st;
    fprintf(f, "  gamma=%.2f cap=%.2f min-value=%.1f\n", d->gamma, d->cap, d->min_value);
}

const StratDef strat_dumb = {
    "dumb", "pile d'ordres MIN_VALUE accumulee au touch, extremites recyclees", create,
    option, init, print,
    "  --gamma F       distance au mid = gamma * maker_fee (2)\n"
    "  --cap F         fraction max du portefeuille engagee (0.90)\n"
    "  --min-value $   notionnel de chaque ordre pose (10.5)\n"
};
