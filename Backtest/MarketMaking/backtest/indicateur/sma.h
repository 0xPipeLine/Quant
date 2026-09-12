#ifndef SMA_H
#define SMA_H

// ============================================================
// calculer_sma
//
//  ma_bougie           : tableau de valeurs (ex: rsi[], close[], high[], low[])
//  nb_ligne            : longueur du tableau source
//  periode             : fenêtre de lissage
// ============================================================

double *cal_sma (int periode, double *ma_bougie, int nb_ligne);


#endif
