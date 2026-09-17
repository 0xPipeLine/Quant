/* l2.h — moteur de backtest sur carnets L2.
 *
 * Le point central du design : la struct Snap ci-dessous EST l'enregistrement
 * ecrit sur disque, octet pour octet. Ouvrir un fichier .l2 revient donc a
 * mapper le fichier en memoire et a caster : aucun parsing, aucune copie,
 * aucune conversion. C'est ce qui rend le moteur court et rapide.
 *
 * Ce header ne contient QUE le coeur : format, lecture, indicateurs,
 * portefeuille, interface generique d'une strategie et moteur. Les strategies
 * elles-memes vivent dans ../strategies/ et n'exposent rien ici.
 */
#ifndef L2_H
#define L2_H

#include <stdint.h>
#include <inttypes.h>   /* PRId64 : printf portable, y compris MinGW */
#include <stdio.h>
#include <stddef.h>

#if defined(_WIN32) && !defined(_WIN64)
#  warning "Binaire 32 bits : un .l2 de plus de ~1 Go ne pourra pas etre mappe. Installe mingw-w64 x86_64 (voir README)."
#endif

/* fichier utilise par backtest/info/probas quand aucun n'est donne */
#define L2_DEFAULT "data/extracted/xyz-XYZ100.l2"

#define LEVELS 20        /* profondeur du carnet conservee, par cote */
#define MAXQ   64        /* nb max d'ordres par cote pour une strategie */
#define MAXREST (2 * MAXQ)

/* ================== 1. Format du fichier .l2 =============================
 *
 *   [ L2Header : 64 octets ][ Snap #0 : 656 o ][ Snap #1 ] ... [ Snap #N-1 ]
 *
 * Ecrit par bin/extract dans data/extracted/, relisible en Python (voir README).
 */

typedef struct {                    /* 64 octets exactement */
    char     magic[4];              /* "L2v1"                                */
    uint32_t levels;                /* doit valoir LEVELS                    */
    uint32_t rec_size;              /* doit valoir sizeof(Snap)              */
    uint32_t reserved;
    uint64_t n_rec;
    int64_t  t_start, t_end;        /* nanosecondes UTC                      */
    char     market[24];
} L2Header;

typedef struct {                    /* 656 octets exactement */
    int64_t ts;                     /* nanosecondes UTC                      */
    int64_t height;
    double  bid_px[LEVELS];         /* decroissant ; 0 = niveau absent       */
    double  bid_sz[LEVELS];
    double  ask_px[LEVELS];         /* croissant                             */
    double  ask_sz[LEVELS];
} Snap;

/* ================== 2. Lecture ========================================== */

typedef struct {
    L2Header    h;                  /* copie de l'en-tete du fichier         */
    const Snap *s;                  /* fenetre mappee : n snapshots          */
    size_t      n;                  /* snapshots dans la fenetre             */
    size_t      i0;                 /* index du premier, dans le fichier     */
    void  *map_; size_t len_; size_t off_;   /* interne */
#ifdef _WIN32
    void *hf_, *hm_;
#else
    int   fd_;
#endif
} Book;

int    book_open (Book *, const char *path);      /* 0 = ok */
int    book_open_range(Book *, const char *path, const char *from, const char *to);
void   book_close(Book *);
size_t book_seek (const Book *, int64_t ts);      /* 1er index avec ts >= t */
Book   book_slice(const Book *, size_t i0, size_t i1);

/* Formes acceptees par `from` / `to` (NULL = bord du fichier) :
 *     2026-08-21                  date, minuit UTC
 *     2026-08-21T16:30            date + heure (":ss" optionnel)
 *     -3d, -12h, -90m             duree relative a la FIN du fichier
 *     #250000                     index de snapshot
 *     40%                         pourcentage du fichier                 */

void l2_fmt_time(int64_t ts, char *buf, size_t n);

/* Cout reel d'un ordre marche : on marche dans le carnet.
 * side=+1 achat (mange les asks), -1 vente. Renvoie le VWAP obtenu.
 *   snap_walk      : s'arrete quand la liquidite visible est epuisee
 *                    (*filled = quantite reellement obtenue)
 *   snap_walk_full : ce qui depasse les LEVELS niveaux est execute au prix du
 *                    DERNIER niveau visible (convention du mode --reverse)   */
double snap_walk     (const Snap *, int side, double qty, double *filled);
double snap_walk_full(const Snap *, int side, double qty);
double snap_depth    (const Snap *, int side, double frac);

/* ================== 3. Indicateurs incrementaux ========================== */

typedef struct { double tau, v; int init; } Ema;
double ema_push(Ema *, double x, double dt_s);

typedef struct {
    double *v; int64_t *t; int cap, head, count; double sum; int64_t win;
} Sma;
int    sma_init(Sma *, int cap, double win_s);
void   sma_free(Sma *);
double sma_push(Sma *, int64_t ts, double x);

typedef struct {
    const Snap *s;
    int64_t ts;
    double  dt;                     /* secondes depuis le snapshot precedent */
    double  bid, ask, mid, micro, spread, imb, vol;
    Ema     ema;                    /* sur le mid                            */
    Ema     var;                    /* EWMA de r^2/dt -> vol par sqrt(s)     */
    uint64_t i;
    double  tf;                     /* mode bougie : duree (s), 0 = continu  */
    double  alpha_c;
    int64_t bucket;
    double  ema_store;
    int     blend;
} Ctx;

void   ctx_init  (Ctx *, double ema_tau_s, double vol_tau_s);
void   ctx_init_candle(Ctx *, double timeframe_s, int window, int wilder,
                       int blend, double vol_tau_s);
double ema_tau_of(double timeframe_s, int window, int wilder);
void   ctx_update(Ctx *, const Snap *);

typedef struct {
    double tau;                 /* mode continu                              */
    double tf;                  /* mode bougie : duree d'une bougie (s)      */
    int    window, wilder;
    int    blend;               /* 1 : comme MarketHL.ema_close() (defaut)   */
} EmaCfg;

void ema_cfg_apply(Ctx *, const EmaCfg *, double vol_tau_s);
void ema_cfg_print(const EmaCfg *, FILE *);

/* ================== 4. Portefeuille ====================================== */

typedef struct {
    double cash, size;              /* invariant : equity == cash + size*mid */
    double equity, entry;
    double fees, volume;
    double leverage, maker_fee, taker_fee;
    long   fills;
    double eq_max, dd_max;
} Portfolio;

/* ================== 5. Strategie =========================================
 *
 * Une strategie = une fonction qui renvoie l'echelle d'ordres desiree, en
 * connaissant les ordres DEJA en carnet (`rest`). Le moteur fait le reste :
 * matching, frais, comptabilite, diff (prix identique -> ordre conserve).
 *
 * `rest` permet a une strategie d'etre incrementale (ajouter un ordre,
 * deplacer un ordre) plutot que de tout recalculer a chaque appel.
 */

typedef struct { double px, sz; } Quote;   /* sz > 0 : bid ; sz < 0 : ask */

typedef struct {
    const char *name;
    void       *st;
    int (*quotes)(void *st, const Ctx *, const Portfolio *,
                  const Quote *rest, int n_rest, Quote *out, int max);
} Strat;

/* ================== 6. Moteur ============================================ */

typedef enum { FILL_THROUGH = 0, FILL_TOUCH = 1 } FillModel;

typedef struct {
    FillModel fill;
    double maker_fee, taker_fee;
    double leverage, initial;
    double latency_s;
    double poll_s;
    double threshold;
    double warmup_s;
    double mmr;
    double max_jump;
    int    reverse;                 /* 1 : un fill maker declenche un taker
                                       de meme taille dans l'AUTRE sens     */
    const char *equity_csv;
    double equity_every_s;
} Cfg;

typedef struct {
    Portfolio pf;
    long      snaps, requotes, rejected;
    long      liquidations;
    long      bad_prints;
    long      reversed;                    /* fills convertis en taker (--reverse) */
    int       ruined;
    double    occupancy;
    int64_t   t0, t1;
} Result;

Result engine_run(const Book *, Strat *, const Cfg *, const EmaCfg *);

#endif
