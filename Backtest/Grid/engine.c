/* engine.c — la boucle de backtest.
 *
 * Pour chaque snapshot, dans cet ordre :
 *
 *   1. mise a jour des indicateurs (Ctx)
 *   2. MATCHING : les ordres deja poses sont confrontes au nouveau carnet ;
 *      ceux qui sont touches deviennent des fills
 *   3. comptabilite + courbe d'equity
 *   4. la strategie est appelee et renvoie l'echelle d'ordres desiree
 *   5. DIFF : on compare cette echelle aux ordres deja poses. Prix identique
 *      -> l'ordre est conserve tel quel (il garde son anciennete, donc sa
 *      place dans la file d'attente). Prix different -> annule et repose,
 *      et il repart pour un tour de latence.
 *
 * L'ordre 2-avant-4 est ce qui empeche le look-ahead : la strategie ne peut
 * jamais reagir a un carnet contre lequel ses ordres n'ont pas deja ete
 * confrontes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "l2.h"

#define MAXREST (2 * MAXQ)

typedef struct { Quote q; int64_t active; } Rest;   /* ordre pose */

/* Un fill. Toute la comptabilite passe par ici, et seulement par ici. */
static void do_fill(Portfolio *pf, double px, double sz)
{
    double notional = px * sz;
    double fee = fabs(notional) * pf->maker_fee;

    if (pf->size == 0 || (pf->size > 0) == (sz > 0))     /* on renforce */
        pf->entry = (pf->entry * fabs(pf->size) + fabs(notional)) /
                    (fabs(pf->size) + fabs(sz));
    else if (fabs(sz) > fabs(pf->size)) pf->entry = px;  /* on retourne */

    pf->cash   -= notional + fee;      /* achat : sz>0 -> le cash sort */
    pf->size   += sz;
    pf->fees   += fee;
    pf->volume += fabs(notional);
    pf->fills++;
}

Result engine_run(const Book *b, Strat *st, const Cfg *cfg, const EmaCfg *ecfg)
{
    Result R; memset(&R, 0, sizeof R);
    Portfolio *pf = &R.pf;
    pf->cash = pf->equity = pf->eq_max = cfg->initial;
    pf->leverage  = cfg->leverage;
    pf->maker_fee = cfg->maker_fee;
    pf->taker_fee = cfg->taker_fee;

    Ctx c; ema_cfg_apply(&c, ecfg, 60.0);
    Rest  rest[MAXREST]; int n_rest = 0;
    Quote want[MAXREST];
    Rest  keep[MAXREST];

    int64_t lat  = (int64_t)(cfg->latency_s * 1e9);
    int64_t poll = (int64_t)(cfg->poll_s    * 1e9);
    int64_t t0 = 0, next_eq = 0, last_quote = 0;
    double  last_ema = 0;
    int     filled_since_quote = 0;
    double  occ_have = 0, occ_want = 0;      /* taux de remplissage de l'echelle */
    int     n_want = 0;

    FILE *eq = cfg->equity_csv ? fopen(cfg->equity_csv, "w") : NULL;
    if (eq) fprintf(eq, "ts,equity,size,mid,ema\n");

    double prev_mid = 0; int64_t prev_ts = 0;

    for (size_t k = 0; k < b->n; k++) {
        /* --- 0. sanite du snapshot ---------------------------------- *
         * Un carnet aberrant (une seule cotation residuelle, un print a
         * -30% qui revient tout de suite) suffit a detruire un backtest a
         * levier. On l'ecarte au lieu de trader dessus, et on le compte.
         * Le filtre ne s'applique qu'entre deux snapshots proches : apres
         * un trou de donnees, un vrai saut de prix est legitime. */
        const Snap *sn = &b->s[k];
        double m = 0.5 * (sn->bid_px[0] + sn->ask_px[0]);
        if (cfg->max_jump > 0 && prev_mid > 0 && m > 0 &&
            (double)(sn->ts - prev_ts) * 1e-9 < 60.0 &&
            fabs(m / prev_mid - 1.0) > cfg->max_jump) {
            if (!R.bad_prints) {
                char t[24]; l2_fmt_time(sn->ts, t, sizeof t);
                fprintf(stderr, "engine: snapshot ecarte le %s : mid %.4f -> "
                        "%.4f (%+.1f%%). Les suivants seront comptes sans "
                        "message (--max-jump pour regler, 0 pour desactiver)\n",
                        t, prev_mid, m, (m / prev_mid - 1.0) * 100);
            }
            R.bad_prints++;
            continue;
        }
        prev_mid = m; prev_ts = sn->ts;

        /* --- 1. indicateurs ---------------------------------------- */
        ctx_update(&c, sn);
        if (!t0) { t0 = R.t0 = c.ts; next_eq = c.ts; }
        R.t1 = c.ts; R.snaps++;

        /* --- 2. matching des ordres poses --------------------------- */
        int w = 0;
        for (int i = 0; i < n_rest; i++) {
            Rest *o = &rest[i];
            if (c.ts < o->active) { rest[w++] = *o; continue; }   /* latence */
            int hit;
            if (o->q.sz > 0)        /* bid  */
                hit = (cfg->fill == FILL_THROUGH) ? (c.ask <= o->q.px)
                                                  : (c.bid <= o->q.px);
            else                    /* ask  */
                hit = (cfg->fill == FILL_THROUGH) ? (c.bid >= o->q.px)
                                                  : (c.ask >= o->q.px);
            if (hit) { do_fill(pf, o->q.px, o->q.sz); filled_since_quote++; }
            else     rest[w++] = *o;
        }
        n_rest = w;

        /* --- 3. comptabilite ---------------------------------------- */
        pf->equity = pf->cash + pf->size * c.mid;      /* le seul invariant */
        if (pf->equity > pf->eq_max) pf->eq_max = pf->equity;
        double dd = pf->eq_max > 0 ? 1.0 - pf->equity / pf->eq_max : 0.0;
        if (dd > pf->dd_max) pf->dd_max = dd;

        if (eq && c.ts >= next_eq) {
            fprintf(eq, "%" PRId64 ",%.4f,%.6f,%.4f,%.4f\n", c.ts,
                    pf->equity, pf->size, c.mid, c.ema.v);
            next_eq = c.ts + (int64_t)(cfg->equity_every_s * 1e9);
        }

        /* Liquidation. Sans ca le backtest garde tranquillement une position
         * que l'exchange aurait soldee de force pendant un krach — c'est
         * exactement le scenario ou une enveloppe perd le plus. */
        double notional = fabs(pf->size * c.mid);
        if (cfg->mmr > 0 && notional > 0 && pf->equity < cfg->mmr * notional) {
            double got, vwap = snap_walk(c.s, pf->size > 0 ? -1 : 1, fabs(pf->size), &got);
            if (got <= 0) vwap = c.mid;
            double nt = vwap * pf->size;
            pf->cash   += nt - fabs(nt) * pf->taker_fee;
            pf->fees   += fabs(nt) * pf->taker_fee;
            pf->volume += fabs(nt);
            pf->size = 0;
            pf->equity = pf->cash;
            n_rest = 0;                       /* les ordres sautent aussi */
            R.liquidations++;
        }

        if ((double)(c.ts - t0) * 1e-9 < cfg->warmup_s) continue;  /* chauffe */
        if (pf->equity <= 0) { R.ruined = 1; break; }              /* ruine  */

        /* Occupation de l'echelle : combien d'ordres sont effectivement poses
         * par rapport a ce que la strategie veut. Un taux bas signifie que le
         * moteur laisse des trous apres les fills -> volume sous-estime. */
        if (n_want) { occ_have += n_rest; occ_want += n_want; }

        /* Cadence de re-quote, calquee sur le bot live :
         *   - au plus une fois par POLL_DELAY ;
         *   - sinon seulement si l'EMA a bouge de plus de THRESHOLD,
         *     OU si des ordres ont ete consommes depuis la derniere fois.
         * Ce dernier cas est le branche `place()` du bot (len(bids) < LEVELS) :
         * sans lui, un niveau rempli reste vide jusqu'au prochain mouvement de
         * la moyenne, ce qui divise le volume par un facteur enorme. */
        if (c.ts - last_quote < poll) continue;
        if (!filled_since_quote && n_rest && last_ema > 0 &&
            fabs(c.ema.v - last_ema) / c.ema.v < cfg->threshold) continue;
        last_quote = c.ts; last_ema = c.ema.v; filled_since_quote = 0;

        /* --- 4. strategie ------------------------------------------- */
        int nw = st->quotes(st->st, &c, pf, want, MAXREST);
        if (nw > MAXREST) nw = MAXREST;
        /* Garde-fou post-only. On refuse l'ordre et on le signale une fois,
         * plutot que d'abandonner le run : un abort() perd tout l'affichage. */
        int keep_n = 0;
        for (int j = 0; j < nw; j++) {
            int ok = (want[j].px > 0) && (want[j].sz != 0) &&
                     (want[j].px == want[j].px) && (want[j].sz == want[j].sz) &&
                     (want[j].sz > 0 ? want[j].px < c.ask : want[j].px > c.bid);
            if (ok) want[keep_n++] = want[j];
            else if (!R.rejected++)
                fprintf(stderr, "engine: ordre refuse (post-only) : px=%.6f "
                        "sz=%.6f alors que bid=%.4f ask=%.4f — "
                        "les suivants seront comptes sans message\n",
                        want[j].px, want[j].sz, c.bid, c.ask);
        }
        nw = keep_n;

        /* --- 5. diff prix a prix ------------------------------------ */
        int nk = 0, changed = 0;
        for (int j = 0; j < nw; j++) {
            int found = -1;
            for (int i = 0; i < n_rest; i++)
                if (rest[i].q.px == want[j].px &&
                    (rest[i].q.sz > 0) == (want[j].sz > 0)) { found = i; break; }
            keep[nk].q      = want[j];
            keep[nk].active = (found >= 0) ? rest[found].active   /* anciennete */
                                           : c.ts + lat;          /* nouveau    */
            if (found < 0) changed++;
            nk++;
        }
        if (changed || nk != n_rest) R.requotes++;
        memcpy(rest, keep, (size_t)nk * sizeof(Rest));
        n_rest = nk;
        n_want = nw;
    }

    /* Cloture de la position restante DANS LE CARNET (cout reel de sortie),
     * pas au mid, et en payant du taker. */
    if (pf->size != 0 && c.s) {
        double filled;
        double vwap = snap_walk(c.s, pf->size > 0 ? -1 : 1, fabs(pf->size), &filled);
        if (filled <= 0) vwap = c.mid;
        double notional = vwap * pf->size;      /* on solde : delta = -size */
        pf->cash   += notional - fabs(notional) * pf->taker_fee;
        pf->fees   += fabs(notional) * pf->taker_fee;
        pf->volume += fabs(notional);
        pf->size = 0;
    }
    pf->equity = pf->cash;
    R.occupancy = occ_want > 0 ? occ_have / occ_want : 0.0;
    if (eq) fclose(eq);
    return R;
}
