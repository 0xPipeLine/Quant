#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "strat_crois_moy.h"

// ============================================================
// strategie_croisement_moyenne
//
//  moy_long            : tableau de valeurs (ex: rsi[], smma50, sma50)
//  moy_cour            : tableau de valeurs (ex: rsi[], smma50, sma50)
//  ma_bougie           : sur quoi on calcule (close[], high[], low[])
//  nb_ligne            : longueur du tableau source
//  bougie_skip         : nombre de bougie a skip 50 pour smma50
//  wallet              : nombre d'argent de départ
//  frais               : le multiple pour compte les frais
// ============================================================

double *strat_crois ( double *moy_long, double *moy_cour, double *ma_bougie, int nb_ligne, int bougie_skip, double wallet, double frais){

    for (int i = bougie_skip-1; i < nb_ligne; i++){

        if (i < nb_ligne && moy_cour[i] > moy_long[i]){
            wallet = wallet * frais*frais;
            i++;
            while (i+1 < nb_ligne && moy_cour[i] > moy_long[i]){                          //long
                wallet = wallet * (ma_bougie[i] / ma_bougie[i-1]);
                i++;
            }
            i++;
        }
        if (i < nb_ligne && moy_cour[i] < moy_long[i]){
            wallet = wallet * frais*frais;
            i++;
            while (i+1 < nb_ligne && moy_cour[i] < moy_long[i]){                          //short
                wallet = wallet * (ma_bougie[i-1] / ma_bougie[i]);
                i++;
            }
            i++;
        }
        printf ("wallet = %.2f\n", wallet);
    }

}