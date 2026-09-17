/* touch.c — "rejoindre le touch" : le market making le plus brut.
 *
 * On colle au meilleur bid et au meilleur ask, en LAYERS couches espacees de
 * TICK, avec un notionnel VALUE par couche. Pas de moyenne, pas de modele :
 * on est toujours en tete de la file, on ramasse tout le flux qui traverse.
 * C'est la strategie qui genere le plus de volume maker par $ de capital, et
 * celle qui souffre le plus de la selection adverse : chaque fill est par
 * definition un mouvement qui vient de traverser notre prix.
 *
 * Deux garde-fous :
 *   - MAX_INV : au-dela de cette fraction du capital en position, on ne
 *     quote plus que le cote qui reduit (on ne s'entete pas) ;
 *   - LEAN : le mid de reference est deplace de LEAN * position, ce qui
 *     rapproche le cote qui reduit et eloigne l'autre.
 *
 * Important : cette strategie DOIT suivre le touch, donc le moteur doit la
 * rappeler des que le carnet bouge -> lancer avec --threshold 0.
 */
#include <stdlib.h>
#include <string.h>
#include "strategy.h"
#include "touch.h"

typedef struct {
    int    layers;
    double tick, value, cap, min_value, max_inv, lean;
} Touch;

static int quotes(void *st, const Ctx *c, const Portfolio *pf,
                  const Quote *rest, int n_rest, Quote *out, int max)
{
    (void)rest; (void)n_rest;
    Touch *t = st;
    double mid = c->mid;
    if (mid <= 0 || pf->equity <= 0) return 0;

    double longable, shortable;
    strat_capacity(c, pf, t->cap, 0, 0, &longable, &shortable);

    /* inventaire signe en fraction du capital : [-lev, +lev] */
    double inv = pf->size * mid / pf->equity;
    int do_bids = inv <  t->max_inv;
    int do_asks = inv > -t->max_inv;

    /* on penche : long -> on baisse les deux cotes, court -> on les monte */
    double shift = -t->lean * inv * mid;
    double bid0 = fmin(c->bid + shift, c->bid);
    double ask0 = fmax(c->ask + shift, c->ask);

    int n = 0;
    double used = 0;
    for (int k = 0; do_bids && k < t->layers; k++) {
        double px = bid0 - k * t->tick, sz = t->value / px;
        if (used + sz > longable) break;
        if (strat_push(out, &n, max, c, px, sz, t->min_value)) used += sz;
    }
    used = 0;
    for (int k = 0; do_asks && k < t->layers; k++) {
        double px = ask0 + k * t->tick, sz = t->value / px;
        if (used + sz > shortable) break;
        if (strat_push(out, &n, max, c, px, -sz, t->min_value)) used += sz;
    }
    return n;
}

static Strat create(void)
{
    Touch *t = calloc(1, sizeof *t);
    t->layers = TOUCH_LAYERS; t->tick = TOUCH_TICK; t->value = TOUCH_VALUE;
    t->cap = TOUCH_CAP; t->min_value = TOUCH_MIN_VALUE; t->max_inv = TOUCH_MAX_INV;
    t->lean = TOUCH_LEAN;
    Strat s = { "touch", t, quotes };
    return s;
}

static int option(void *st, const char *a, const char *v)
{
    Touch *t = st;
    OPT_I("--layers",    t->layers);
    OPT_D("--tick",      t->tick);
    OPT_D("--value",     t->value);
    OPT_D("--cap",       t->cap);
    OPT_D("--min-value", t->min_value);
    OPT_D("--max-inv",   t->max_inv);
    OPT_D("--lean",      t->lean);
    return 0;
}

static void init(void *st)
{
    Touch *t = st;
    if (t->layers > MAXQ) t->layers = MAXQ;
    if (t->layers < 1)    t->layers = 1;
}

static void print(const void *st, FILE *f)
{
    const Touch *t = st;
    fprintf(f, "  layers=%d tick=%.4f value=%.1f$ cap=%.2f min-value=%.1f "
               "max-inv=%.2f lean=%.4f\n",
            t->layers, t->tick, t->value, t->cap, t->min_value, t->max_inv, t->lean);
}

const StratDef strat_touch = {
    "touch", "colle au meilleur bid/ask en couches, volume maximal", create,
    option, init, print,
    "  --layers N      couches par cote (3)\n"
    "  --tick P        espacement des couches, en unites de PRIX (0.1)\n"
    "  --value $       notionnel de chaque couche (50)\n"
    "  --cap F         fraction max du portefeuille engagee (0.90)\n"
    "  --min-value $   notionnel minimum d'un ordre (10.5)\n"
    "  --max-inv F     inventaire max (fraction du capital) avant de ne quoter\n"
    "                  que le cote qui reduit (0.5)\n"
    "  --lean F        decalage des prix = -lean * inventaire (0 = aucun)\n"
};
