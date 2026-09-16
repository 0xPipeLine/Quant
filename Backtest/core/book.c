/* book.c — ouverture d'un fichier .l2 et lecture du carnet.
 *
 * Le fichier n'est pas lu : il est mappe en memoire, `b->s` pointe dedans.
 * Aucune copie, aucune deserialisation.
 *
 * book_open_range() ne mappe QUE la zone demandee. Les bornes sont trouvees
 * par quelques lectures de 8 octets (recherche binaire sur les timestamps),
 * et l'espace d'adressage consomme est celui de la fenetre, pas du fichier.
 *
 * Regle appliquee partout ici : aucun chemin d'erreur ne renvoie -1 sans dire
 * pourquoi.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "l2.h"

#ifdef _WIN32
#  include <windows.h>
#  define FSEEK64 _fseeki64
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/mman.h>
#  include <sys/stat.h>
#  define FSEEK64 fseeko
#endif

/* ==================== outils fichier ==================== */

static size_t map_granularity(void)
{
#ifdef _WIN32
    SYSTEM_INFO si; GetSystemInfo(&si);
    return (size_t)si.dwAllocationGranularity;      /* 64 Ko sous Windows */
#else
    return (size_t)sysconf(_SC_PAGE_SIZE);
#endif
}

static int64_t file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    FSEEK64(f, 0, SEEK_END);
#ifdef _WIN32
    int64_t n = _ftelli64(f);
#else
    int64_t n = (int64_t)ftello(f);
#endif
    fclose(f);
    return n;
}

static int64_t ts_at(FILE *f, size_t i)
{
    int64_t ts = 0;
    FSEEK64(f, (int64_t)sizeof(L2Header) + (int64_t)i * (int64_t)sizeof(Snap),
            SEEK_SET);
    if (fread(&ts, sizeof ts, 1, f) != 1) return 0;
    return ts;
}

static size_t seek_ts_file(FILE *f, size_t n, int64_t ts)
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t m = lo + (hi - lo) / 2;
        if (ts_at(f, m) < ts) lo = m + 1; else hi = m;
    }
    return lo;
}

/* ==================== dates ==================== */

static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, unsigned *m, unsigned *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t  yy  = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp  = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yy + (*m <= 2));
}

void l2_fmt_time(int64_t ts, char *buf, size_t n)
{
    int64_t sec = ts / 1000000000LL;
    int64_t day = sec / 86400, rem = sec % 86400;
    if (rem < 0) { rem += 86400; day--; }
    int y; unsigned mo, d;
    civil_from_days(day, &y, &mo, &d);
    snprintf(buf, n, "%04d-%02u-%02uT%02d:%02d:%02d", y, mo, d,
             (int)(rem / 3600), (int)(rem % 3600 / 60), (int)(rem % 60));
}

/* ==================== selection d'une zone ==================== */

static long spec_index(const L2Header *h, FILE *f, size_t n,
                       const char *sp, int is_end)
{
    if (!sp || !*sp) return is_end ? (long)n : 0;

    if (*sp == '#') return atol(sp + 1);                        /* index */

    size_t L = strlen(sp);
    if (L && sp[L - 1] == '%') {                                /* pourcentage */
        double fr = atof(sp) / 100.0;
        if (fr < 0) fr = 0;
        if (fr > 1) fr = 1;
        return (long)(fr * (double)n);
    }

    int64_t t;
    if (*sp == '-') {                                        /* relatif a la fin */
        double v = atof(sp + 1);
        char u = sp[L - 1];
        double mul = (u == 'd') ? 86400 : (u == 'h') ? 3600 : (u == 'm') ? 60 : 1;
        t = h->t_end - (int64_t)(v * mul * 1e9);
    } else if (L >= 10 && sp[4] == '-' && sp[7] == '-') {       /* date ISO */
        int y = atoi(sp), mo = atoi(sp + 5), d = atoi(sp + 8);
        int hh = 0, mi = 0, se = 0;
        if (L >= 16 && (sp[10] == 'T' || sp[10] == ' ')) {
            hh = atoi(sp + 11); mi = atoi(sp + 14);
            if (L >= 19) se = atoi(sp + 17);
        }
        t = ((days_from_civil(y, mo, d) * 86400LL) + hh * 3600LL + mi * 60LL + se)
            * 1000000000LL;
    } else if (L >= 15) {
        t = strtoll(sp, NULL, 10);                              /* ns brut */
    } else {
        fprintf(stderr, "book: borne non reconnue : '%s'\n"
                "      formes acceptees : 2026-08-21, 2026-08-21T16:30,\n"
                "      -3d / -12h / -90m, 40%%, #250000\n", sp);
        return -1;
    }

    if (t < h->t_start || t > h->t_end) {
        char a[24], z[24], w[24];
        l2_fmt_time(h->t_start, a, sizeof a);
        l2_fmt_time(h->t_end,   z, sizeof z);
        l2_fmt_time(t,          w, sizeof w);
        fprintf(stderr, "book: la borne '%s' (%s) est hors du fichier,\n"
                        "      qui couvre %s -> %s\n", sp, w, a, z);
        if (t < h->t_start && !is_end) return 0;
        if (t > h->t_end   &&  is_end) return (long)n;
        return -1;
    }
    return f ? (long)seek_ts_file(f, n, t) : (is_end ? (long)n : 0);
}

/* ==================== mapping ==================== */

static int map_window(Book *b, const char *path, size_t i0, size_t i1)
{
    int64_t off   = (int64_t)sizeof(L2Header) + (int64_t)i0 * (int64_t)sizeof(Snap);
    int64_t gran  = (int64_t)map_granularity();
    int64_t base  = off - (off % gran);
    size_t  delta = (size_t)(off - base);
    size_t  len   = delta + (i1 - i0) * sizeof(Snap);

#ifdef _WIN32
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "book: ouverture de %s impossible (erreur Windows %lu)\n",
                path, (unsigned long)GetLastError());
        return -1;
    }
    HANDLE m = CreateFileMappingA(f, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!m) {
        fprintf(stderr, "book: CreateFileMapping a echoue (erreur Windows %lu)\n",
                (unsigned long)GetLastError());
        CloseHandle(f); return -1;
    }
    void *p = MapViewOfFile(m, FILE_MAP_READ, (DWORD)((uint64_t)base >> 32),
                            (DWORD)((uint64_t)base & 0xFFFFFFFFu), len);
    if (!p) {
        fprintf(stderr,
            "book: MapViewOfFile a echoue pour une fenetre de %.2f Go "
            "(erreur Windows %lu).\n"
            "      Ce binaire est en %d bits. En 32 bits, une fenetre de plus\n"
            "      de ~1 Go ne peut pas etre mappee : recompile en 64 bits\n"
            "      (MSYS2 MINGW64) ou restreins la zone avec --from / --to.\n",
            (double)len / 1e9, (unsigned long)GetLastError(),
            (int)(sizeof(void *) * 8));
        CloseHandle(m); CloseHandle(f); return -1;
    }
    b->hf_ = f; b->hm_ = m;
#else
    int fd = open(path, O_RDONLY);
    if (fd < 0) { fprintf(stderr, "book: ouverture de %s impossible\n", path); return -1; }
    void *p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, (off_t)base);
    if (p == MAP_FAILED) {
        fprintf(stderr, "book: mmap d'une fenetre de %.2f Go a echoue "
                        "(binaire %d bits)\n",
                (double)len / 1e9, (int)(sizeof(void *) * 8));
        close(fd); return -1;
    }
    b->fd_ = fd;
#endif
    b->map_ = p; b->len_ = len; b->off_ = delta;
    b->s    = (const Snap *)((const char *)p + delta);
    b->n    = i1 - i0;
    b->i0   = i0;
    return 0;
}

/* ==================== ouverture ==================== */

static long read_header(const char *path, L2Header *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "book: %s introuvable ou illisible\n", path); return -1; }
    size_t got = fread(h, 1, sizeof *h, f);
    fclose(f);
    if (got != sizeof *h) {
        fprintf(stderr, "book: %s trop court pour contenir un en-tete\n", path);
        return -1;
    }
    if (memcmp(h->magic, "L2v1", 4)) {
        fprintf(stderr, "book: %s n'est pas un fichier .l2 "
                        "(signature '%.4s' au lieu de 'L2v1')\n", path, h->magic);
        return -1;
    }
    if (h->levels != LEVELS || h->rec_size != (uint32_t)sizeof(Snap)) {
        fprintf(stderr, "book: %s ecrit avec levels=%u rec=%u, moteur compile "
                        "avec LEVELS=%d rec=%lu.\n"
                        "      -> reconvertis les donnees, ou recompile avec le "
                        "meme LEVELS.\n",
                path, h->levels, h->rec_size, LEVELS, (unsigned long)sizeof(Snap));
        return -1;
    }
    int64_t sz = file_size(path);
    long fit = (long)((sz - (int64_t)sizeof(L2Header)) / (int64_t)sizeof(Snap));
    long n   = (long)h->n_rec;
    if (n <= 0) {
        fprintf(stderr, "book: l'en-tete annonce %ld snapshots. Conversion "
                        "interrompue avant la reecriture de l'en-tete ?\n", n);
        return -1;
    }
    if (n > fit) {
        fprintf(stderr, "book: l'en-tete annonce %ld snapshots, le fichier n'en "
                        "contient que %ld — on s'arrete la.\n", n, fit);
        n = fit;
    }
    return n;
}

int book_open_range(Book *b, const char *path, const char *from, const char *to)
{
    memset(b, 0, sizeof *b);
    L2Header h;
    long n = read_header(path, &h);
    if (n < 0) return -1;
    b->h = h;
    b->h.n_rec = (uint64_t)n;

    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "book: %s illisible\n", path); return -1; }
    long i0 = spec_index(&b->h, f, (size_t)n, from, 0);
    long i1 = spec_index(&b->h, f, (size_t)n, to,   1);
    fclose(f);

    if (i0 < 0 || i1 < 0) return -1;
    if (i1 > n) i1 = n;
    if (i0 >= i1) {
        fprintf(stderr, "book: zone vide — from=%s to=%s donne [%ld, %ld) "
                        "sur %ld snapshots\n",
                from ? from : "debut", to ? to : "fin", i0, i1, n);
        return -1;
    }
    return map_window(b, path, (size_t)i0, (size_t)i1);
}

int book_open(Book *b, const char *path)
{
    return book_open_range(b, path, NULL, NULL);
}

void book_close(Book *b)
{
    if (!b->map_) { memset(b, 0, sizeof *b); return; }
#ifdef _WIN32
    UnmapViewOfFile(b->map_); CloseHandle((HANDLE)b->hm_); CloseHandle((HANDLE)b->hf_);
#else
    munmap(b->map_, b->len_); close(b->fd_);
#endif
    memset(b, 0, sizeof *b);
}

size_t book_seek(const Book *b, int64_t ts)
{
    size_t lo = 0, hi = b->n;
    while (lo < hi) {
        size_t m = lo + (hi - lo) / 2;
        if (b->s[m].ts < ts) lo = m + 1; else hi = m;
    }
    return lo;
}

Book book_slice(const Book *b, size_t i0, size_t i1)
{
    Book v;
    memset(&v, 0, sizeof v);
    if (i1 > b->n) i1 = b->n;
    if (i0 > i1)   i0 = i1;
    v.h  = b->h;
    v.s  = b->s + i0;
    v.n  = i1 - i0;
    v.i0 = b->i0 + i0;
    return v;
}

/* ==================== carnet ==================== */

double snap_walk(const Snap *s, int side, double qty, double *filled)
{
    const double *px = side > 0 ? s->ask_px : s->bid_px;
    const double *sz = side > 0 ? s->ask_sz : s->bid_sz;
    double left = qty, notional = 0, done = 0;
    for (int i = 0; i < LEVELS && left > 0; i++) {
        if (px[i] <= 0) break;
        double take = sz[i] < left ? sz[i] : left;
        notional += take * px[i];
        done += take; left -= take;
    }
    if (filled) *filled = done;
    return done > 0 ? notional / done : 0.0;
}

/* Convention du mode --reverse : on consomme les 20 niveaux, et ce qui reste
 * est execute au prix du dernier niveau touche. Un carnet avec 2 + 1 + 3 pour
 * une demande de 5 remplit 2, 1, 2 ; une demande de 8 remplit 2, 1, 3 puis 2
 * de plus au prix du troisieme niveau. */
double snap_walk_full(const Snap *s, int side, double qty)
{
    const double *px = side > 0 ? s->ask_px : s->bid_px;
    const double *sz = side > 0 ? s->ask_sz : s->bid_sz;
    double left = qty, notional = 0, last = 0;
    for (int i = 0; i < LEVELS && left > 0; i++) {
        if (px[i] <= 0) break;
        double take = sz[i] < left ? sz[i] : left;
        notional += take * px[i];
        left -= take;
        last = px[i];
    }
    if (left > 0) {
        if (last <= 0) last = px[0];
        notional += left * last;
    }
    return qty > 0 ? notional / qty : 0.0;
}

double snap_depth(const Snap *s, int side, double frac)
{
    double mid = 0.5 * (s->bid_px[0] + s->ask_px[0]);
    double lim = side > 0 ? mid * (1 + frac) : mid * (1 - frac);
    const double *px = side > 0 ? s->ask_px : s->bid_px;
    const double *sz = side > 0 ? s->ask_sz : s->bid_sz;
    double tot = 0;
    for (int i = 0; i < LEVELS; i++) {
        if (px[i] <= 0) break;
        if (side > 0 ? (px[i] > lim) : (px[i] < lim)) break;
        tot += sz[i];
    }
    return tot;
}
