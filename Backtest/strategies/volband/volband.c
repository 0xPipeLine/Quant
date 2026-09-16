/* volband.c — echelle dont la largeur suit la volatilite.
 *
 * L'enveloppe pose ses paliers a une distance FIXE en pourcentage de l'EMA :
 * trop large en marche calme (rien ne se remplit), trop serree en marche
 * agite (tout se remplit, on se charge). Ici les paliers sont poses en
 * unites d'ecart-type attendu sur un horizon H :
 *
 *      band = sigma * sqrt(H)             (deplacement typique en H secondes)
 *      bids : ema * (1 - band * k_i)      k_i regulierement espaces de K1 a K2
 *      asks : ema * (1 + band * k_i)
 *
 * En marche calme l'echelle se resserre et continue de tourner ; en marche
 * agite elle s'ecarte toute seule. Le notionnel est FIXE par palier (VALUE),
 * pas reparti : le nombre de paliers effectivement poses est borne par la
 * capacite, ce qui limite l'inventaire sans skew.
 *
 * INV = 1 : comme l'enveloppe, seul le cote du retour a la moyenne est quote.
 */
#include <stdlib.h>
#include <string.h>
#include "strategy.h"

typedef struct {
    int    levels, inv;
    double k1, k2, horizon, value, cap, min_value, min_band;
} VolBand;

static int quotes(void *st, const Ctx *c, const Portfolio *pf,
                  const Quote *rest, int n_rest, Quote *out, int max)
{
    (void)rest; (void)n_rest;
    VolBand *b = st;
    double mid = c->mid, ema = c->ema.v;
    if (mid <= 0 || ema <= 0) return 0;

    double band = c->vol * sqrt(b->horizon);
    if (band < b->min_band * 1e-4) band = b->min_band * 1e-4;

    double longable, shortable;
    strat_capacity(c, pf, b->cap, 0, 0, &longable, &shortable);

    int do_bids = 1, do_asks = 1;
    if (b->inv) { do_bids = (ema < mid); do_asks = !do_bids; }

    int n = 0, L = b->levels;
    double used = 0;
    for (int i = 0; do_bids && i < L; i++) {
        double k  = L > 1 ? b->k1 + (b->k2 - b->k1) * i / (L - 1) : b->k1;
        double px = fmin(ema * (1 - band * k), mid * (1 - pf->maker_fee));
        double sz = b->value / px;
        if (used + sz > longable) break;
        if (strat_push(out, &n, max, c, px, sz, b->min_value)) used += sz;
    }
    used = 0;
    for (int i = 0; do_asks && i < L; i++) {
        double k  = L > 1 ? b->k1 + (b->k2 - b->k1) * i / (L - 1) : b->k1;
        double px = fmax(ema * (1 + band * k), mid * (1 + pf->maker_fee));
        double sz = b->value / px;
        if (used + sz > shortable) break;
        if (strat_push(out, &n, max, c, px, -sz, b->min_value)) used += sz;
    }
    return n;
}

static Strat create(void)
{
    VolBand *b = calloc(1, sizeof *b);
    b->levels = 12; b->inv = 1; b->k1 = 0.5; b->k2 = 3.0; b->horizon = 300.0;
    b->value = 50.0; b->cap = 0.85; b->min_value = 10.5; b->min_band = 2.0;
    Strat s = { "volband", b, quotes };
    return s;
}

static int option(void *st, const char *a, const char *v)
{
    VolBand *b = st;
    OPT_I("--levels",    b->levels);
    OPT_I("--inv",       b->inv);
    OPT_D("--k1",        b->k1);
    OPT_D("--k2",        b->k2);
    OPT_D("--horizon",   b->horizon);
    OPT_D("--value",     b->value);
    OPT_D("--cap",       b->cap);
    OPT_D("--min-value", b->min_value);
    OPT_D("--min-band",  b->min_band);
    return 0;
}

static void init(void *st)
{
    VolBand *b = st;
    if (b->levels > MAXQ) b->levels = MAXQ;
    if (b->levels < 1)    b->levels = 1;
}

static void print(const void *st, FILE *f)
{
    const VolBand *b = st;
    fprintf(f, "  levels=%d k=[%.2f,%.2f] horizon=%.0fs value=%.1f$ cap=%.2f "
               "min-value=%.1f min-band=%.1fbps inv=%d\n",
            b->levels, b->k1, b->k2, b->horizon, b->value, b->cap,
            b->min_value, b->min_band, b->inv);
}

const StratDef strat_volband = {
    "volband", "echelle autour de l'EMA, largeur = k * sigma * sqrt(H)", create,
    option, init, print,
    "  --levels N      paliers par cote (12)\n"
    "  --k1 F          1er palier, en ecarts-types (0.5)\n"
    "  --k2 F          dernier palier, en ecarts-types (3.0)\n"
    "  --horizon S     horizon de la vol, en secondes (300)\n"
    "  --value $       notionnel fixe de chaque palier (50)\n"
    "  --min-band B    ecart-type plancher, en bps (2)\n"
    "  --cap F         fraction max du portefeuille engagee (0.85)\n"
    "  --min-value $   notionnel minimum d'un ordre (10.5)\n"
    "  --inv 0|1       1 = ne quoter que le cote du retour a la moyenne (1)\n"
};
