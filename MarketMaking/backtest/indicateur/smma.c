#include <stdio.h>
#include <stdlib.h>
#include "smma.h"
#include "../bougie/candles.h"


// ============================================================
// calculer_SMMA
//
//  periode             : fenêtre de lissage
//  ma_bougie           : tableau de valeurs (ex: rsi[], close[], high[], low[])
//  line_count            : longueur du tableau source
// ============================================================

double *cal_smma (int periode, double *ma_bougie, int line_count ){
    double somme = 0;
    
    double *smma;
    smma = malloc (line_count * sizeof(double));
    
    //FILE *a = fopen (fichier_sortie, "w");
    for (int i = 0 ; i < periode  ; i++){                            //calculer de la moyenne des x premier terme
        for (int j = 0 ; j <= i ; j++){
            somme = somme + ma_bougie[i-j];
        }
        smma[i] = somme / (i+1);
        somme = 0;        
    }

    for (int i = periode ; i < line_count ; i++){                         //calculer du smma suivant
        smma[i] = (smma[i-1] * (periode - 1) + ma_bougie[i]) / periode;
    }
    //fclose(a);
    return smma;
}

void compute_smma(Candles *candles, int window) {
    double somme = 0;
    for (int i = 0; i < window; i++) {
        somme += candles -> candles[i].close;
        candles -> candles[i].moy = somme / (i + 1);
    }
    for (int i = window; i < candles -> count; i++) {
        candles -> candles[i].moy = (candles -> candles[i-1].moy * (window - 1) + candles -> candles[i].close) / window;
    }
}
