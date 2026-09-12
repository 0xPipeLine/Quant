#ifndef strat_enveloppes_H
#define strat_enveloppes_H
#include "../config.h"
#include "../bougie/candles.h"



typedef struct Order {
    double size; // < 0 if ask else > 0
    double price;
    int state; // 0 if unfill; 1 if fill; 2 if cancel
} Order;

typedef struct PF {
    Order asks[LEVELS];   // ascending
    Order bids[LEVELS];   // descending
    double value;   //valeur en $ du wallet
    double entry;   // entry price
    double size;
    double volume;
    int step;
    int step_back;
    double asset_wallet;
    double usdc_wallet;
} PF;

void print_order(Order o);
void print_pf(PF *pf);

PF *enveloppe(double initial_price, double initial_value, Candles *candles, int start_candle);
#endif
