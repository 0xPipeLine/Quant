#ifndef SMMA_H
#define SMMA_H
#include "../bougie/candles.h"


// ============================================================
// calculer_SMMA
//
//  ma_bougie           : tableau de valeurs (ex: rsi[], close[], high[], low[])
//  nb_ligne            : longueur du tableau source
//  periode             : fenêtre de lissage
// ============================================================

double *cal_smma (int periode, double *ma_bougie, int line_count);
void compute_smma(Candles *candles, int window);

#endif
