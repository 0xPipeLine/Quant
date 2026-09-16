/* backtest.c — lance un backtest.
 *
 *   bin/backtest data/extracted/mkts-US500.l2 --strategy envelope --tau 300 --reverse 0 \
 *                --levels 24 --spread 0.005 ... --from 2026-08-21 --to -1d
 *
 * Ce fichier ne connait aucune strategie : les options qu'il ne reconnait
 * pas sont proposees a la strategie choisie (--strategy, ou envelope par
 * defaut). `bin/backtest --list` enumere les strategies, `--help <nom>`
 * affiche leurs options.
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
#include "strategy.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s book.l2 [options]\n"
        "       %s --list             strategies disponibles\n"
        "       %s --help <strategie> options d'une strategie\n"
        "options du moteur (defaut entre parentheses) :\n"
        "  --strategy NOM   (envelope)       --reverse 0|1     (0)\n"
        "  --tau S          (300)            --timeframe S --window N [--wilder] [--staircase]\n"
        "  --fill through|touch (through)    --maker F (0.00003)  --taker F (0.00009)\n"
        "  --lev F (1)  --mmr F (0.5/lev)    --initial $ (1000)\n"
        "  --poll S (10.1)  --threshold F (0.0001)  --lat MS (0)  --warmup S (5*tau)\n"
        "  --max-jump F (0.10)  --equity FILE  --from BORNE  --to BORNE\n",
        prog, prog, prog);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (argc >= 2 && !strcmp(argv[1], "--list")) {
        printf("strategies :\n"); strat_list(stdout); return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--help")) {
        const StratDef *d = strat_find(argv[2]);
        if (!d) { fprintf(stderr, "strategie inconnue : %s\n", argv[2]); return 1; }
        printf("%s — %s\n%s", d->name, d->summary, d->help); return 0;
    }
    if (argc < 2) { usage(argv[0]); return 1; }

    /* --- 1. quelle strategie ? (premiere passe) ------------------------ */
    const char *strategy = "envelope";
    for (int i = 2; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--strategy")) strategy = argv[i + 1];
    const StratDef *def = strat_find(strategy);
    if (!def) {
        fprintf(stderr, "--strategy : '%s' inconnu. Disponibles :\n", strategy);
        strat_list(stderr); return 1;
    }
    Strat s = def->create();

    /* --- 2. options du moteur, puis de la strategie -------------------- */
    Cfg cfg = { .fill = FILL_THROUGH, .maker_fee = 0.00003, .taker_fee = 0.00009,
                .leverage = 1.0, .initial = 1000.0, .latency_s = 0.0,
                .poll_s = 10.1, .threshold = 0.0001, .warmup_s = 1800.0,
                .mmr = -1.0, .max_jump = 0.10, .reverse = 0,
                .equity_csv = NULL, .equity_every_s = 60.0 };
    EmaCfg ema = { .tau = 300.0, .tf = 0.0, .window = 5, .wilder = 0, .blend = 1 };
    const char *from = NULL, *to = NULL;
    int warmup_set = 0;

    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if      (!strcmp(a, "--strategy")  && v) i++;
        else if (!strcmp(a, "--tau")       && v) { ema.tau = atof(argv[++i]); ema.tf = 0; }
        else if (!strcmp(a, "--timeframe") && v) ema.tf         = atof(argv[++i]);
        else if (!strcmp(a, "--window")    && v) ema.window     = atoi(argv[++i]);
        else if (!strcmp(a, "--wilder"))         ema.wilder     = 1;
        else if (!strcmp(a, "--staircase"))      ema.blend      = 0;
        else if (!strcmp(a, "--reverse")   && v) cfg.reverse    = atoi(argv[++i]);
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
        else {
            int r = def->option(s.st, a, v);
            if (r == 2) i++;
            else if (r == 0) {
                fprintf(stderr, "option inconnue : %s\n(options de %s :)\n%s",
                        a, def->name, def->help);
                return 1;
            }
        }
    }
    def->init(s.st);

    if (!warmup_set) {
        double tau = ema.tf > 0 ? ema_tau_of(ema.tf, ema.window, ema.wilder) : ema.tau;
        cfg.warmup_s = 5 * tau;
    }
    if (cfg.mmr < 0) cfg.mmr = 0.5 / cfg.leverage;

    /* --- 3. donnees ---------------------------------------------------- */
    Book b;
    if (book_open_range(&b, argv[1], from, to)) return 1;

    char d0[24], d1[24];
    l2_fmt_time(b.s[0].ts, d0, sizeof d0);
    l2_fmt_time(b.s[b.n - 1].ts, d1, sizeof d1);
    printf("zone           : %s -> %s  (%lu snapshots sur %llu, %.1f%%)\n",
           d0, d1, (unsigned long)b.n, (unsigned long long)b.h.n_rec,
           100.0 * (double)b.n / (double)b.h.n_rec);
    printf("strategie      : %s%s\n", s.name,
           cfg.reverse ? "  [REVERSE : fills maker -> takers inverses]" : "");
    def->print(s.st, stdout);
    ema_cfg_print(&ema, stdout);

    /* --- 4. run -------------------------------------------------------- */
    Result r = engine_run(&b, &s, &cfg, &ema);
    free(s.st); s.st = NULL;
    book_close(&b);

    double days = (double)(r.t1 - r.t0) / 86400e9;
    double ret  = r.pf.equity / cfg.initial - 1.0;

    printf("modele de fill : %s\n", cfg.fill == FILL_TOUCH ? "TOUCH (optimiste)"
                                                           : "THROUGH (conservateur)");
    printf("snapshots      : %ld sur %.2f jours\n", r.snaps, days);
    printf("equity finale  : %.2f  (%+.2f%%", r.pf.equity, ret * 100);
    if (days >= 7 && ret > -1)
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
    if (cfg.reverse)
        printf("fills inverses : %ld  (chacun execute en taker, sens oppose)\n",
               r.reversed);
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
        printf("par M$ traite  : %+.0f$ de PnL   dont %.0f$ de frais\n",
               pnl / r.pf.volume * 1e6, -r.pf.fees / r.pf.volume * 1e6);
    }
    return 0;
}
