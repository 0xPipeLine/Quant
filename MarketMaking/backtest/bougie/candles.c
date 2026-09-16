#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "candles.h"

int count_csv_lines(FILE *file) {
    if (!file) return -1;
    int line_count = 0;
    char buffer[80];
    while (fgets(buffer, sizeof(buffer), file)) {
        line_count++;
    }
    rewind(file);
    return line_count;
}

Candle *parse(FILE *file, int line_count) {
    Candle *candles = malloc((line_count - 1) * sizeof(Candle));
    char buffer[256];
    fgets(buffer, sizeof(buffer), file);
    int i = 0;
    while (fgets(buffer, sizeof(buffer), file)) {
        long open_ts, close_ts;
        double open, high, low, close, volume;
        int trades;
        int parsed = sscanf(buffer,
            "%ld,%ld,%lf,%lf,%lf,%lf,%lf,%d",
            &open_ts, &close_ts,
            &open, &high, &low, &close,
            &volume, &trades);
        if (parsed != 8) continue;
        candles[i].open = open;
        candles[i].high = high;
        candles[i].low = low;
        candles[i].close = close;
        candles[i].moy = 0;
        i++;
    }
    if (i != line_count - 1) {
        printf("diff %d, %d", i, line_count);
        return NULL;
    }
    return candles;
}

Candles *get_candles(const char *path) {
    Candles *candles = malloc(sizeof(Candles));
    candles -> count = 0;
    candles -> candles = NULL;
    FILE *file = fopen(path, "r");
    if (!file) return candles;
    int line_count = count_csv_lines(file);
    if (line_count <= 1) {fclose(file); return candles;}
    candles -> count = line_count - 1;
    candles -> candles = parse(file, line_count);
    fclose(file);
    return candles;
}

void print_candles(Candles *candles) {
    if (!candles -> candles || candles -> count <= 0) {
        printf("No candles to display.\n");
        return;
    }
    for (int i = 0; i < candles -> count; i++) {
        printf("open: %.2f", candles -> candles[i].open);
        printf(" high: %.2f", candles -> candles[i].high);
        printf(" low: %.2f", candles -> candles[i].low);
        printf(" close: %.2f", candles -> candles[i].close);
        printf(" moy: %.2f\n", candles -> candles[i].moy);
    }
}
