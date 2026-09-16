/* probabilities.c — probabilites "fill a D% de la moyenne".
 *
 * On ne simule pas une strategie, on observe le marche. Pour chaque distance
 * D d'une grille logarithmique et pour chaque cote, on suit un ordre passif
 * pose en permanence a ref*(1 -/+ D), ref = EMA (ou SMA avec --sma).
 *
 *      arme  --(fill)-->  ouvert  --(retour a la moyenne | stop | horizon)--> agrege
 *
 * Trois precautions cablees :
 *   1. un seul episode ouvert a la fois par (cote, D) ;
 *   2. pas de look-ahead : le niveau vient de la moyenne du snapshot
 *      PRECEDENT, le fill est teste sur le snapshot courant ;
 *   3. les episodes non resolus a l'horizon sont comptes (p_timeout).
 *
 *   bin/probas Data/l2/US500.l2 --tau 300 --nd 40 --dmin 0.0002 --dmax 0.01 \
 *              --horizon 3600 --out study.csv [--dump episodes.csv] [--sma]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "l2.h"

typedef struct {
    double  d;
    int     open;
    int64_t t0;
    double  px0, mae, mfe;
    long    n_ep, n_ret, n_stop, n_to;
    double  s_ttr, s_pnl, s_mae, s_mfe, s_ret_pnl;
} Track;

static void close_ep(Track *t, int64_t ts, double pnl, int kind, FILE *d, int side)
{
    double ttr = (double)(ts - t->t0) * 1e-9;
    t->n_ep++;
    t->s_ttr += ttr; t->s_pnl += pnl; t->s_mae += t->mae; t->s_mfe += t->mfe;
    if      (kind == 0) { t->n_ret++; t->s_ret_pnl += pnl; }
    else if (kind == 1)   t->n_stop++;
    else                  t->n_to++;
    if (d) fprintf(d, "%d,%.6f,%" PRId64 ",%.2f,%.4f,%.2f,%.2f,%.2f,%d\n",
                   side, t->d, t->t0, ttr, t->px0, pnl, t->mae, t->mfe, kind);
    t->open = 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (argc < 2) { fprintf(stderr, "usage: %s book.l2 [options]\n", argv[0]); return 1; }

    EmaCfg ema = { .tau = 300.0, .tf = 0.0, .window = 5, .wilder = 0, .blend = 1 };
    double dmin = 0.0002, dmax = 0.01, horizon = 3600, stop = 0;
    int nd = 40, use_sma = 0;
    const char *out = "study.csv", *dumpf = NULL, *from = NULL, *to = NULL;

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i], *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if      (!strcmp(a, "--tau")     && v) { ema.tau = atof(argv[++i]); ema.tf = 0; }
        else if (!strcmp(a, "--timeframe") && v) ema.tf     = atof(argv[++i]);
        else if (!strcmp(a, "--window")    && v) ema.window = atoi(argv[++i]);
        else if (!strcmp(a, "--wilder"))         ema.wilder = 1;
        else if (!strcmp(a, "--staircase"))      ema.blend  = 0;
        else if (!strcmp(a, "--dmin")    && v) dmin    = atof(argv[++i]);
        else if (!strcmp(a, "--dmax")    && v) dmax    = atof(argv[++i]);
        else if (!strcmp(a, "--nd")      && v) nd      = atoi(argv[++i]);
        else if (!strcmp(a, "--horizon") && v) horizon = atof(argv[++i]);
        else if (!strcmp(a, "--stop")    && v) stop    = atof(argv[++i]);
        else if (!strcmp(a, "--out")     && v) out     = argv[++i];
        else if (!strcmp(a, "--dump")    && v) dumpf   = argv[++i];
        else if (!strcmp(a, "--from")    && v) from    = argv[++i];
        else if (!strcmp(a, "--to")      && v) to      = argv[++i];
        else if (!strcmp(a, "--sma"))          use_sma = 1;
        else { fprintf(stderr, "option inconnue : %s\n", a); return 1; }
    }

    Book b;
    if (book_open_range(&b, argv[1], from, to)) return 1;
    { char d0[24], d1[24];
      l2_fmt_time(b.s[0].ts, d0, sizeof d0);
      l2_fmt_time(b.s[b.n - 1].ts, d1, sizeof d1);
      fprintf(stderr, "zone           : %s -> %s  (%lu snapshots)\n",
              d0, d1, (unsigned long)b.n); }

    Ctx c; ema_cfg_apply(&c, &ema, 60.0);
    ema_cfg_print(&ema, stderr);
    double tau = c.ema.tau;
    Sma sma; sma_init(&sma, 1 << 20, tau);

    Track *buy  = calloc((size_t)nd, sizeof(Track));
    Track *sell = calloc((size_t)nd, sizeof(Track));
    for (int j = 0; j < nd; j++)
        buy[j].d = sell[j].d = nd > 1
                 ? dmin * pow(dmax / dmin, (double)j / (nd - 1)) : dmin;

    FILE *dump = dumpf ? fopen(dumpf, "w") : NULL;
    if (dump) fprintf(dump, "side,d,t0_ns,ttr_s,px0,pnl_bps,mae_bps,mfe_bps,kind\n");

    int64_t hz   = (int64_t)(horizon * 1e9);
    int64_t warm = (int64_t)(5 * tau * 1e9);
    int64_t t0 = b.n ? b.s[0].ts : 0, t1 = t0;
    double  ref_prev = 0;

    for (size_t k = 0; k < b.n; k++) {
        ctx_update(&c, &b.s[k]);
        t1 = c.ts;
        double sm = sma_push(&sma, c.ts, c.mid);
        double ref_now = use_sma ? sm : c.ema.v;
        double ref = ref_prev;
        ref_prev = ref_now;
        if (c.ts - t0 < warm || ref <= 0) continue;

        for (int j = 0; j < nd; j++) {
            Track *t = &buy[j];
            double lvl = ref * (1.0 - t->d);
            if (!t->open) {
                if (c.ask <= lvl) {
                    t->open = 1; t->t0 = c.ts; t->px0 = lvl; t->mae = t->mfe = 0;
                }
            } else {
                double pnl = (c.mid - t->px0) / t->px0 * 1e4;
                if (pnl < t->mae) t->mae = pnl;
                if (pnl > t->mfe) t->mfe = pnl;
                if (c.mid >= ref)
                    close_ep(t, c.ts, (ref - t->px0) / t->px0 * 1e4, 0, dump, 1);
                else if (stop > 0 && pnl <= -stop)
                    close_ep(t, c.ts, pnl, 1, dump, 1);
                else if (c.ts - t->t0 > hz)
                    close_ep(t, c.ts, pnl, 2, dump, 1);
            }

            t = &sell[j];
            lvl = ref * (1.0 + t->d);
            if (!t->open) {
                if (c.bid >= lvl) {
                    t->open = 1; t->t0 = c.ts; t->px0 = lvl; t->mae = t->mfe = 0;
                }
            } else {
                double pnl = (t->px0 - c.mid) / t->px0 * 1e4;
                if (pnl < t->mae) t->mae = pnl;
                if (pnl > t->mfe) t->mfe = pnl;
                if (c.mid <= ref)
                    close_ep(t, c.ts, (t->px0 - ref) / t->px0 * 1e4, 0, dump, -1);
                else if (stop > 0 && pnl <= -stop)
                    close_ep(t, c.ts, pnl, 1, dump, -1);
                else if (c.ts - t->t0 > hz)
                    close_ep(t, c.ts, pnl, 2, dump, -1);
            }
        }
    }

    double days = (double)(t1 - t0) / 86400e9;
    FILE *o = fopen(out, "w");
    fprintf(o, "side,dist_bps,n_ep,fills_per_day,p_return,p_stop,p_timeout,"
               "mean_ttr_s,mean_pnl_bps,pnl_if_return_bps,mean_mae_bps,mean_mfe_bps\n");
    for (int k = 0; k < 2; k++) {
        Track *T = k ? sell : buy;
        for (int j = 0; j < nd; j++) {
            Track *t = &T[j];
            double n = t->n_ep ? (double)t->n_ep : 1.0;
            fprintf(o, "%s,%.2f,%ld,%.3f,%.4f,%.4f,%.4f,%.1f,%.3f,%.3f,%.3f,%.3f\n",
                    k ? "sell" : "buy", t->d * 1e4, t->n_ep,
                    days > 0 ? t->n_ep / days : 0,
                    t->n_ep ? t->n_ret / n : 0, t->n_ep ? t->n_stop / n : 0,
                    t->n_ep ? t->n_to / n : 0, t->s_ttr / n, t->s_pnl / n,
                    t->n_ret ? t->s_ret_pnl / t->n_ret : 0,
                    t->s_mae / n, t->s_mfe / n);
        }
    }
    fclose(o);
    if (dump) fclose(dump);
    fprintf(stderr, "%.2f jours -> %s\n", days, out);

    sma_free(&sma); free(buy); free(sell); book_close(&b);
    return 0;
}
