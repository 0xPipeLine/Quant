#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "strat_enveloppes.h"

// ============================================================
// strategie_envoloppes
//
//  moy                 : tableau de valeurs (ex: rsi[], smma50, sma50)
//  ecart_max           : donner l'écart max des ordre de la strat envelope
//  nb_ordre            : donner le nombre d'ordre entre smma et le ecart_max
//  b_close             : sur quoi on calcule close[]
//  b_high              : sur quoi on calcule high[]
//  b_low               : sur quoi on calcule low[]
//  nb_ligne            : longueur du tableau source
//  bougie_skip         : nombre de bougie a skip 50 pour smma50
//  wallet              : nombre d'argent de départ
//  frais               : le multiple pour compte les frais
// ============================================================

void short_short(
    double *trade,
    double *palier,
    double *wallet_usd,
    double *wallet_actif,
    double *moy,
    int     nb_ordre,
    double *b_close,
    double *b_high,
    double *b_low,
    double  frais,
    int    *palier_high,
    int    *palier_low,
    int    *palier_pos,
    int     i) {    
    if (palier_high[i] > *palier_pos ){          //si le high est aller au minumum 1 palier au dessus de close
        while (abs(*palier_pos) + 1 <= nb_ordre){                                    //tant qu'on dépasse pas le nb ordre max, en ajoutant ad
            double niveau_short = moy[i-1] * (1.0 + *palier * (*palier_pos + 1));    //on détermine le prix du short

            if (b_high[i] > niveau_short) {                                         //si le plus haut dépasse le niveau de short donc si on est fil

                double taille_ordre = *wallet_usd / (nb_ordre -(abs(*palier_pos)));    //donne la taille des ordre en usdc quand on ce fait fill

                *trade += taille_ordre * (niveau_short/b_close[i]);            // trade c'est un wallet temporaire pour comter tout les trade que on fait dans la bougie, pour calculer indépendament le wallet_actif 
                *wallet_usd -= taille_ordre + (taille_ordre-(taille_ordre*frais));           
                *palier_pos += 1;

                printf("SHORT à %.2f | pos=%.4f\n", niveau_short, taille_ordre);
            } else {
                break; 
            }
        }
    }
}

void short_long(
    double *trade,
    double *palier,
    double *wallet_usd,
    double *wallet_actif,
    double *moy,
    int     nb_ordre,
    double *b_close,
    double *b_high,
    double *b_low,
    double  frais,
    int    *palier_high,
    int    *palier_low,
    int    *palier_pos,
    int     i) {    
    
    palier_pos   = 0;
    int ad = 1;
    *wallet_actif = (*wallet_actif * (b_close[i-1]/moy[i-1]))*frais;  //on ferme la posision short au prix de la smma
    *wallet_usd = *wallet_usd + *wallet_actif;                          // on remet le wallet full usdc car on était en short et le prix et passer en dessous de smma
    *wallet_actif = 0;
    while (abs(ad) <= nb_ordre){                                    //tant qu'on dépasse pas le nb ordre max, en ajoutant ad
        double niveau_long = moy[i-1] * (1.0 - *palier * ad);    //on détermine le prix du short

        if (b_low[i] < niveau_long) {                                         //si le plus haut dépasse le niveau de short donc si on est fil

            double taille_ordre = *wallet_usd / (nb_ordre -(abs(*palier_pos)));    //donne la taille des ordre en usdc quand on ce fait fill

            *trade += taille_ordre * (niveau_long/b_close[i]);            // trade c'est un wallet temporaire pour comter tout les trade que on fait dans la bougie, pour calculer indépendament le wallet_actif 
            *wallet_usd -= taille_ordre + (taille_ordre-(taille_ordre*frais));
            ad++;
            *palier_pos -= 1;

            printf("LONG à %.2f | pos=%.4f\n", niveau_long, taille_ordre);
        } else {
            break; 
        }
    }
}

void long_long(
    double *trade,
    double *palier,
    double *wallet_usd,
    double *wallet_actif,
    double *moy,
    int     nb_ordre,
    double *b_close,
    double *b_high,
    double *b_low,
    double  frais,
    int    *palier_high,
    int    *palier_low,
    int    *palier_pos,
    int     i) {
    if (palier_low[i] < palier_pos ){          //si le low est aller au minumum 1 palier en dessous de close
        while (abs(*palier_pos) + 1 <= nb_ordre){                                    //tant qu'on dépasse pas le nb ordre max, en ajoutant ad
            double niveau_long = moy[i-1] * (1.0 - *palier * (abs(*palier_pos) + 1));    //on détermine le prix du long

            if (b_low[i] < niveau_long) {                                         //si le plus haut dépasse le niveau de short donc si on est fil

                double taille_ordre = *wallet_usd / (nb_ordre -(abs(*palier_pos)));    //donne la taille des ordre en usdc quand on ce fait fill

                *trade += taille_ordre * (b_close[i]/niveau_long);            // trade c'est un wallet temporaire pour comter tout les trade que on fait dans la bougie, pour calculer indépendament le wallet_actif 
                *wallet_usd -= taille_ordre + (taille_ordre-(taille_ordre*frais));                
                *palier_pos -= 1;

                printf("LONG à %.2f | pos=%.4f\n", niveau_long, taille_ordre);
            } else {
                break; 
            }        
        }
    }
}

void long_short(
    double *trade,
    double *palier,
    double *wallet_usd,
    double *wallet_actif,
    double *moy,
    int     nb_ordre,
    double *b_close,
    double *b_high,
    double *b_low,
    double  frais,
    int    *palier_high,
    int    *palier_low,
    int    *palier_pos,
    int     i) {

    palier_pos   = 0;
    int ad = 1;
    *wallet_actif = (*wallet_actif * (moy[i-1]/b_close[i-1])) * frais;  //on ferme la posision long au prix de la smma
    *wallet_usd = *wallet_usd + *wallet_actif;                          // on remet le wallet full usdc car on était en long et le prix et passer en au dessus de smma
    *wallet_actif = 0;
    while (abs(ad) <= nb_ordre){                                    //tant qu'on dépasse pas le nb ordre max, en ajoutant ad
        double niveau_short = moy[i-1] * (1.0 + *palier * ad);    //on détermine le prix du short

        if (b_high[i] > niveau_short) {                                         //si le plus haut dépasse le niveau de short donc si on est fil

            double taille_ordre = *wallet_usd / (nb_ordre -(abs(*palier_pos)));    //donne la taille des ordre en usdc quand on ce fait fill

            *trade += taille_ordre * (niveau_short/b_close[i]);            // trade c'est un wallet temporaire pour comter tout les trade que on fait dans la bougie, pour calculer indépendament le wallet_actif 
            *wallet_usd -= taille_ordre + (taille_ordre-(taille_ordre*frais));       //on retire la valeur de l'ordre du *wallet_usd
            ad++;
            *palier_pos += 1;

            printf("SHORT à %.2f | pos=%.4f\n", niveau_short, taille_ordre);
        } else {
            break; 
        }        
    }

}

double strat_enveloppes(
    double *moy,
    double  ecart_max,
    int     nb_ordre,
    double *b_close,
    double *b_high,
    double *b_low,
    int     nb_ligne,
    int     bougie_skip,
    double  wallet,
    double  frais) {

    double palier = ecart_max / nb_ordre;
    double wallet_usd = wallet;
    double wallet_actif = 0;        //wallet de i-1 des actif

    int *palier_high  = (int*)calloc(nb_ligne, sizeof(int));
    int *palier_low   = (int*)calloc(nb_ligne, sizeof(int));
    int  palier_pos   = 0;
    
    if (!palier_high || !palier_low) {
        fprintf(stderr, "Erreur d'allocation mémoire\n");
        free(palier_high);
        free(palier_low);
        return -1.0;
    }

    for (int i = bougie_skip; i < nb_ligne; i++){

        palier_high[i]  = (int)((b_high[i]  - moy[i]) / palier);        //faire un palier high et low pour strat enveloppe classique
        palier_low[i]   = (int)((b_low[i]   - moy[i]) / palier);        //si on est en long c'est négatif si on est en short c'est positif

        
        if (b_close[i-1] > moy[i-1]){                                     //short  
            double trade = 0;  
            short_short(&trade, &palier, &wallet_usd, &wallet_actif,
                        moy, nb_ordre, b_close, b_high, b_low,
                        frais, palier_high, palier_low, i);
            if (palier_low[i] <= 0 ){          //si le low est aller en dessous de la smma donc on ferme la position
                short_long (&trade, &palier, &wallet_usd, &wallet_actif,
                            moy, nb_ordre, b_close, b_high, b_low,
                            frais, palier_high, palier_low, i);
            }
            else {
                wallet_actif = wallet_actif * (b_close[i-1]/b_close[i]);
                wallet_actif = wallet_actif + trade;
            }
        }

        else if (b_close[i-1] < moy[i-1]){                                     //long
            double trade = 0;
            long_long (&trade, &palier, &wallet_usd, &wallet_actif,
                       moy, nb_ordre, b_close, b_high, b_low,
                       frais, palier_high, palier_low, i);     
            if (palier_high[i] >= 0 ){                                      //si le high est aller au dessus de smma donc on ferme la position
                long_short(&trade, &palier, &wallet_usd, &wallet_actif,
                        moy, nb_ordre, b_close, b_high, b_low,
                        frais, palier_high, palier_low, i);
            }
            else{
                wallet_actif = wallet_actif * (b_close[i]/b_close[i-1]);
                wallet_actif = wallet_actif + trade;
            }
        }
    }
    free(palier_high);
    free(palier_low);
    return wallet_usd + wallet_actif;
}
    
