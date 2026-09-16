/* backtest.c — lance un backtest de la strategie enveloppe.
 *
 *   ./backtest book.l2 --tau 300 --levels 24 --spread 0.005 --skew 2 \
 *              --alpha 1 --offset 0.0001 --cap 0.85 --min-value 10.5 --inv 1 \
 *              --poll 10.1 --threshold 0.0001 --maker 0.00003 --taker 0.00009 \
 *              --fill through --equity equity.csv --from 2026-08-21 --to -1d
 *
 * Moyenne de reference, deux modes exclusifs :
 *   --tau 740                      EMA continue, mise a jour a chaque snapshot
 *   --timeframe 300 --window 5     EMA "bougie", identique au bot live
 *                                  (ajouter --wilder pour alpha = 1/N)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "l2.h"

int main(int argc, char **argv)
{
    /* MSYS2/MinGW bufferise stdout par blocs quand la sortie passe par un
     * pseudo-terminal : sans ca, un plantage avale tout l'affichage et on ne
     * voit rien du tout. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (argc < 2) { fprintf(stderr, "usage: %s book.l2 [options]\n", argv[0]); return 1; }

    EnvCfg e = { .levels = 24, .spread = 0.005, .offset = 0.0001, .alpha = 1.0,
                 .skew = 2.0, .cap = 0.85, .min_value = 10.5, .inv = 1 };
    GridCfg g = { .levels = 24, .value = 100.5, .delta = 2.0, .gap = 0.6,
                  .cap = 0.80, .min_value = 10.5 };
    int    tp = 0;
    double tp_spread = 0.01;
    const char *strategy = "envelope";
    Cfg cfg = { .fill = FILL_THROUGH, .maker_fee = 0.00003, .taker_fee = 0.00009,
                .leverage = 1.0, .initial = 1000.0, .latency_s = 0.0,
                .poll_s = 10.1, .threshold = 0.0001, .warmup_s = 1800.0,
                .equity_csv = NULL, .equity_every_s = 60.0, .mmr = -1.0,
                .max_jump = 0.10 };
    EmaCfg ema = { .tau = 300.0, .tf = 0.0, .window = 5, .wilder = 0, .blend = 1 };
    const char *from = NULL, *to = NULL;
    int warmup_set = 0;

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if      (!strcmp(a, "--tau")       && v) { ema.tau = atof(argv[++i]); ema.tf = 0; }
        else if (!strcmp(a, "--timeframe") && v) ema.tf         = atof(argv[++i]);
        else if (!strcmp(a, "--window")    && v) ema.window     = atoi(argv[++i]);
        else if (!strcmp(a, "--wilder"))         ema.wilder     = 1;
        else if (!strcmp(a, "--staircase"))      ema.blend      = 0;
        else if (!strcmp(a, "--levels")    && v) e.levels       = atoi(argv[++i]);
        else if (!strcmp(a, "--spread")    && v) e.spread       = atof(argv[++i]);
        else if (!strcmp(a, "--offset")    && v) e.offset       = atof(argv[++i]);
        else if (!strcmp(a, "--alpha")     && v) e.alpha        = atof(argv[++i]);
        else if (!strcmp(a, "--skew")      && v) e.skew         = atof(argv[++i]);
        else if (!strcmp(a, "--cap")       && v) e.cap          = atof(argv[++i]);
        else if (!strcmp(a, "--min-value") && v) e.min_value    = atof(argv[++i]);
        else if (!strcmp(a, "--inv")       && v) e.inv          = atoi(argv[++i]);
        else if (!strcmp(a, "--strategy")  && v) strategy       = argv[++i];
        else if (!strcmp(a, "--tp")        && v) tp             = atoi(argv[++i]);
        else if (!strcmp(a, "--tp-spread") && v) tp_spread      = atof(argv[++i]);
        else if (!strcmp(a, "--value")     && v) g.value        = atof(argv[++i]);
        else if (!strcmp(a, "--delta")     && v) g.delta        = atof(argv[++i]);
        else if (!strcmp(a, "--gap")       && v) g.gap          = atof(argv[++i]);
        else if (!strcmp(a, "--lev")       && v) cfg.leverage   = atof(argv[++i]);
        else if (!strcmp(a, "--mmr")       && v) cfg.mmr        = atof(argv[++i]);
        else if (!strcmp(a, "--max-jump")  && v) cfg.max_jump   = atof(argv[++i]);
        else if (!strcmp(a, "--initial")   && v) cfg.initial    = atof(argv[++i]);
        else if (!strcmp(a, "--maker")     && v) cfg.maker_fee  = atof(argv[++i]);
        else if (!strcmp(a, "--taker")     && v) cfg.taker_fee  = atof(argv[++i]);
        else if (!strcmp(a, "--lat")       && v) cfg.latency_s  = atof(argv[++i]) / 1000.0;
        else if (!strcmp(a, "--poll")      && v) cfg.poll_s     = atof(argv[++i]);
        else if (!strcmp(a, "--threshold") && v) cfg.threshold  = atof(argv[++i]);
        else if (!strcmp(a, "--warmup")    && v) { cfg.warmup_s = atof(argv[++i]);
                                                   warmup_set = 1; }
        else if (!strcmp(a, "--equity")    && v) cfg.equity_csv = argv[++i];
        else if (!strcmp(a, "--from")      && v) from           = argv[++i];
        else if (!strcmp(a, "--to")        && v) to             = argv[++i];
        else if (!strcmp(a, "--fill")      && v) {
            const char *f = argv[++i];
            if      (!strcmp(f, "touch"))   cfg.fill = FILL_TOUCH;
            else if (!strcmp(f, "through")) cfg.fill = FILL_THROUGH;
            else { fprintf(stderr, "--fill : '%s' inconnu, attendu "
                           "'through' ou 'touch'\n", f); return 1; }
        }
        else { fprintf(stderr, "option inconnue : %s\n", a); return 1; }
    }

    /* La moyenne repart de zero au debut de la zone : par defaut on ne trade
     * pas pendant 5 constantes de temps, le temps qu'elle converge. */
    if (!warmup_set) {
        double tau = ema.tf > 0 ? ema_tau_of(ema.tf, ema.window, ema.wilder) : ema.tau;
        cfg.warmup_s = 5 * tau;
    }

    /* marge de maintenance par defaut : la moitie de la marge initiale,
     * comme sur Hyperliquid. A levier 1 elle ne se declenche jamais. */
    if (cfg.mmr < 0) cfg.mmr = 0.5 / cfg.leverage;

    Book b;                                    /* seule la zone est mappee */
    if (book_open_range(&b, argv[1], from, to)) return 1;

    char d0[24], d1[24];
    l2_fmt_time(b.s[0].ts, d0, sizeof d0);
    l2_fmt_time(b.s[b.n - 1].ts, d1, sizeof d1);
    printf("zone           : %s -> %s  (%lu snapshots sur %llu, %.1f%%)\n",
           d0, d1, (unsigned long)b.n, (unsigned long long)b.h.n_rec,
           100.0 * (double)b.n / (double)b.h.n_rec);

    /* les options communes servent aux trois strategies */
    g.levels = e.levels; g.cap = e.cap; g.min_value = e.min_value;
    EnvBCfg eb = { .levels = e.levels, .spread = e.spread, .offset = e.offset,
                   .alpha = e.alpha, .skew = e.skew, .cap = e.cap,
                   .min_value = e.min_value, .inv = e.inv,
                   .tp = tp, .tp_spread = tp_spread };

    Strat s;
    if      (!strcmp(strategy, "envelope"))  s = env_new(&e);
    else if (!strcmp(strategy, "envelopeb")) s = envb_new(&eb);
    else if (!strcmp(strategy, "grid"))      s = grid_new(&g);
    else { fprintf(stderr, "--strategy : '%s' inconnu, attendu "
                   "'envelope', 'envelopeb' ou 'grid'\n", strategy);
           book_close(&b); return 1; }

    printf("strategie      : %s", s.name);
    if (!strcmp(strategy, "envelopeb") && tp)
        printf("  (TP a %.2f%% de l'entree)", tp_spread * 100);
    if (!strcmp(strategy, "grid"))
        printf("  (value=%.1f$ delta=%.3f gap=%.3f)", g.value, g.delta, g.gap);
    printf("\n");
    ema_cfg_print(&ema, stdout);
    Result r = engine_run(&b, &s, &cfg, &ema);
    free(s.st); s.st = NULL;
    book_close(&b);

    double days = (double)(r.t1 - r.t0) / 86400e9;
    double ret  = r.pf.equity / cfg.initial - 1.0;

    printf("modele de fill : %s\n", cfg.fill == FILL_TOUCH ? "TOUCH (optimiste)"
                                                           : "THROUGH (conservateur)");
    printf("snapshots      : %ld sur %.2f jours\n", r.snaps, days);
    printf("equity finale  : %.2f  (%+.2f%%", r.pf.equity, ret * 100);
    if (days >= 7 && ret > -1)                  /* pas d'annualisation sur 1 jour */
        printf(", %+.2f%%/an", (pow(1 + ret, 365.0 / days) - 1) * 100);
    printf(")\n");
    printf("drawdown max   : %.2f%%\n", fmin(r.pf.dd_max, 1.0) * 100);
    if (r.ruined)
        printf("RUINE          : capital epuise, le run s'arrete la\n");
    if (r.bad_prints)
        printf("snaps ecartes  : %ld (saut de mid > %.0f%%, carnet aberrant)\n",
               r.bad_prints, cfg.max_jump * 100);
    printf("fills          : %ld   volume %.0f$   frais %.2f$\n",
           r.pf.fills, r.pf.volume, r.pf.fees);
    printf("requotes       : %ld   (%.1f/jour)\n", r.requotes,
           days > 0 ? r.requotes / days : 0);
    printf("echelle posee  : %.1f%% du temps  (100%% = tous les ordres voulus "
           "sont en carnet)\n", r.occupancy * 100);
    printf("fills          : %.0f/jour   rotation %.1fx du capital\n",
           days > 0 ? r.pf.fills / days : 0, r.pf.volume / cfg.initial);
    if (r.rejected) printf("ordres refuses : %ld (post-only)\n", r.rejected);
    printf("levier         : %.1fx   liquidation si equity < %.0f%% du notionnel\n",
           cfg.leverage, cfg.mmr * 100);
    if (r.liquidations)
        printf("LIQUIDATIONS   : %ld  <-- position soldee de force\n", r.liquidations);
    if (r.pf.volume > 0) {
        double pnl = r.pf.equity - cfg.initial;
        printf("PnL / volume   : %.2f bps\n", pnl / r.pf.volume * 1e4);
        /* ce que coute (ou rapporte) un million de dollars traite : le seul
         * chiffre directement comparable au reel, quel que soit le capital */
        printf("par M$ traite  : %+.0f$ de PnL   dont %.0f$ de frais\n",
               pnl / r.pf.volume * 1e6, -r.pf.fees / r.pf.volume * 1e6);
    }
    return 0;
}
