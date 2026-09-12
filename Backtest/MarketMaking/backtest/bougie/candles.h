#ifndef CANDLES_H
#define CANDLES_H

typedef struct Candle {
    double moy;
    double open;
    double high;
    double low;
    double close;
} Candle;

typedef struct Candles {
    Candle *candles;
    int count;
} Candles;

Candles *get_candles(const char *path);
void print_candles(Candles *candles);
#endif