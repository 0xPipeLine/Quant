/* strategy_envelope_b.c — enveloppe + take profit maker.
 *
 * Identique a l'enveloppe, avec en plus :
 *
 *   TP = 1  ->  un ordre maker de la TAILLE ENTIERE de la position, place a
 *               TPSPREAD du prix d'entree :
 *                 position longue  : vente a entry * (1 + TPSPREAD)
 *                 position courte  : achat a entry * (1 - TPSPREAD)
 *
 * Le point delicat est le dimensionnement, et il vaut la peine d'etre explicite.
 *
 * Sans TP, la capacite du cote qui REDUIT la position vaut `power + position`
 * (cas long) : elle contient deux choses, la fermeture de la position existante
 * et l'ouverture d'une position inverse. L'echelle consomme les deux.
 *
 * Avec TP, la partie "fermeture" (|position|) est reservee a l'ordre de take
 * profit. L'echelle ne recoit donc plus que la partie "cash", c'est-a-dire
 * `power`. L'exposition maximale totale est inchangee : si le TP et toute
 * l'echelle se remplissent, on a vendu |position| + power, exactement comme
 * avant. On a seulement decide OU se place la partie qui solde.
 *
 *   long  :  longable = power - position     shortable = power
 *   court :  longable = power                shortable = power + position
 *   plat  :  comme l'enveloppe classique
 *
 * Sans ce partage, le TP et l'echelle vendraient tous les deux la meme
 * position et on se retrouverait short du double sans l'avoir voulu.
 */
#include <stdlib.h>
#include <math.h>
#include "l2.h"

typedef struct {
    EnvBCfg c;
    double  geo_px;
    double  beta, geo_sz;
    double  bids[MAXQ], asks[MAXQ];
} EnvB;

static void grid_update(EnvB *e, double ema)
{
    int L = e->c.levels;
    double raw = e->c.spread * ema / e->geo_px;
    double off = e->c.offset * raw;
    double px = ema - off;
    for (int i = 0; i < L; i++) { e->bids[L - 1 - i] = px; px -= raw * pow(e->c.alpha, i); }
    px = ema + off;
    for (int i = 0; i < L; i++) { e->asks[i] = px;         px += raw * pow(e->c.alpha, i); }
}

static void sizes(const EnvB *e, double available, double *out)
{
    int L = e->c.levels;
    if (e->beta == 1.0 || L == 1) {
        for (int i = 0; i < L; i++) out[i] = available / L;
        return;
    }
    double s0 = available / e->geo_sz;
    for (int i = 0; i < L; i++) out[i] = s0 * pow(e->beta, i);
}

static int quotes(void *st, const Ctx *c, const Portfolio *pf, Quote *out, int max)
{
    EnvB *e = st;
    int L = e->c.levels, n = 0;
    double mid = c->mid, ema = c->ema.v;
    if (mid <= 0 || ema <= 0) return 0;

    grid_update(e, ema);

    double power    = pf->equity * pf->leverage;
    double position = pf->size * mid;              /* signe */
    double factor   = e->c.cap / mid;

    /* ---- le take profit, et le partage de capacite qu'il impose ---- */
    int tp_on = e->c.tp && pf->size != 0 && pf->entry > 0 && e->c.tp_spread > 0;
    double longable, shortable;
    if (tp_on && pf->size > 0) {                   /* long : le TP vend */
        longable  = fmax(power - position, 0) * factor;
        shortable = fmax(power, 0) * factor;       /* |position| part au TP */
    } else if (tp_on && pf->size < 0) {            /* court : le TP achete */
        longable  = fmax(power, 0) * factor;       /* |position| part au TP */
        shortable = fmax(power + position, 0) * factor;
    } else {
        longable  = fmax(power - position, 0) * factor;
        shortable = fmax(power + position, 0) * factor;
    }

    if (tp_on) {
        double px, sz;
        if (pf->size > 0) {                        /* vente au-dessus de l'entree */
            px = pf->entry * (1.0 + e->c.tp_spread);
            px = fmax(px, mid * (1 + pf->maker_fee));       /* reste maker */
            sz = -pf->size;
        } else {                                   /* achat sous l'entree */
            px = pf->entry * (1.0 - e->c.tp_spread);
            px = fmin(px, mid * (1 - pf->maker_fee));
            sz = -pf->size;                        /* size < 0 -> sz > 0 */
        }
        int ok = (px > 0) && (sz > 0 ? px < c->ask : px > c->bid)
                 && (px * fabs(sz) >= e->c.min_value);
        if (ok && n < max) { out[n].px = px; out[n].sz = sz; n++; }
    }

    /* ---- l'echelle, inchangee par ailleurs ---- */
    double sz[MAXQ];
    int do_bids = 1, do_asks = 1;
    if (e->c.inv) { do_bids = (ema < mid); do_asks = !do_bids; }

    if (do_bids && longable > 0) {
        sizes(e, longable, sz);
        for (int k = 0; k < L && n < max; k++) {
            double px = fmin(e->bids[k], mid * (1 - pf->maker_fee));
            double q  = sz[L - 1 - k];
            if (px <= 0 || px >= c->ask) continue;
            if (px * q < e->c.min_value) continue;
            out[n].px = px; out[n].sz = q; n++;
        }
    }
    if (do_asks && shortable > 0) {
        sizes(e, shortable, sz);
        for (int k = 0; k < L && n < max; k++) {
            double px = fmax(e->asks[k], mid * (1 + pf->maker_fee));
            double q  = sz[k];
            if (px <= 0 || px <= c->bid) continue;
            if (px * q < e->c.min_value) continue;
            out[n].px = px; out[n].sz = -q; n++;
        }
    }
    return n;
}

Strat envb_new(const EnvBCfg *cfg)
{
    EnvB *e = calloc(1, sizeof(EnvB));
    e->c = *cfg;
    if (e->c.levels > MAXQ) e->c.levels = MAXQ;
    if (e->c.levels < 1)    e->c.levels = 1;
    int L = e->c.levels;

    e->geo_px = (L <= 1)            ? 1.0
              : (e->c.alpha == 1.0) ? (double)(L - 1)
              : (pow(e->c.alpha, L - 1) - 1) / (e->c.alpha - 1);
    e->beta   = (L > 1) ? pow(e->c.skew, 1.0 / (L - 1)) : 1.0;
    e->geo_sz = (e->beta != 1.0) ? (1 - pow(e->beta, L)) / (1 - e->beta) : (double)L;

    Strat s = { "envelopeB", e, quotes };
    return s;
}

void envb_free(Strat *s) { free(s->st); s->st = NULL; }
