

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "strat_enveloppes.h"
#include <math.h>

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
    pf -> usdc_wallet = initial_value;
    pf -> size = 0;
    pf -> step = 0;
    pf -> step_back = 0;
    pf -> volume = 0;
    double sz_bid = initial_value / (LEVELS * initial_price);           //
    double sz_ask = initial_value / (LEVELS * initial_price); // same
    for (int i = 0; i < LEVELS; i++) {                                                  //on dois remettre i =0 car sinon bug de mémoir
        double px_bid = initial_price - (initial_price *( (i + 1) * SPREAD));           //pas possible pour i = 0 on as une bid supérieur au prix du march // j'ai mis i = 1
        double px_ask = initial_price + (initial_price *( (i + 1) * SPREAD));
        pf -> bids[i] = (Order){.price=px_bid, .size=sz_bid, .state=0};
        pf -> asks[i] = (Order){.price=px_ask, .size=-sz_ask, .state=0};
    }
    return pf;
}

void update(PF *pf, Candle c) {
    pf -> step_back = pf -> step;
    if (pf -> size <= 0){                    //si on était short a la bougie précédente
        printf ("11.1\n");
        for (int i = pf -> step; i < LEVELS; i++) {       // on met la valeur de i a step //attention ici on as une lecture inutile de i jusqu'a levels pour toute les bougie 
            Order ask = pf -> asks[i];                      // j'ai mis step +1 car on regard pour lordre suivant 
            printf ("11.2\n"  );
            if (c.high > ask.price) {
                printf ("11.3\n"  );
                double trade_value = ask.price * ask.size;
                if (pf -> size + ask.size != 0) {
                    pf -> entry = ((pf -> entry * pf -> size) + trade_value) / fabs(pf -> size + ask.size);             //fabs pour tout le temps diviser par une valeur positive pour garder le signe
                }
                pf -> volume -= trade_value;
                pf -> value += trade_value * FEE;
                pf -> size += ask.size;
                pf -> step += 1;
            }
        }

        if (c.low < c.moy) {            //on dois fermer le short donc calculer le value
            pf -> step = 0;
            pf -> volume += (pf -> entry * pf -> size);
            pf -> value = (pf -> value - (pf -> entry * fabs (pf -> size))) + (( pf -> entry - c.moy) * fabs (pf -> size));       //calcule du nouveau value
            pf -> value -= (pf -> entry * pf -> size) * FEE;
            pf -> size = 0;
            printf ("11.4\n"  );

            for (int i = pf -> step; i < LEVELS; i++) {         //quand on était en short et que on as touché la moy donc on as tp, maintenant on regarde si on prend pas des long
                Order bid = pf -> bids[i];
                printf ("11.5\n" );
                if (c.low < bid.price) {
                    double trade_value = bid.price * bid.size;
                    if (pf -> size + bid.size != 0) {
                        pf -> entry = ((pf -> entry * pf -> size) + trade_value) / fabs(pf -> size + bid.size);
                    }
                    pf -> volume += trade_value;
                    pf -> value -= trade_value * FEE;
                    pf -> size += bid.size;
                    pf -> step -= 1;
                    printf ("11.6 \n");
                }
            }
        }
    }
    printf ("size %f \n", pf -> size);
    if (pf -> size >= 0 && pf -> step_back <= 0){                    //si on était long a la bougie précédente        // il faut faure attention a que il rentre pas dans le premier if size < 0 et que il rentre pas aussi dans size > 0, si on était en short après on tp ensuite on ce fait fille en long, ensuite il vas re tp car le high aura était précédement au dessus de smma
        printf ("22.1 \n");
        for (int i = pf -> step; i < LEVELS; i++) {         //quand on était en short et que on as touché la moy donc on as tp, maintenant on regarde si on prend pas des long
            Order bid = pf -> bids[i];
            printf ("22.2 \n");
            if (c.low < bid.price) {
                printf ("22.3 \n");
                double trade_value = bid.price * bid.size;
                if (pf -> size + bid.size != 0) {
                    pf -> entry = ((pf -> entry * pf -> size) + trade_value) / fabs(pf -> size + bid.size);
                }
                pf -> volume += trade_value;
                pf -> value -= trade_value * FEE;
                pf -> size += bid.size;
                pf -> step -= 1;
            }
        }
        if (c.high > c.moy) {            //si on dois fermer le long donc calculer le value
            printf ("22.4 \n");
            pf -> step = 0;
            pf -> volume += (pf -> entry * pf -> size);
            pf -> value = (pf -> value - (pf -> entry * fabs (pf -> size))) + (( c.moy - pf -> entry) * fabs (pf -> size));       //calcule du nouveau value
            pf -> value -= (pf -> entry * pf -> size) * FEE;
            pf -> size = 0;

            for (int i = pf -> step; i < LEVELS; i++) {       // on met la valeur de i a step //attention ici on as une lecture inutile de i jusqu'a levels pour toute les bougie 
                Order ask = pf -> asks[i];
                printf ("22.5 \n");
                if (c.high > ask.price) {
                    double trade_value = ask.price * ask.size;
                    if (pf -> size + ask.size != 0) {
                        pf -> entry = ((pf -> entry * pf -> size) + trade_value) / fabs(pf -> size + ask.size);
                    }
                    pf -> volume -= trade_value;
                    pf -> value += trade_value * FEE;
                    pf -> size += ask.size;
                    pf -> step += 1;
                    printf ("22.6 \n");
                }
            }
        }
    }
    
    double position = c.close * pf -> size;
    
    for (int i = 0; i < LEVELS; i++) {                                              //i = 1 pour que a i = 0 on est pas de position
        double px_bid = min(c.moy - (c.moy * (i * SPREAD)), c.close * (1 - FEE));   //changer pour mieux comprendre il y avais des valeur bizare
        double px_ask = max(c.moy + (c.moy * (i * SPREAD)), c.close * (1 + FEE));
        
        if (pf -> step_back != 0){
            double sz_bid = max(pf -> value - position, 0) / (LEVELS - abs (pf -> step_back)) / px_bid;            // on fait / (LEVELS * c.close) ça n'a aucun sens, il faut calculer en $ notre capacité, et ensuite quand on sais le entry de l'ordre on le divise a ce moment
            pf->bids[i] = (Order){.size = sz_bid, .price = px_bid, .state = 0};
        }
        if (pf -> step_back != 0){
            double sz_ask = max(pf -> value + position, 0) / (LEVELS - abs (pf -> step_back)) / px_ask;             //problème on peut diviser par 0 // j'ai mis le calcul des sz après le prix comme ça on peut utiliser le prix des bid et ask pour les sz
            pf->asks[i] = (Order){.size = -sz_ask, .price = px_ask, .state = 0};
        }                                                                                            //px permet de pas se faire fille plus de valeur que ce que l'on as dans value
        
        
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
        print_pf(pf);
    }
    return pf;
}

