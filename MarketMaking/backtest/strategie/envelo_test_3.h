#ifndef strat_test_3_H
#define strat_test_3_H

double *strat_enveloppes(
    double *moy,
    double  ecart_max,
    int     nb_ordre,
    double *b_close,
    double *b_high,
    double *b_low,
    int     nb_ligne,
    int     bougie_skip,
    double  wallet,
    double  frais);

#endif
