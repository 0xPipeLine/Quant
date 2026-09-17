/* stoikov.c — market making a la Avellaneda-Stoikov (2008), version
 * relative et simplifiee.
 *
 * Deux idees seulement :
 *
 *   1. prix de reservation : le "vrai" mid pour NOUS depend de l'inventaire.
 *        r = mid * (1 - q * GAMMA * sigma^2 * T)
 *      q = position / capital (signe). Long -> r sous le mid : on vend plus
 *      facilement et on achete moins facilement. C'est ce qui recentre
 *      l'inventaire sans jamais payer de taker.
 *
 *   2. demi-spread optimal autour de r :
 *        delta = GAMMA * sigma^2 * T / 2  +  ln(1 + GAMMA / K) / GAMMA
 *      Le premier terme paie le risque d'inventaire (plus la vol est haute,
 *      plus on s'ecarte), le second est la prime de liquidite (K grand =
 *      flux dense = on peut serrer). delta est borne en dessous par
 *      MIN_SPREAD (bps), qui doit couvrir au moins 2 x maker_fee.
 *
 * sigma est la vol du moteur (ecart-type des rendements par sqrt(s)),
 * T est l'horizon en secondes. sigma^2 * T est donc une variance relative.
 *
 * Contrairement a l'enveloppe, la strategie ne parie pas sur un retour a une
 * moyenne : elle serre le spread et laisse l'inventaire piloter les prix.
 * Volume eleve, inventaire bas, sensible au reglage de GAMMA.
 */
#include <stdlib.h>
#include <string.h>
#include "strategy.h"
#include "stoikov.h"

typedef struct {
    double gamma, k, horizon, value, cap, min_value, min_spread, max_spread;
    int    layers;
    double step;                    /* ecart entre couches, en multiples de delta */
} Stoikov;

static int quotes(void *st, const Ctx *c, const Portfolio *pf,
                  const Quote *rest, int n_rest, Quote *out, int max)
{
    (void)rest; (void)n_rest;
    Stoikov *s = st;
    double mid = c->mid;
    if (mid <= 0 || pf->equity <= 0) return 0;

    double var  = c->vol * c->vol * s->horizon;             /* sigma^2 T */
    double q    = pf->size * mid / pf->equity;              /* inventaire */
    double r    = mid * (1.0 - q * s->gamma * var);         /* reservation */
    double dlt  = 0.5 * s->gamma * var + log(1.0 + s->gamma / s->k) / s->gamma;
    if (dlt < s->min_spread * 1e-4) dlt = s->min_spread * 1e-4;
    if (dlt > s->max_spread * 1e-4) dlt = s->max_spread * 1e-4;

    double longable, shortable;
    strat_capacity(c, pf, s->cap, 0, 0, &longable, &shortable);

    int n = 0;
    double used = 0;
    for (int i = 0; i < s->layers; i++) {
        double px = fmin(r * (1.0 - dlt * (1 + i * s->step)), mid * (1 - pf->maker_fee));
        double sz = s->value / px;
        if (used + sz > longable) break;
        if (strat_push(out, &n, max, c, px, sz, s->min_value)) used += sz;
    }
    used = 0;
    for (int i = 0; i < s->layers; i++) {
        double px = fmax(r * (1.0 + dlt * (1 + i * s->step)), mid * (1 + pf->maker_fee));
        double sz = s->value / px;
        if (used + sz > shortable) break;
        if (strat_push(out, &n, max, c, px, -sz, s->min_value)) used += sz;
    }
    return n;
}

static Strat create(void)
{
    Stoikov *s = calloc(1, sizeof *s);
    s->gamma = STOIKOV_GAMMA; s->k = STOIKOV_K; s->horizon = STOIKOV_HORIZON;
    s->value = STOIKOV_VALUE; s->cap = STOIKOV_CAP; s->min_value = STOIKOV_MIN_VALUE;
    s->min_spread = STOIKOV_MIN_SPREAD; s->max_spread = STOIKOV_MAX_SPREAD;
    s->layers = STOIKOV_LAYERS; s->step = STOIKOV_STEP;
    Strat st = { "stoikov", s, quotes };
    return st;
}

static int option(void *st, const char *a, const char *v)
{
    Stoikov *s = st;
    OPT_D("--gamma",      s->gamma);
    OPT_D("--k",          s->k);
    OPT_D("--horizon",    s->horizon);
    OPT_D("--value",      s->value);
    OPT_D("--cap",        s->cap);
    OPT_D("--min-value",  s->min_value);
    OPT_D("--min-spread", s->min_spread);
    OPT_D("--max-spread", s->max_spread);
    OPT_I("--layers",     s->layers);
    OPT_D("--step",       s->step);
    return 0;
}

static void init(void *st)
{
    Stoikov *s = st;
    if (s->layers > MAXQ) s->layers = MAXQ;
    if (s->layers < 1)    s->layers = 1;
    if (s->gamma <= 0)    s->gamma = 1e-6;
    if (s->k <= 0)        s->k = 1e-6;
}

static void print(const void *st, FILE *f)
{
    const Stoikov *s = st;
    fprintf(f, "  gamma=%.2f k=%.2f horizon=%.0fs value=%.1f$ cap=%.2f "
               "min-value=%.1f spread=[%.1f,%.1f]bps layers=%d step=%.2f\n",
            s->gamma, s->k, s->horizon, s->value, s->cap, s->min_value,
            s->min_spread, s->max_spread, s->layers, s->step);
}

const StratDef strat_stoikov = {
    "stoikov", "Avellaneda-Stoikov : spread par la vol, prix par l'inventaire", create,
    option, init, print,
    "  --gamma F       aversion au risque d'inventaire (100)\n"
    "  --k F           intensite du flux ; grand = spread serre (1000)\n"
    "  --horizon S     horizon de risque, en secondes (300)\n"
    "  --value $       notionnel de chaque couche (50)\n"
    "  --layers N      couches par cote (2)\n"
    "  --step F        ecart entre couches, en multiples du demi-spread (1)\n"
    "  --min-spread B  demi-spread minimum, en bps (2)\n"
    "  --max-spread B  demi-spread maximum, en bps (30)\n"
    "  --cap F         fraction max du portefeuille engagee (0.90)\n"
    "  --min-value $   notionnel minimum d'un ordre (10.5)\n"
};
