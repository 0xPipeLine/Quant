/* extract.c — *.json.gz  ->  book.l2
 *
 *   bin/extract Data/raw/US500 Data/l2/US500.l2 [--jobs N] [--market NOM]
 *
 * Pourquoi c'est rapide :
 *   - parseur JSON dedie au schema ;
 *   - conversion decimale maison, pas de strtod ;
 *   - zero allocation par snapshot ;
 *   - decompression et parsing en parallele (OpenMP), ecriture sequentielle.
 *
 * Compilation : necessite zlib (-lz).
 *   MSYS2/MinGW : pacman -S mingw-w64-x86_64-zlib
 *   Debian/Ubuntu : apt install zlib1g-dev
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>
#include "l2.h"

#ifdef _OPENMP
#  include <omp.h>
#endif

/* ==================== 1. Liste des fichiers ==================== */

typedef struct { char **v; size_t n, cap; } Paths;

static void paths_add(Paths *p, const char *s)
{
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 1024;
        p->v = realloc(p->v, p->cap * sizeof(char *));
    }
    p->v[p->n++] = strdup(s);
}

static unsigned long long base_num(const char *path)
{
    const char *b = path;
    for (const char *q = path; *q; q++)
        if (*q == '/' || *q == '\\') b = q + 1;
    return strtoull(b, NULL, 10);
}

static int cmp_path(const void *a, const void *b)
{
    const char *x = *(char *const *)a, *y = *(char *const *)b;
    unsigned long long nx = base_num(x), ny = base_num(y);
    if (nx != ny) return nx < ny ? -1 : 1;
    return strcmp(x, y);
}

static void walk(const char *dir, Paths *out)
{
    DIR *d = opendir(dir);
    if (!d) { fprintf(stderr, "opendir %s\n", dir); return; }
    struct dirent *e;
    char path[4096];
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) walk(path, out);
        else {
            size_t L = strlen(e->d_name);
            if ((L > 8 && !strcmp(e->d_name + L - 8, ".json.gz")) ||
                (L > 5 && !strcmp(e->d_name + L - 5, ".json")))
                paths_add(out, path);
        }
    }
    closedir(d);
}

/* ==================== 2. Conversions bas niveau ==================== */

static const double P10[] = { 1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8 };

static double dec(const char *s)
{
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    unsigned long long m = 0;
    int e = 0;
    while (*s >= '0' && *s <= '9') m = m * 10 + (unsigned)(*s++ - '0');
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9' && e < 8) { m = m * 10 + (unsigned)(*s++ - '0'); e++; }
    }
    double v = (double)m / P10[e];
    return neg ? -v : v;
}

static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int64_t parse_ts(const char *s)
{
    int Y = atoi(s), M = atoi(s + 5), D = atoi(s + 8);
    int h = atoi(s + 11), mi = atoi(s + 14), se = atoi(s + 17);
    int64_t ns = 0;
    if (s[19] == '.') {
        int k = 0;
        for (const char *p = s + 20; *p >= '0' && *p <= '9' && k < 9; p++, k++)
            ns = ns * 10 + (*p - '0');
        for (; k < 9; k++) ns *= 10;
    }
    return ((days_from_civil(Y, M, D) * 86400LL) + h * 3600LL + mi * 60LL + se)
           * 1000000000LL + ns;
}

/* ==================== 3. Parsing d'un fichier ==================== */

static const char *find_in(const char *p, const char *end, const char *key)
{
    if (!p) return NULL;
    size_t k = strlen(key);
    if (k < 2) return NULL;
    char anchor = key[1];
    const char *lim = end - k;
    p++;
    while (p <= lim + 1) {
        const char *q = memchr(p, anchor, (size_t)(lim + 1 - p) + 1);
        if (!q) return NULL;
        if (!memcmp(q - 1, key, k)) return q - 1 + k;
        p = q + 1;
    }
    return NULL;
}

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

static const char *value_of(const char *p, const char *end, const char *key)
{
    const char *q = find_in(p, end, key);
    if (!q) return NULL;
    while (q < end && is_ws(*q)) q++;
    if (q < end && *q == ':') q++;
    while (q < end && is_ws(*q)) q++;
    if (q < end && *q == '"') q++;
    return q < end ? q : NULL;
}

static int parse_side(const char *p, const char *end, double *px, double *sz)
{
    int i;
    for (i = 0; i < LEVELS; i++) { px[i] = 0.0; sz[i] = 0.0; }
    for (i = 0; i < LEVELS && p && p < end; i++) {
        const char *a = value_of(p, end, "\"px\"");
        if (!a) break;
        px[i] = dec(a);
        const char *b = value_of(a, end, "\"sz\"");
        if (!b) break;
        sz[i] = dec(b);
        p = b;
    }
    return i;
}

static Snap *parse_file(const char *path, size_t *n_out, char *sample,
                        size_t sample_sz, int verbose)
{
    *n_out = 0;
    gzFile f = gzopen(path, "rb");
    if (!f) return NULL;
    gzbuffer(f, 1 << 20);

    size_t cap = 1 << 20, len = 0;
    char *buf = malloc(cap);
    for (;;) {
        if (len + (1 << 16) + 1 > cap) { cap *= 2; buf = realloc(buf, cap); }
        int r = gzread(f, buf + len, 1 << 16);
        if (r <= 0) break;
        len += (size_t)r;
    }
    gzclose(f);
    buf[len] = '\0';
    const char *bend = buf + len;

    size_t max = 1;
    for (const char *p = buf; (p = find_in(p, bend, "\"height\"")); ) max++;
    Snap *out = malloc(max * sizeof(Snap));

    size_t n = 0;
    const char *cur = buf;
    const char *rec = find_in(cur, bend, "\"height\"");
    while (rec) {
        const char *rstart = rec - 8;
        const char *nxt  = find_in(rec, bend, "\"height\"");
        const char *rend = nxt ? nxt - 8 : bend;
        Snap *s = &out[n];

        const char *h  = value_of(rstart, rend, "\"height\"");
        const char *bt = value_of(rstart, rend, "\"block_time\"");
        const char *bs = value_of(rstart, rend, "\"bids\"");
        const char *as = value_of(rstart, rend, "\"asks\"");
        rec = nxt;
        if (!h || !bt || !bs || !as) continue;

        if (!n && sample && sample_sz) {
            size_t L = (size_t)(rend - rstart);
            if (L > sample_sz - 1) L = sample_sz - 1;
            memcpy(sample, rstart, L);
            sample[L] = '\0';
        }
        s->height = (int64_t)strtoull(h, NULL, 10);
        s->ts     = parse_ts(bt);
        const char *be = memchr(bs, ']', (size_t)(rend - bs));
        const char *ae = memchr(as, ']', (size_t)(rend - as));
        if (!be || !ae) continue;
        int nb = parse_side(bs, be, s->bid_px, s->bid_sz);
        int na = parse_side(as, ae, s->ask_px, s->ask_sz);
        if (!nb && memchr(bs, '{', (size_t)(be - bs))) s->bid_px[0] = -1.0;
        if (!na && memchr(as, '{', (size_t)(ae - as))) s->ask_px[0] = -1.0;
        n++;
    }

    if (!n && verbose) {
        fprintf(stderr, "\nextract: aucun snapshot trouve dans %s\n"
                        "         debut du fichier decompresse :\n         %.200s\n"
                        "         (le parseur attend les cles height, block_time, "
                        "bids, asks avec px/sz)\n", path, buf);
    }
    free(buf);
    *n_out = n;
    return out;
}

/* ==================== 4. Programme principal ==================== */

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <dossier_racine> book.l2 [--jobs N] [--market NOM]\n",
                argv[0]);
        return 1;
    }
    const char *root = argv[1], *out_path = argv[2], *market = "mkts:US500";
    int jobs = 0;
    for (int i = 3; i < argc; i++) {
        if      (!strcmp(argv[i], "--jobs")   && i + 1 < argc) jobs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--market") && i + 1 < argc) market = argv[++i];
        else { fprintf(stderr, "option inconnue : %s\n", argv[i]); return 1; }
    }
#ifdef _OPENMP
    if (jobs > 0) omp_set_num_threads(jobs);
    fprintf(stderr, "parallelisme : %d threads\n", omp_get_max_threads());
#else
    if (jobs > 1)
        fprintf(stderr,
            "!! --jobs %d ignore : ce binaire est mono-thread.\n"
            "   Recompile avec  make OMP=1  pour paralleliser la decompression.\n",
            jobs);
#endif

    Paths P = { 0 };
    walk(root, &P);
    if (!P.n) { fprintf(stderr, "aucun fichier .json.gz sous %s\n", root); return 1; }
    qsort(P.v, P.n, sizeof(char *), cmp_path);
    fprintf(stderr, "%lu fichiers\n", (unsigned long)P.n);

    FILE *out = fopen(out_path, "wb");
    if (!out) { perror(out_path); return 1; }
    L2Header h;
    memset(&h, 0, sizeof h);
    fwrite(&h, sizeof h, 1, out);
    setvbuf(out, NULL, _IOFBF, 1 << 22);

    enum { BATCH = 64 };
    Snap   *part[BATCH];
    size_t  cnt[BATCH];
    static char sample[BATCH][600];
    int     diagnosed = 0;

    clock_t  t_begin = clock();
    uint64_t n_rec = 0, dup = 0, bad = 0, empty = 0, crossed = 0, unreadable = 0;
    uint64_t parsed = 0;
    int64_t  last_h = -1, t_start = 0, t_end = 0, prev_ts = 0;
    double   gap_max = 0;

    for (size_t base = 0; base < P.n; base += BATCH) {
        int m = (int)((P.n - base < BATCH) ? P.n - base : BATCH);

#ifdef _OPENMP
#       pragma omp parallel for schedule(dynamic)
#endif
        for (int j = 0; j < m; j++)
            part[j] = parse_file(P.v[base + j], &cnt[j],
                                 sample[j], sizeof sample[j], base == 0 && j == 0);

        for (int j = 0; j < m; j++) {
            if (!part[j]) { fprintf(stderr, "\nskip %s\n", P.v[base + j]); continue; }
            parsed += cnt[j];
            for (size_t i = 0; i < cnt[j]; i++) {
                Snap *s = &part[j][i];
                const char *why = NULL;
                int parse_fail = (s->bid_px[0] < 0 || s->ask_px[0] < 0);
                if (parse_fail) {
                    bad++; unreadable++;
                    why = "niveaux presents mais cles px/sz illisibles";
                } else if (s->bid_px[0] == 0 || s->ask_px[0] == 0) {
                    empty++; bad++;
                    continue;
                } else if (s->ask_px[0] <= s->bid_px[0]) {
                    crossed++; bad++;
                    why = "carnet croise : ask <= bid";
                } else if (s->height <= last_h) {
                    dup++;
                    why = "hauteur non croissante : fichiers mal ordonnes ?";
                }
                if (why && !diagnosed) {
                    diagnosed = 1;
                    fprintf(stderr,
                        "\n---------------- premier enregistrement rejete ------------"
                        "----\nfichier   : %s\nraison    : %s\n"
                        "lu        : height=%lld ts=%lld\n"
                        "            bid[0]=%.6f x %.6f   ask[0]=%.6f x %.6f\n"
                        "            bid[1]=%.6f   ask[1]=%.6f\n"
                        "texte brut de l'enregistrement :\n%s\n"
                        "------------------------------------------------------------"
                        "----\n",
                        P.v[base + j], why,
                        (long long)s->height, (long long)s->ts,
                        s->bid_px[0], s->bid_sz[0], s->ask_px[0], s->ask_sz[0],
                        s->bid_px[1], s->ask_px[1], sample[j]);
                }
                if (why) continue;
                last_h = s->height;

                if (!n_rec) t_start = s->ts;
                else {
                    double g = (double)(s->ts - prev_ts) * 1e-9;
                    if (g > gap_max) gap_max = g;
                }
                prev_ts = t_end = s->ts;
                fwrite(s, sizeof(Snap), 1, out);
                n_rec++;
            }
            free(part[j]);
        }
        {
            double el = (double)(clock() - t_begin) / CLOCKS_PER_SEC;
            fprintf(stderr,
                "\r%lu/%lu fichiers  %llu snaps  rejets: %llu vides %llu dup"
                "  %.0f fic/s   ",
                (unsigned long)(base + m), (unsigned long)P.n,
                (unsigned long long)n_rec, (unsigned long long)empty,
                (unsigned long long)dup, el > 0 ? (base + m) / el : 0);
        }
        if (base + m >= 64 && (!parsed || unreadable)) {
            fprintf(stderr, "\nextract: format illisible apres %lu fichiers "
                            "(%llu enregistrements decoupes, %llu avec des "
                            "niveaux illisibles).\n         Voir l'enregistrement "
                            "rejete ci-dessus.\n",
                    (unsigned long)(base + m), (unsigned long long)parsed,
                    (unsigned long long)unreadable);
            fclose(out); return 1;
        }
    }

    memcpy(h.magic, "L2v1", 4);
    h.levels   = LEVELS;
    h.rec_size = (uint32_t)sizeof(Snap);
    h.n_rec    = n_rec;
    h.t_start  = t_start;
    h.t_end    = t_end;
    strncpy(h.market, market, sizeof h.market - 1);
    fflush(out);
    fseek(out, 0, SEEK_SET);
    fwrite(&h, sizeof h, 1, out);
    fclose(out);

    double span = (double)(t_end - t_start) * 1e-9;
    fprintf(stderr,
        "\nsnapshots : %llu\ndoublons  : %llu\ninvalides : %llu "
        "(%llu carnets vides, %llu croises, %llu illisibles)\n"
        "periode   : %.2f jours (%.3f s / snapshot)\ngap max   : %.1f s\n"
        "taille    : %.2f Go\n",
        (unsigned long long)n_rec, (unsigned long long)dup,
        (unsigned long long)bad, (unsigned long long)empty,
        (unsigned long long)crossed, (unsigned long long)unreadable,
        span / 86400.0,
        n_rec > 1 ? span / (double)(n_rec - 1) : 0.0, gap_max,
        (double)(n_rec * sizeof(Snap)) / 1e9);
    if (gap_max > 3600)
        fprintf(stderr, "!! gap > 1 h : session fermee ou trou de donnees, "
                        "a exclure des statistiques\n");

    for (size_t i = 0; i < P.n; i++) free(P.v[i]);
    free(P.v);
    return 0;
}
