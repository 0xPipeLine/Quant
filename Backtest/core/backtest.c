/* backtest.c — lance un backtest.
 *
 *   bin/backtest data/extracted/xyz-XYZ100.l2 --strategy envelope --tau 300 --reverse 0 \
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
#include "parameters.h"
#include "strategy.h"

/* 1510944 -> "1 510 944" */
static void fmt_thousands(double v, char *buf, size_t n)
{
    char raw[32]; snprintf(raw, sizeof raw, "%.0f", fabs(v));
    size_t L = strlen(raw), k = 0;
    if (v < 0 && k < n - 1) buf[k++] = '-';
    for (size_t i = 0; i < L && k < n - 1; i++) {
        if (i && (L - i) % 3 == 0) buf[k++] = ' ';
        buf[k++] = raw[i];
    }
    buf[k] = 0;
}

static void usage(const char *prog)
{
    fprintf(stderr,
        "usage: %s [book.l2] [options]     (defaut : " L2_DEFAULT ")\n"
        "       %s --list             strategies disponibles\n"
        "       %s --help <strategie> options d'une strategie\n"
        "options du moteur (defauts dans core/parameters.h) :\n"
        "  --strategy NOM   --reverse 0|1\n"
        "  --tau S  |  --timeframe S --window N [--wilder] [--staircase]\n"
        "  --fill through|touch  --maker F  --taker F\n"
        "  --lev F  --mmr F  --initial $\n"
        "  --poll S  --threshold F  --lat MS  --warmup S\n"
        "  --max-jump F  --equity FILE  --from BORNE  --to BORNE\n",
        prog, prog, prog);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (argc >= 2 && !strcmp(argv[1], "--list")) {
        printf("strategies :\n"); strat_list(stdout); return 0;
    }
    if (argc >= 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) && argc < 3) {
        usage(argv[0]); return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "--help")) {
        const StratDef *d = strat_find(argv[2]);
        if (!d) { fprintf(stderr, "strategie inconnue : %s\n", argv[2]); return 1; }
        printf("%s — %s\n%s", d->name, d->summary, d->help); return 0;
    }
    /* le fichier est optionnel : sans lui, ou si argv[1] est une option,
     * on prend L2_DEFAULT et on parse les options a partir de argv[1] */
    const char *path = L2_DEFAULT;
    int first = 1;
    if (argc >= 2 && strncmp(argv[1], "--", 2)) { path = argv[1]; first = 2; }

    /* --- 1. quelle strategie ? (premiere passe) ------------------------ */
    const char *strategy = "envelope";
    for (int i = first; i + 1 < argc; i++)
        if (!strcmp(argv[i], "--strategy")) strategy = argv[i + 1];
    const StratDef *def = strat_find(strategy);
    if (!def) {
        fprintf(stderr, "--strategy : '%s' inconnu. Disponibles :\n", strategy);
        strat_list(stderr); return 1;
    }
    Strat s = def->create();

    /* --- 2. options du moteur, puis de la strategie -------------------- */
    Cfg cfg = { .fill = DEF_FILL, .maker_fee = DEF_MAKER_FEE, .taker_fee = DEF_TAKER_FEE,
                .leverage = DEF_LEVERAGE, .initial = DEF_INITIAL,
                .latency_s = DEF_LATENCY_MS / 1000.0,
                .poll_s = DEF_POLL_S, .threshold = DEF_THRESHOLD, .warmup_s = 0,
                .mmr = DEF_MMR, .max_jump = DEF_MAX_JUMP, .reverse = DEF_REVERSE,
                .equity_csv = NULL, .equity_every_s = DEF_EQUITY_EVERY_S };
    EmaCfg ema = { .tau = DEF_EMA_TAU, .tf = DEF_EMA_TF, .window = DEF_EMA_WINDOW,
                   .wilder = DEF_EMA_WILDER, .blend = DEF_EMA_BLEND };
    const char *from = NULL, *to = NULL;
    int warmup_set = 0;

    for (int i = first; i < argc; i++) {
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
        cfg.warmup_s = DEF_WARMUP_TAU * tau;
    }
    if (cfg.mmr < 0) cfg.mmr = 0.5 / cfg.leverage;

    /* --- 3. donnees ---------------------------------------------------- */
    Book b;
    if (book_open_range(&b, path, from, to)) return 1;
    printf("fichier        : %s\n", path);

    /* --- 4. run -------------------------------------------------------- */
    Result r = engine_run(&b, &s, &cfg, &ema);

    double days = (double)(r.t1 - r.t0) / 86400e9;
    double ret  = r.pf.equity / cfg.initial - 1.0;
    double pnl  = r.pf.equity - cfg.initial;
    char d0[24], d1[24], vol[32];
    l2_fmt_time(b.s[0].ts, d0, sizeof d0);
    l2_fmt_time(b.s[b.n - 1].ts, d1, sizeof d1);
    fmt_thousands(r.pf.volume, vol, sizeof vol);

    printf("zone           : %s -> %s (%.2f jours) (%lu snapshots sur %llu, %.1f%%)\n",
           d0, d1, days, (unsigned long)b.n, (unsigned long long)b.h.n_rec,
           100.0 * (double)b.n / (double)b.h.n_rec);
    book_close(&b);
    printf("strategie      : %s%s\n", s.name,
           cfg.reverse ? "  [REVERSE : fills maker -> takers inverses]" : "");
    def->print(s.st, stdout);
    free(s.st); s.st = NULL;
    printf("parametres generaux\n"
           "  fill=%s reverse=%d lev=%g mmr=%.4f initial=%g maker=%.5f taker=%.5f\n"
           "  poll=%gs threshold=%g lat=%gms warmup=%gs max-jump=%g\n",
           cfg.fill == FILL_TOUCH ? "touch" : "through", cfg.reverse,
           cfg.leverage, cfg.mmr, cfg.initial, cfg.maker_fee, cfg.taker_fee,
           cfg.poll_s, cfg.threshold, cfg.latency_s * 1000, cfg.warmup_s, cfg.max_jump);
    ema_cfg_print(&ema, stdout);

    printf("requotes       : %ld   (%.1f/jour)\n", r.requotes,
           days > 0 ? r.requotes / days : 0);
    printf("fills          : %ld (%.0f/jour)\n", r.pf.fills,
           days > 0 ? r.pf.fills / days : 0);
    if (cfg.reverse)
        printf("fills inverses : %ld  (chacun execute en taker, sens oppose)\n", r.reversed);
    if (r.rejected)   printf("ordres refuses : %ld (post-only)\n", r.rejected);
    if (r.bad_prints) printf("snaps ecartes  : %ld (saut de mid > %.0f%%)\n",
                             r.bad_prints, cfg.max_jump * 100);
    if (r.occupancy < 0.99)
        printf("echelle posee  : %.1f%% du temps  (trous apres les fills : volume "
               "sous-estime)\n", r.occupancy * 100);
    printf("drawdown max   : %.2f%%\n", fmin(r.pf.dd_max, 1.0) * 100);
    if (r.liquidations)
        printf("LIQUIDATIONS   : %ld  <-- position soldee de force (equity < %.1f%% "
               "du notionnel)\n", r.liquidations, cfg.mmr * 100);
    if (r.ruined)
        printf("RUINE          : capital epuise, le run s'arrete la\n");
    printf("rotation       : %.1fx du capital (%.1fx/jour)\n",
           r.pf.volume / cfg.initial, days > 0 ? r.pf.volume / cfg.initial / days : 0);
    printf("equity finale  : %.2f$  (%+.2f%%", r.pf.equity, ret * 100);
    if (days >= 7 && ret > -1)
        printf(", %+.2f%%/an", (pow(1 + ret, 365.0 / days) - 1) * 100);
    printf(")\n");
    printf("PnL            : %+.2f$\n", pnl);
    printf("frais          : %.2f$\n", r.pf.fees);
    printf("volume         : %s$\n", vol);
    if (r.pf.volume > 0)
        printf("par M$ traite  : %+.0f$ de PnL   dont %.0f$ de frais\n",
               pnl / r.pf.volume * 1e6, -r.pf.fees / r.pf.volume * 1e6);
    return 0;
}
