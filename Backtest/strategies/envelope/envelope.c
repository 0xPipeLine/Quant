/* envelope.c — enveloppe geometrique autour de l'EMA + take profit maker.
 *
 * Echelle de LEVELS ordres par cote centree sur l'EMA (pas geometrique alpha^i,
 * tailles geometriques de raison beta = SKEW^(1/(L-1)), INV = ne quoter que
 * le cote du retour a la moyenne), plus un take profit optionnel :
 *
 *   TP = 1  ->  un ordre maker de la TAILLE ENTIERE de la position, place a
 *               TPSPREAD du prix d'entree.
 *
 * Partage de capacite : la partie "fermeture" (|position|) est reservee au
 * TP, l'echelle ne recoit que la partie "cash" (`power`). L'exposition
 * maximale totale est inchangee ; on decide seulement OU se place la partie
 * qui solde.
 *
 *   long  :  longable = power - position     shortable = power
 *   court :  longable = power                shortable = power + position
 *   plat  :  comme l'enveloppe classique
 */
#include <stdlib.h>
#include <string.h>
#include "strategy.h"
#include "envelope.h"

typedef struct {
    int    levels;
    double spread, offset, alpha, skew, cap, min_value;
    int    inv, tp;
    double tp_spread;
    double geo_px, beta, geo_sz;
    double bids[MAXQ], asks[MAXQ];
} EnvB;

static void grid_update(EnvB *e, double ema)
{
    int L = e->levels;
    double raw = e->spread * ema / e->geo_px;
    double off = e->offset * raw;
    double px = ema - off;
    for (int i = 0; i < L; i++) { e->bids[L - 1 - i] = px; px -= raw * pow(e->alpha, i); }
    px = ema + off;
    for (int i = 0; i < L; i++) { e->asks[i] = px;         px += raw * pow(e->alpha, i); }
}

static void sizes(const EnvB *e, double available, double *out)
{
    int L = e->levels;
    if (e->beta == 1.0 || L == 1) {
        for (int i = 0; i < L; i++) out[i] = available / L;
        return;
    }
    double s0 = available / e->geo_sz;
    for (int i = 0; i < L; i++) out[i] = s0 * pow(e->beta, i);
}

static int quotes(void *st, const Ctx *c, const Portfolio *pf,
                  const Quote *rest, int n_rest, Quote *out, int max)
{
    (void)rest; (void)n_rest;
    EnvB *e = st;
    int L = e->levels, n = 0;
    double mid = c->mid, ema = c->ema.v;
    if (mid <= 0 || ema <= 0) return 0;

    grid_update(e, ema);

    double power    = pf->equity * pf->leverage;
    double position = pf->size * mid;
    double factor   = e->cap / mid;

    int tp_on = e->tp && pf->size != 0 && pf->entry > 0 && e->tp_spread > 0;
    double longable, shortable;
    if (tp_on && pf->size > 0) {
        longable  = fmax(power - position, 0) * factor;
        shortable = fmax(power, 0) * factor;
    } else if (tp_on && pf->size < 0) {
        longable  = fmax(power, 0) * factor;
        shortable = fmax(power + position, 0) * factor;
    } else {
        longable  = fmax(power - position, 0) * factor;
        shortable = fmax(power + position, 0) * factor;
    }

    if (tp_on) {
        double px;
        if (pf->size > 0) px = fmax(pf->entry * (1.0 + e->tp_spread), mid * (1 + pf->maker_fee));
        else              px = fmin(pf->entry * (1.0 - e->tp_spread), mid * (1 - pf->maker_fee));
        strat_push(out, &n, max, c, px, -pf->size, e->min_value);
    }

    double sz[MAXQ];
    int do_bids = 1, do_asks = 1;
    if (e->inv) { do_bids = (ema < mid); do_asks = !do_bids; }

    if (do_bids && longable > 0) {
        sizes(e, longable, sz);
        for (int k = 0; k < L; k++)
            strat_push(out, &n, max, c, fmin(e->bids[k], mid * (1 - pf->maker_fee)),
                       sz[L - 1 - k], e->min_value);
    }
    if (do_asks && shortable > 0) {
        sizes(e, shortable, sz);
        for (int k = 0; k < L; k++)
            strat_push(out, &n, max, c, fmax(e->asks[k], mid * (1 + pf->maker_fee)),
                       -sz[k], e->min_value);
    }
    return n;
}

static Strat create(void)
{
    EnvB *e = calloc(1, sizeof *e);
    e->levels = ENV_LEVELS; e->spread = ENV_SPREAD; e->offset = ENV_OFFSET;
    e->alpha = ENV_ALPHA; e->skew = ENV_SKEW; e->cap = ENV_CAP;
    e->min_value = ENV_MIN_VALUE; e->inv = ENV_INV;
    e->tp = ENV_TP; e->tp_spread = ENV_TP_SPREAD;
    Strat s = { "envelope", e, quotes };
    return s;
}

static int option(void *st, const char *a, const char *v)
{
    EnvB *e = st;
    OPT_I("--levels",    e->levels);
    OPT_D("--spread",    e->spread);
    OPT_D("--offset",    e->offset);
    OPT_D("--alpha",     e->alpha);
    OPT_D("--skew",      e->skew);
    OPT_D("--cap",       e->cap);
    OPT_D("--min-value", e->min_value);
    OPT_I("--inv",       e->inv);
    OPT_I("--tp",        e->tp);
    OPT_D("--tp-spread", e->tp_spread);
    return 0;
}

static void init(void *st)
{
    EnvB *e = st;
    if (e->levels > MAXQ) e->levels = MAXQ;
    if (e->levels < 1)    e->levels = 1;
    int L = e->levels;
    e->geo_px = (L <= 1)          ? 1.0
              : (e->alpha == 1.0) ? (double)(L - 1)
              : (pow(e->alpha, L - 1) - 1) / (e->alpha - 1);
    e->beta   = (L > 1) ? pow(e->skew, 1.0 / (L - 1)) : 1.0;
    e->geo_sz = (e->beta != 1.0) ? (1 - pow(e->beta, L)) / (1 - e->beta) : (double)L;
}

static void print(const void *st, FILE *f)
{
    const EnvB *e = st;
    fprintf(f, "  levels=%d spread=%.4f offset=%.4f alpha=%.3f skew=%.2f "
               "cap=%.2f min-value=%.1f inv=%d tp=%d tp-spread=%.4f\n",
            e->levels, e->spread, e->offset, e->alpha, e->skew, e->cap,
            e->min_value, e->inv, e->tp, e->tp_spread);
}

const StratDef strat_envelope = {
    "envelope", "echelle geometrique autour de l'EMA, tailles skewees, TP maker optionnel", create,
    option, init, print,
    "  --levels N      ordres par cote (24)\n"
    "  --spread F      ecart total 1er -> dernier ordre, fraction (0.005)\n"
    "  --offset F      decalage du 1er ordre, en fraction d'un pas (0.0001)\n"
    "  --alpha F       espacement geometrique, 1 = regulier (1)\n"
    "  --skew F        taille dernier / premier ordre (2)\n"
    "  --cap F         fraction max du portefeuille engagee (0.85)\n"
    "  --min-value $   notionnel minimum d'un ordre (10.5)\n"
    "  --inv 0|1       1 = ne quoter que le cote du retour a la moyenne (1)\n"
    "  --tp 0|1        1 = poser un TP de la taille de la position (0)\n"
    "  --tp-spread F   distance du TP au prix d'entree, fraction (0.01)\n"
};
