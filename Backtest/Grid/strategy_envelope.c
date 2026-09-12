/* strategy_envelope.c — enveloppe / grille geometrique autour d'une EMA.
 *
 * Transposition directe de la classe Python : memes parametres, meme geometrie
 * de prix (Grid.update), meme repartition des tailles (sizes/beta/geo), memes
 * garde-fous (MIN_VALUE, post-only, INV).
 *
 *   prix   : les paliers s'ecartent de la moyenne d'un pas `raw`, multiplie par
 *            alpha^i (alpha=1 -> paliers regulierement espaces ; alpha>1 ->
 *            de plus en plus espaces). SPREAD fixe l'ecart total premier ->
 *            dernier ordre, OFFSET decale le premier palier.
 *   tailles: geometriques de raison beta = SKEW^(1/(L-1)). L'ordre le plus
 *            eloigne de la moyenne est SKEW fois plus gros que le plus proche.
 *   INV    : ne quoter que le cote vers lequel le prix doit revenir. Si le mid
 *            est au-dessus de la moyenne, seuls les bids (sous la moyenne) sont
 *            poses : on ne parie pas contre le retour a la moyenne.
 *
 * Pour ecrire une autre strategie : copier ce fichier et reecrire quotes().
 */
#include <stdlib.h>
#include <math.h>
#include "l2.h"

typedef struct {
    EnvCfg c;
    double geo_px;                   /* somme des alpha^i : normalise SPREAD */
    double beta, geo_sz;             /* skew des tailles                     */
    double bids[MAXQ], asks[MAXQ];   /* prix, croissants des deux cotes      */
} Env;

/* geometrie des prix — equivalent de Grid.update() */
static void grid_update(Env *e, double ema)
{
    int L = e->c.levels;
    double raw = e->c.spread * ema / e->geo_px;    /* pas de base            */
    double off = e->c.offset * raw;                /* decalage du 1er ordre  */

    double px = ema - off;
    for (int i = 0; i < L; i++) {                  /* bids : du + proche...  */
        e->bids[L - 1 - i] = px;                   /* ...stockes croissants  */
        px -= raw * pow(e->c.alpha, i);
    }
    px = ema + off;
    for (int i = 0; i < L; i++) { e->asks[i] = px; px += raw * pow(e->c.alpha, i); }
}

/* repartition geometrique des tailles — equivalent de sizes() */
static void sizes(const Env *e, double available, double *out)
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
    Env *e = st;
    int L = e->c.levels, n = 0;
    double mid = c->mid, ema = c->ema.v;
    if (mid <= 0 || ema <= 0) return 0;

    grid_update(e, ema);

    /* Capacite de chaque cote, en unites de l'actif.
     * longable  : ce qu'on peut encore acheter avant de saturer le levier
     * shortable : idem a la vente. Une position longue reduit l'un et
     * augmente l'autre, ce qui fait naturellement decroitre la taille des
     * ordres au fur et a mesure qu'on se charge. */
    double power    = pf->equity * pf->leverage;
    double position = pf->size * mid;
    double factor   = e->c.cap / mid;
    double longable  = fmax(power - position, 0) * factor;
    double shortable = fmax(power + position, 0) * factor;

    double sz[MAXQ];
    int do_bids = 1, do_asks = 1;
    if (e->c.inv) { do_bids = (ema < mid); do_asks = !do_bids; }

    if (do_bids && longable > 0) {
        sizes(e, longable, sz);
        for (int k = 0; k < L && n < max; k++) {
            double px = fmin(e->bids[k], mid * (1 - pf->maker_fee));
            double q  = sz[L - 1 - k];             /* le plus loin = le plus gros */
            if (px <= 0 || px >= c->ask) continue; /* post-only : ne croise pas   */
            if (px * q < e->c.min_value) continue; /* anti-fragmentation          */
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

Strat env_new(const EnvCfg *cfg)
{
    Env *e = calloc(1, sizeof(Env));
    e->c = *cfg;
    if (e->c.levels > MAXQ) e->c.levels = MAXQ;
    if (e->c.levels < 1)    e->c.levels = 1;
    int L = e->c.levels;

    e->geo_px = (L <= 1)            ? 1.0
              : (e->c.alpha == 1.0) ? (double)(L - 1)
              : (pow(e->c.alpha, L - 1) - 1) / (e->c.alpha - 1);
    e->beta   = (L > 1) ? pow(e->c.skew, 1.0 / (L - 1)) : 1.0;
    e->geo_sz = (e->beta != 1.0) ? (1 - pow(e->beta, L)) / (1 - e->beta) : (double)L;

    Strat s = { "envelope", e, quotes };
    return s;
}

void env_free(Strat *s) { free(s->st); s->st = NULL; }
