/* info.c — inspection d'un fichier .l2. A lancer en premier quand quelque
 * chose ne va pas : dit ce que le moteur voit reellement du fichier.
 *
 *   bin/info data/extracted/mkts-US500.l2 [--gaps 20] [--from B] [--to B]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "l2.h"

typedef struct { double sec; int64_t ts; } Gap;

static int cmp_gap(const void *a, const void *b)
{
    double x = ((const Gap *)a)->sec, y = ((const Gap *)b)->sec;
    return x < y ? 1 : x > y ? -1 : 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (argc < 2) { fprintf(stderr, "usage: %s book.l2 [--gaps N]\n", argv[0]); return 1; }
    int top = 10;
    const char *from = NULL, *to = NULL;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i], *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if      (!strcmp(a, "--gaps") && v) top  = atoi(argv[++i]);
        else if (!strcmp(a, "--from") && v) from = argv[++i];
        else if (!strcmp(a, "--to")   && v) to   = argv[++i];
        else { fprintf(stderr, "option inconnue : %s\n", a); return 1; }
    }

    printf("binaire        : %d bits\n", (int)(sizeof(void *) * 8));
    printf("sizeof(Snap)   : %lu octets, LEVELS=%d\n",
           (unsigned long)sizeof(Snap), LEVELS);

    Book b;
    if (book_open_range(&b, argv[1], from, to)) return 1;

    char d0[24], d1[24];
    l2_fmt_time(b.s[0].ts, d0, sizeof d0);
    l2_fmt_time(b.s[b.n - 1].ts, d1, sizeof d1);
    double span = (double)(b.s[b.n - 1].ts - b.s[0].ts) * 1e-9;

    printf("marche         : %.24s\n", b.h.market);
    printf("snapshots      : %lu sur %" PRIu64 " dans le fichier\n",
           (unsigned long)b.n, b.h.n_rec);
    printf("periode        : %s -> %s  (%.2f jours)\n", d0, d1, span / 86400.0);
    printf("cadence moyenne: %.3f s\n", b.n > 1 ? span / (double)(b.n - 1) : 0.0);
    printf("taille mappee  : %.2f Go\n", (double)b.n * sizeof(Snap) / 1e9);

    const Snap *a = &b.s[0], *z = &b.s[b.n - 1];
    printf("premier carnet : bid %.4f x%.4f   ask %.4f x%.4f\n",
           a->bid_px[0], a->bid_sz[0], a->ask_px[0], a->ask_sz[0]);
    printf("dernier carnet : bid %.4f x%.4f   ask %.4f x%.4f\n",
           z->bid_px[0], z->bid_sz[0], z->ask_px[0], z->ask_sz[0]);

    long bad = 0, back = 0, empty = 0;
    double lo = 1e300, hi = -1e300;
    int64_t t_lo = 0, t_hi = 0;
    Gap *g = malloc((size_t)b.n * sizeof(Gap));
    size_t ng = 0;
    for (size_t i = 0; i < b.n; i++) {
        const Snap *s = &b.s[i];
        if (s->bid_px[0] <= 0 || s->ask_px[0] <= 0) { empty++; continue; }
        if (s->ask_px[0] <= s->bid_px[0]) bad++;
        double mid = 0.5 * (s->bid_px[0] + s->ask_px[0]);
        if (mid < lo) { lo = mid; t_lo = s->ts; }
        if (mid > hi) { hi = mid; t_hi = s->ts; }
        if (i) {
            int64_t d = s->ts - b.s[i - 1].ts;
            if (d < 0) back++;
            else if (d > 60LL * 1000000000LL)
                { g[ng].sec = (double)d * 1e-9; g[ng].ts = b.s[i - 1].ts; ng++; }
        }
    }
    { char x[24], y[24];
      l2_fmt_time(t_lo, x, sizeof x); l2_fmt_time(t_hi, y, sizeof y);
      printf("mid le plus bas: %.4f  le %s\n", lo, x);
      printf("mid le + haut  : %.4f  le %s\n", hi, y); }
    printf("anomalies      : %ld carnets vides, %ld croises, %ld timestamps "
           "en arriere\n", empty, bad, back);

    printf("trous > 60 s   : %lu\n", (unsigned long)ng);
    if (ng) {
        qsort(g, ng, sizeof(Gap), cmp_gap);
        int n = (int)ng < top ? (int)ng : top;
        for (int i = 0; i < n; i++) {
            char t[24];
            l2_fmt_time(g[i].ts, t, sizeof t);
            printf("                 %s  +%.0f s (%.2f h)\n",
                   t, g[i].sec, g[i].sec / 3600.0);
        }
        printf("  (un trou = session fermee ou donnees manquantes : decoupe\n"
               "   tes tests avec --from/--to plutot que de le traverser)\n");
    }
    free(g);
    book_close(&b);
    return 0;
}
