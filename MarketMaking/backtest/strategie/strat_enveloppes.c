#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "strat_enveloppes.h"

// available_long = max(value - (c.close * size) - bids_value, 0)
// available_short = max(value + (c.close * size) - asks_value, 0)

double min (double a, double b) {
    return a > b ? b : a;
}

double max(double a, double b) {
    return a > b ? a : b;
}

PF *new_pf(double initial_price, double initial_value) {
    PF *pf = malloc(sizeof(PF));
    pf -> entry = initial_price;
    pf -> value = initial_value;
    pf -> size = 0;
    pf -> step = 0;
    pf -> volume = 0;
    double sz_bid = initial_value / (LEVELS * initial_price);           //sz_bid et ask ne peuvent pas être égaux car si on est déjat short de 3 levels on peut plus buy que short
    double sz_ask = initial_value / (LEVELS * initial_price); // same
    for (int i = 0; i < LEVELS; i++) {
        double px_bid = initial_price - (initial_price *( (i + 1) * SPREAD));           //pas possible pour i = 0 on as une bid supérieur au prix du march
        double px_ask = initial_price + (initial_price *( (i + 1) * SPREAD));
        pf -> bids[i] = (Order){.price=px_bid, .size=sz_bid, .state=0};
        pf -> asks[i] = (Order){.price=px_ask, .size=-sz_ask, .state=0};
    }
    return pf;
}

void update(PF *pf, Candle c) {
    
    for (int i = 0; i < LEVELS; i++) {
        int in = 0;
        Order bid = pf -> bids[i];
        Order ask = pf -> asks[i];
        if (c.low < bid.price) {
            double trade_value = bid.price * bid.size;
            if (pf -> size < 0) { // shorting
                if (bid.size > -pf -> size) { // closing short
                    pf -> value += (bid.price - pf -> entry) * (bid.size + pf -> size);
                    pf -> entry = bid.price;
                } else {
                    pf -> value += (bid.price - pf -> entry) * bid.size;
                }
            } else if (pf -> size + bid.size != 0) {
                pf -> entry = ((pf -> entry * pf -> size) + trade_value) / (pf -> size + bid.size); // raising position
            }
            pf -> volume += trade_value;
            pf -> value -= trade_value * FEE;
            pf -> size += bid.size;
            pf -> step -= 1;
            in = 1;
        }
        if (c.high > ask.price) {
            double trade_value = ask.price * ask.size;
            if (pf -> size > 0) { // longing
                if (-ask.size > pf -> size) { // closing long
                    pf -> value += (ask.price - pf -> entry) * (ask.size + pf -> size);
                    pf -> entry = ask.price;
                } else {
                    pf -> value += (ask.price - pf -> entry) * ask.size;
                }
            } else if (pf -> size + ask.size != 0) {
                pf -> entry = ((pf -> entry * pf -> size) + trade_value) / (pf -> size + ask.size);  // raising position
            }
            pf -> volume -= trade_value;
            pf -> value += trade_value * FEE;
            pf -> size += ask.size;
            pf -> step += 1;
            in = 1;
        }
        if (in == 0) {
            break; // plus aucun ask et bid ne sera prix
        }
    }
    double position = c.close * pf -> size;
    double sz_bid = max(pf -> value - position, 0) / (LEVELS * c.close);
    double sz_ask = max(pf -> value + position, 0) / (LEVELS * c.close);
    for (int i = 0; i < LEVELS; i++) {
        double px_bid = min(c.moy * (1 - (i + 1) * SPREAD), c.close * (1 - FEE));
        double px_ask = max(c.moy * (1 + (i + 1) * SPREAD), c.close * (1 + FEE));
        pf->bids[i] = (Order){.size = sz_bid, .price = px_bid, .state = 0};
        pf->asks[i] = (Order){.size = -sz_ask, .price = px_ask, .state = 0};
    }
}

void print_order(Order o) {
    const char *side = (o.size < 0) ? "ASK" : "BID";
    printf("%s: %.2f x %.2f$", side, o.size, o.price);
}

void print_pf(PF *pf) {
    if (pf == NULL) {
        printf("PF is NULL\n");
        return;
    }
    printf("Value: %.2f$ ", pf->value);
    printf("Entry: %.5f$ ", pf->entry);
    printf("Size: %.5f ", pf->size);
    printf("Volume: %.5f$", pf->volume);
    printf("\nbids: ");
    for (int i = 0; i < LEVELS; i++) {
        print_order(pf->bids[i]);
        printf("  ");
    }
    printf("\nasks: ");
    for (int i = 0; i < LEVELS; i++) {
        print_order(pf->asks[i]);
        printf("  ");
    }
    printf("\n");
}

PF *enveloppe(double initial_price, double initial_value, Candles *candles, int start_candle) {
    PF *pf = new_pf(initial_price, initial_value);
    for (int i = start_candle; i < candles -> count; i++) {
        update(pf, candles -> candles[i]);
    }
    return pf;
}


