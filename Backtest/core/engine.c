/* engine.c — la boucle de backtest.
 *
 * Pour chaque snapshot, dans cet ordre :
 *
 *   1. mise a jour des indicateurs (Ctx)
 *   2. MATCHING : les ordres deja poses sont confrontes au nouveau carnet ;
 *      ceux qui sont touches deviennent des fills
 *   3. comptabilite + courbe d'equity
 *   4. la strategie est appelee, avec la liste des ordres encore en carnet,
 *      et renvoie l'echelle d'ordres desiree
 *   5. DIFF : on compare cette echelle aux ordres deja poses. Prix identique
 *      -> l'ordre est conserve tel quel (anciennete gardee). Prix different
 *      -> annule et repose, et il repart pour un tour de latence.
 *
 * L'ordre 2-avant-4 est ce qui empeche le look-ahead.
 *
 * Mode --reverse (cfg->reverse = 1) :
 *   Chaque fois qu'un ordre maker AURAIT ete rempli, on n'execute pas ce fill.
 *   A la place on envoie un ordre taker de la meme taille dans l'AUTRE sens,
 *   en marchant dans le carnet du snapshot courant (snap_walk_full : ce qui
 *   depasse la liquidite visible passe au prix du dernier niveau). Le fill
 *   paye taker_fee. La strategie, elle, ne change pas : elle continue de
 *   poser la meme echelle, c'est le moteur qui inverse l'execution.
 *   Concretement : une echelle "retour a la moyenne" devient une strategie de
 *   momentum — quand le prix vient chercher notre bid, on vend au marche.
 *   La strategie recoit la position MIROIR (size negate) : de son point de
 *   vue ses fills maker ont eu lieu, et ses regles de capacite/TP/INV
 *   restent valides ; la position reelle est simplement l'opposee.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "l2.h"

typedef struct { Quote q; int64_t active; } Rest;   /* ordre pose */

/* Un fill. Toute la comptabilite passe par ici, et seulement par ici. */
static void do_fill(Portfolio *pf, double px, double sz, double fee_rate)
{
    double notional = px * sz;
    double fee = fabs(notional) * fee_rate;

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

/* Vend (ou rachete) toute la position dans le carnet, en taker. */
static void flatten(Portfolio *pf, const Ctx *c)
{
    double got, vwap = snap_walk(c->s, pf->size > 0 ? -1 : 1, fabs(pf->size), &got);
    if (got <= 0) vwap = c->mid;
    double nt = vwap * pf->size;
    pf->cash   += nt - fabs(nt) * pf->taker_fee;
    pf->fees   += fabs(nt) * pf->taker_fee;
    pf->volume += fabs(nt);
    pf->size    = 0;
    pf->equity  = pf->cash;
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
    Quote restq[MAXREST];                /* vue "Quote" de rest[], pour la strat */
    Quote want[MAXREST];
    Rest  keep[MAXREST];

    int64_t lat  = (int64_t)(cfg->latency_s * 1e9);
    int64_t poll = (int64_t)(cfg->poll_s    * 1e9);
    int64_t t0 = 0, next_eq = 0, last_quote = 0;
    double  last_ema = 0;
    int     filled_since_quote = 0;
    double  occ_have = 0, occ_want = 0;
    int     n_want = 0;

    FILE *eq = cfg->equity_csv ? fopen(cfg->equity_csv, "w") : NULL;
    if (eq) fprintf(eq, "ts,equity,size,mid,ema\n");

    double prev_mid = 0; int64_t prev_ts = 0;

    for (size_t k = 0; k < b->n; k++) {
        /* --- 0. sanite du snapshot ---------------------------------- */
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
            if (!hit) { rest[w++] = *o; continue; }

            if (cfg->reverse) {
                /* le fill maker n'a pas lieu : taker de meme taille, sens
                 * oppose, execute dans le carnet courant */
                double sz   = -o->q.sz;
                double vwap = snap_walk_full(c.s, sz > 0 ? 1 : -1, fabs(sz));
                do_fill(pf, vwap, sz, pf->taker_fee);
                R.reversed++;
            } else {
                do_fill(pf, o->q.px, o->q.sz, pf->maker_fee);
            }
            filled_since_quote++;
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

        /* Liquidation. */
        double notional = fabs(pf->size * c.mid);
        if (cfg->mmr > 0 && notional > 0 && pf->equity < cfg->mmr * notional) {
            flatten(pf, &c);
            n_rest = 0;
            R.liquidations++;
        }

        if ((double)(c.ts - t0) * 1e-9 < cfg->warmup_s) continue;  /* chauffe */
        if (pf->equity <= 0) { R.ruined = 1; break; }              /* ruine  */

        if (n_want) { occ_have += n_rest; occ_want += n_want; }

        /* Cadence de re-quote, calquee sur le bot live. */
        if (c.ts - last_quote < poll) continue;
        if (!filled_since_quote && n_rest && last_ema > 0 &&
            fabs(c.ema.v - last_ema) / c.ema.v < cfg->threshold) continue;
        last_quote = c.ts; last_ema = c.ema.v; filled_since_quote = 0;

        /* --- 4. strategie ------------------------------------------- */
        for (int i = 0; i < n_rest; i++) restq[i] = rest[i].q;
        /* En mode reverse la strategie raisonne comme si ses fills maker
         * avaient eu lieu : on lui montre la position MIROIR. Son echelle
         * "qui reduit" reste ainsi celle qui, une fois inversee, reduit la
         * position reelle ; capacite, TP et INV restent coherents. */
        Portfolio view = *pf;
        if (cfg->reverse) view.size = -pf->size;
        int nw = st->quotes(st->st, &c, &view, restq, n_rest, want, MAXREST);
        if (nw > MAXREST) nw = MAXREST;

        /* Garde-fou post-only. */
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
            keep[nk].active = (found >= 0) ? rest[found].active
                                           : c.ts + lat;
            if (found < 0) changed++;
            nk++;
        }
        if (changed || nk != n_rest) R.requotes++;
        memcpy(rest, keep, (size_t)nk * sizeof(Rest));
        n_rest = nk;
        n_want = nw;
    }

    /* Cloture de la position restante DANS LE CARNET, en taker. */
    if (pf->size != 0 && c.s) flatten(pf, &c);
    pf->equity = pf->cash;
    R.occupancy = occ_want > 0 ? occ_have / occ_want : 0.0;
    if (eq) fclose(eq);
    return R;
}
