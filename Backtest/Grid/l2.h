/* l2.h — moteur de backtest sur carnets L2.
 *
 * Le point central du design : la struct Snap ci-dessous EST l'enregistrement
 * ecrit sur disque, octet pour octet. Ouvrir un fichier .l2 revient donc a
 * mapper le fichier en memoire et a caster : aucun parsing, aucune copie,
 * aucune conversion. C'est ce qui rend le moteur court et rapide.
 */
#ifndef L2_H
#define L2_H

#include <stdint.h>
#include <inttypes.h>   /* PRId64 : printf portable, y compris MinGW */
#include <stdio.h>

#if defined(_WIN32) && !defined(_WIN64)
#  warning "Binaire 32 bits : un .l2 de plus de ~1 Go ne pourra pas etre mappe. Installe mingw-w64 x86_64 (voir README)."
#endif

#define LEVELS 20        /* profondeur du carnet conservee, par cote */
#define MAXQ   64        /* nb max d'ordres par cote pour une strategie */

/* ================== 1. Format du fichier .l2 =============================
 *
 *   [ L2Header : 64 octets ][ Snap #0 : 656 o ][ Snap #1 ] ... [ Snap #N-1 ]
 *
 * Ecrit par l2_convert.py, relisible en Python en deux lignes (voir README).
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

/* Ouvre tout le fichier. */
int    book_open (Book *, const char *path);      /* 0 = ok */

/* Ouvre en ne mappant QUE la zone demandee. Sur un gros fichier c'est ce
 * qu'il faut utiliser : la recherche des bornes se fait par quelques lectures
 * de 8 octets, et seule la fenetre utile occupe de l'espace d'adressage.
 * `from` / `to` : memes formes que book_range ci-dessous, NULL = bord. */
int    book_open_range(Book *, const char *path, const char *from, const char *to);
void   book_close(Book *);
size_t book_seek (const Book *, int64_t ts);      /* 1er index avec ts >= t */

/* Vue sur une tranche [i0, i1) du meme fichier mappe : aucune copie, aucune
 * allocation. La vue se referme toute seule (book_close dessus ne fait rien),
 * seul le Book d'origine doit etre ferme. */
Book book_slice(const Book *, size_t i0, size_t i1);

/* Formes acceptees par `from` / `to` (NULL = bord du fichier) :
 *     2026-08-21                  date, minuit UTC
 *     2026-08-21T16:30            date + heure (":ss" optionnel)
 *     -3d, -12h, -90m             duree relative a la FIN du fichier
 *     #250000                     index de snapshot
 *     40%                         pourcentage du fichier                 */

/* "2026-08-21T16:46:53" dans buf (au moins 20 octets) */
void l2_fmt_time(int64_t ts, char *buf, size_t n);

/* Cout reel d'un ordre marche : on marche dans le carnet.
 * side=+1 achat (mange les asks), -1 vente. Renvoie le VWAP obtenu. */
double snap_walk (const Snap *, int side, double qty, double *filled);
/* Liquidite cumulee a moins de frac du mid (ex 0.001 = 10 bps). */
double snap_depth(const Snap *, int side, double frac);

/* ================== 3. Indicateurs incrementaux ========================== */

/* EMA a decroissance temporelle : alpha = 1 - exp(-dt/tau), tau en secondes.
 * Indispensable : les snapshots ne sont pas equidistants. */
typedef struct { double tau, v; int init; } Ema;
double ema_push(Ema *, double x, double dt_s);

typedef struct {                    /* SMA sur fenetre temporelle glissante */
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
    /* mode bougie (voir ctx_init_candle) : 0 = EMA continue */
    double  tf;                     /* duree d'une bougie, en secondes       */
    double  alpha_c;                /* coefficient applique a chaque pas     */
    int64_t bucket;                 /* index de la bougie en cours           */
    double  ema_store;              /* EMA memorisee, mise a jour au pas     */
    int     blend;                  /* 1 : melange avec le mid courant       */
} Ctx;

/* EMA continue : alpha = 1 - exp(-dt/tau), mise a jour a chaque snapshot. */
void ctx_init  (Ctx *, double ema_tau_s, double vol_tau_s);

/* EMA "bougie" : reproduit exactement un ema_close(window) sur un timeframe.
 * La moyenne ne bouge qu'a la cloture de chaque bougie, et la valeur utilisee
 * pendant la bougie i ne contient que les clotures jusqu'a i-1 — c'est-a-dire
 * le meme retard d'une bougie que le bot en production.
 *   wilder = 0 : alpha = 2/(N+1)   (convention "span", pandas ewm)
 *   wilder = 1 : alpha = 1/N       (RMA / SMMA, convention Wilder)          */
/* blend = 1 : reproduit MarketHL.ema_close() de ton bot, c'est-a-dire
 *   ref = alpha*mid_courant + (1-alpha)*ema_memorisee
 * ou ema_memorisee est reactualisee tous les `timeframe_s` avec le mid de
 * l'instant. Le centre de la grille suit donc le mid en permanence.
 * blend = 0 : EMA de clotures classique, en escalier. */
void ctx_init_candle(Ctx *, double timeframe_s, int window, int wilder,
                     int blend, double vol_tau_s);

/* tau equivalent a (window, timeframe), pour information/affichage */
double ema_tau_of(double timeframe_s, int window, int wilder);

void ctx_update(Ctx *, const Snap *);

/* ================== 4. Portefeuille ====================================== */

typedef struct {
    double cash, size;              /* invariant : equity == cash + size*mid */
    double equity, entry;
    double fees, volume;
    double leverage, maker_fee, taker_fee;
    long   fills;
    double eq_max, dd_max;          /* drawdown relatif max                  */
} Portfolio;

/* ================== 5. Strategie =========================================
 *
 * Une strategie = une fonction qui renvoie l'echelle d'ordres desiree.
 * Le moteur fait tout le reste : matching, frais, comptabilite, et le diff
 * avec les ordres deja poses (prix inchange -> ordre conserve, donc place
 * dans la file d'attente conservee).
 */

typedef struct { double px, sz; } Quote;   /* sz > 0 : bid ; sz < 0 : ask */

typedef struct {
    const char *name;
    void       *st;
    int (*quotes)(void *st, const Ctx *, const Portfolio *, Quote *out, int max);
} Strat;

/* ================== 6. Moteur ============================================ */

typedef enum { FILL_THROUGH = 0, FILL_TOUCH = 1 } FillModel;

typedef struct {
    FillModel fill;
    double maker_fee, taker_fee;
    double leverage, initial;
    double latency_s;               /* delai avant qu'un ordre pose soit actif */
    double poll_s;                  /* delai min entre deux re-quotes          */
    double threshold;               /* variation relative min de l'EMA         */
    double warmup_s;                /* pas de trading avant (chauffe de l'EMA) */
    double mmr;                     /* marge de maintenance : liquidation si
                                       equity < mmr * |notionnel| ; 0 = jamais */
    double max_jump;                /* saut de mid juge aberrant entre deux
                                       snapshots proches ; 0 = pas de filtre  */
    const char *equity_csv;
    double equity_every_s;
} Cfg;

typedef struct {
    Portfolio pf;
    long      snaps, requotes, rejected;   /* rejected : ordres non post-only */
    long      liquidations;                /* positions soldees de force      */
    long      bad_prints;                  /* snapshots ecartes (saut aberrant)*/
    int       ruined;                      /* equity tombee a zero            */
    double    occupancy;                   /* part de l'echelle reellement posee */
    int64_t   t0, t1;
} Result;

/* Parametrage de la moyenne de reference, partage par le backtest et les
 * probabilites. tf <= 0 -> EMA continue de constante tau. */
typedef struct {
    double tau;                 /* mode continu                              */
    double tf;                  /* mode bougie : duree d'une bougie (s)      */
    int    window, wilder;      /* mode bougie : fenetre et convention       */
    int    blend;               /* 1 : comme MarketHL.ema_close() (defaut)   */
} EmaCfg;

void   ema_cfg_apply(Ctx *, const EmaCfg *, double vol_tau_s);
void   ema_cfg_print(const EmaCfg *, FILE *);

Result engine_run(const Book *, Strat *, const Cfg *, const EmaCfg *);

/* ================== 7. Strategie fournie : enveloppe ===================== */

typedef struct {
    int    levels;      /* nb d'ordres par cote                              */
    double spread;      /* distance totale premier -> dernier ordre (frac)   */
    double offset;      /* decalage du 1er ordre, en fraction d'un palier    */
    double alpha;       /* espacement geometrique (>1 : de + en + espace)    */
    double skew;        /* ratio de taille dernier / premier ordre           */
    double cap;         /* fraction max du portefeuille utilisee             */
    double min_value;   /* notionnel minimum d'un ordre, en $                */
    int    inv;         /* 1 : ne quote que le cote du retour a la moyenne   */
} EnvCfg;

Strat env_new (const EnvCfg *);
void  env_free(Strat *);

/* ---- enveloppeB : la meme, plus un take profit maker ---- */
typedef struct {
    int    levels;
    double spread, offset, alpha, skew, cap, min_value;
    int    inv;
    int    tp;            /* 1 : poser un TP de la taille de la position     */
    double tp_spread;     /* distance au prix d'entree (0.01 = 1%)           */
} EnvBCfg;

Strat envb_new (const EnvBCfg *);
void  envb_free(Strat *);

/* ---- grille a pas absolu, ancree au prix de depart (grid.py) ---- */
typedef struct {
    int    levels;        /* nb de niveaux par cote                          */
    double value;         /* notionnel fixe par ordre, en $                  */
    double delta;         /* pas entre deux niveaux, en unites de PRIX       */
    double gap;           /* ecart total bid/ask au centre, en unites de PRIX*/
    double cap;
    double min_value;
} GridCfg;

Strat grid_new (const GridCfg *);
void  grid_free(Strat *);

#endif
