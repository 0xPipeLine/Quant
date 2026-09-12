#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include "strategie/strat_crois_moy.h"
#include "bougie/candles.h"
// #include "bougie/bougie_classique.h"
// #include "strategie/envelo_test_3.h"
// #include "indicateur/sma.h"
#include "indicateur/smma.h"
#include "strategie/strat_enveloppes.h"

#include "config.h"

//int bougie_skip = 51;
//double frais = 0.00003//0.99997////0.99991;//   0.99955;
//double wallet = 1000;
//int nb_ordre = 10;
//double ecart_max = 0.015; // en % /100


int main (){
    
    // charger_bougies("data/xyz_SP500_1m.csv");
    //charger_bougies("../data/xyz_SILVER_1m.csv");
    // charger_bougies("data/ethusd_1min_2025.csv");
    //printf ("%f\n", close[0] );
    
    Candles *candles = get_candles("../data/xyz_SP500_5m.csv");
    //Candles *candles = get_candles("../data/ethusd_1min_2025.csv");
    compute_smma(candles, 50);
    double initial_price = candles -> candles[0].close;
    PF *pf = enveloppe(initial_price, INITIAL_VALUE, candles, START_CANDLE);
    print_pf(pf);
    

    // on calcule smma 3 et 50 sur les close
    
    
    // double *smma_long = cal_smma(50, close, line_count);

    

    // double *enveloppes = strat_enveloppes(
    // smma_long,
    // SPREAD*LEVELS,
    // LEVELS,
    // close,
    // high,
    // low,
    // line_count,
    // START_CANDLE,
    // INITIAL_VALUE,
    // FEE);
    
    //double *strat = strat_crois ( smma_long, smma_long, close, line_count, START_CANDLE, WALLET, FEE);

    //free(sma5);
    //free(smma_long);
    //free(smma_cour);
    //liberer_bougies();
    return 0;
}