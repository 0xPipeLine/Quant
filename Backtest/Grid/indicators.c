/* indicators.c — indicateurs mis a jour en O(1) par snapshot.
 *
 * Tout est indexe sur le TEMPS, pas sur le nombre de snapshots : les carnets
 * n'arrivent pas a intervalle regulier (~1,35 s en moyenne, avec des trous).
 * Une EMA "sur N points" sauterait d'un coup apres une interruption.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "l2.h"

double ema_push(Ema *e, double x, double dt)
{
    if (!e->init) { e->v = x; e->init = 1; return e->v; }
    if (dt < 0) dt = 0;
    e->v += (1.0 - exp(-dt / e->tau)) * (x - e->v);   /* alpha depend de dt */
    return e->v;
}

int sma_init(Sma *s, int cap, double win_s)
{
    s->v = malloc((size_t)cap * sizeof(double));
    s->t = malloc((size_t)cap * sizeof(int64_t));
    if (!s->v || !s->t) return -1;
    s->cap = cap; s->head = s->count = 0; s->sum = 0;
    s->win = (int64_t)(win_s * 1e9);
    return 0;
}

void sma_free(Sma *s) { free(s->v); free(s->t); s->v = NULL; s->t = NULL; }

double sma_push(Sma *s, int64_t ts, double x)
{
    if (s->count == s->cap) {                        /* ring plein */
        int tail = (s->head - s->count + s->cap) % s->cap;
        s->sum -= s->v[tail]; s->count--;
    }
    s->v[s->head] = x; s->t[s->head] = ts;
    s->sum += x; s->head = (s->head + 1) % s->cap; s->count++;
    while (s->count > 1) {                           /* sortie de fenetre */
        int tail = (s->head - s->count + s->cap) % s->cap;
        if (ts - s->t[tail] <= s->win) break;
        s->sum -= s->v[tail]; s->count--;
    }
    return s->sum / s->count;
}

void ctx_init(Ctx *c, double ema_tau, double vol_tau)
{
    memset(c, 0, sizeof *c);
    c->ema.tau = ema_tau;
    c->var.tau = vol_tau;
    c->tf = 0.0;                       /* mode continu */
}

/* Constante de temps equivalente a un EMA de fenetre N sur des bougies de
 * duree T. On egalise les deux coefficients sur un pas de duree T :
 *
 *      1 - exp(-T/tau) = alpha        ->      tau = -T / ln(1 - alpha)
 *
 * avec alpha = 2/(N+1) (span) ou 1/N (Wilder).
 * Exemple : N=5 sur du 5 min  ->  tau = 740 s, pas 300 s. */
double ema_tau_of(double tf, int window, int wilder)
{
    if (window <= 1 || tf <= 0) return tf > 0 ? tf : 1.0;
    double alpha = wilder ? 1.0 / window : 2.0 / (window + 1.0);
    return -tf / log(1.0 - alpha);
}

void ctx_init_candle(Ctx *c, double tf, int window, int wilder, int blend,
                     double vol_tau)
{
    memset(c, 0, sizeof *c);
    c->var.tau = vol_tau;
    c->tf      = tf;
    c->blend   = blend;
    c->alpha_c = (window <= 1) ? 1.0
               : (wilder ? 1.0 / window : 2.0 / (window + 1.0));
    c->ema.tau = ema_tau_of(tf, window, wilder);   /* pour information */
}

void ctx_update(Ctx *c, const Snap *s)
{
    double prev = c->mid;
    int64_t prev_ts = c->ts;

    c->s = s; c->ts = s->ts;
    c->dt = c->i ? (double)(c->ts - prev_ts) * 1e-9 : 0.0;

    c->bid = s->bid_px[0];
    c->ask = s->ask_px[0];
    c->mid = 0.5 * (c->bid + c->ask);
    c->spread = c->ask - c->bid;

    double bs = s->bid_sz[0], as = s->ask_sz[0];
    /* microprix : le mid pondere par les tailles au touch. Meilleur
     * predicteur du prochain mid que le mid lui-meme. */
    c->micro = (bs + as > 0) ? (c->bid * as + c->ask * bs) / (bs + as) : c->mid;
    c->imb   = (bs + as > 0) ? (bs - as) / (bs + as) : 0.0;

    if (c->tf > 0) {
        int64_t b = c->ts / (int64_t)(c->tf * 1e9);
        if (!c->ema.init) { c->ema_store = c->mid; c->ema.init = 1; c->bucket = b; }
        else if (b != c->bucket) {     /* un pas de timeframe vient de passer */
            /* comme MarketHL.run() : on incorpore le mid de l'instant */
            c->ema_store += c->alpha_c * (c->mid - c->ema_store);
            c->bucket = b;
        }
        /* comme MarketHL.ema_close() : la valeur lue melange le mid courant */
        c->ema.v = c->blend
                 ? c->alpha_c * c->mid + (1 - c->alpha_c) * c->ema_store
                 : c->ema_store;
    } else {
        ema_push(&c->ema, c->mid, c->dt);
    }

    if (c->i && c->dt > 0 && prev > 0) {
        double r = log(c->mid / prev);
        double v = ema_push(&c->var, r * r / c->dt, c->dt);
        c->vol = sqrt(v > 0 ? v : 0);                /* ecart-type par sqrt(s) */
    }
    c->i++;
}

/* Applique un EmaCfg a un Ctx : un seul endroit ou le choix est fait. */
void ema_cfg_apply(Ctx *c, const EmaCfg *e, double vol_tau)
{
    if (e->tf > 0) ctx_init_candle(c, e->tf, e->window, e->wilder, e->blend, vol_tau);
    else           ctx_init(c, e->tau, vol_tau);
}

void ema_cfg_print(const EmaCfg *e, FILE *f)
{
    if (e->tf > 0)
        fprintf(f, "moyenne        : %s, pas=%.0fs window=%d (alpha=%.4f)\n",
                e->blend ? "EMA melangee au mid courant (comme MarketHL)"
                         : "EMA de clotures, en escalier",
                e->tf, e->window,
                e->wilder ? 1.0 / e->window : 2.0 / (e->window + 1.0));
    else
        fprintf(f, "moyenne        : EMA continue tau=%.0fs "
                   "(demi-vie %.0fs)\n", e->tau, e->tau * 0.6931);
}
