#include <stdio.h>
#include <stdlib.h>
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



double *strat_enveloppes (double*moy, double ecart_max, int nb_ordre, double*b_close, double *b_high, double *b_low, int nb_ligne, int bougie_skip, double wallet, double frais){

    double palier = ecart_max / nb_ordre;
    double wallet_usd = wallet;
    double wallet_actif = 0;        //wallet de i-1 des actif
    int palier_close[nb_ligne] = 0;
    int palier_high[nb_ligne] = 0;
    int palier_low[nb_ligne] = 0;

    for (int i = bougie_skip-1; i < nb_ligne; i++){

        palier_close[i] = (b_close[i] - moy[i])/(ecart_max/nb_ordre);           //si on est en long c'est négatif si on est en short c'est positif
        palier_high[i] = (b_high[i] - moy[i])/(ecart_max/nb_ordre);           //faire un palier high et low pour strat enveloppe classique
        palier_low[i] = (b_low[i] - moy[i])/(ecart_max/nb_ordre);
        
        double taille_ordre = wallet_usd / (nb_ordre -(abs(palier_close[i-1])));    //donne la taille des ordre en usdc quand on ce fait fill
        
        /*
        if (palier_close[i-1] > 0){                     //short mais le if est pas possible si on passe par smma
            wallet_actif = wallet_actif * (b_close[i-1]/b_close[i]);
        }
        if (palier_close[i-1] < 0){                     //long mais le if est pas possible si on passe par smma
            wallet_actif = wallet_actif * (b_close[i]/b_close[i-1]);
        }
        */
        
        
                                        // 🔴 SHORT
        if (b_close[i-1] > moy[i-1]){
        //if (palier_close[i-1] < palier_close[i]){           //pour savoir si le prix monte ou dessens, si il monte on comte d'abord les long et après short si il dessend on compte d'abord les short et après les long

            if (palier_high[i] > palier_close[i-1] ){          //si le high est aller au minumum 1 palier au dessus de close
                int ad = 1;
                double trade = 0;
                while (abs(palier_close[i-1]) + abs(ad) <= nb_ordre){                                    //tant qu'on dépasse pas le nb ordre max, en ajoutant ad
                    double niveau_short = moy[i-1] * (1.0 + palier * (palier_close[i-1]+ad));    //on détermine le prix du short

                    if (b_high[i] > niveau_short) {                                         //si le plus haut dépasse le niveau de short donc si on est fil

                        double taille_ordre = wallet_usd / (nb_ordre -(abs(palier_close[i-1])));    //donne la taille des ordre en usdc quand on ce fait fill

                        trade = trade + taille_ordre * (niveau_short/b_close[i]);            // trade c'est un wallet temporaire pour comter tout les trade que on fait dans la bougie, pour calculer indépendament le wallet_actif 
                        wallet_usd -= taille_ordre + (taille_ordre-(taille_ordre*frais));
                        ad++;

                        printf("SHORT à %.2f | pos=%.4f\n", niveau_short, taille_ordre);
                    }
                }
                wallet_actif = wallet_actif + trade;
            }
            

            //if (palier_close[i-1] > palier_close[i]){           //pour savoir si le prix monte ou dessens, si il monte on comte d'abord les long et après short si il dessend on compte d'abord les short et après les long

            if (palier_low[i] <= 0 ){          //si le low est aller en dessous de la smma donc on ferme la position
                int ad = -1;
                double trade = 0;
                wallet_actif = (wallet_actif * (b_close[i-1]/moy[i-1]))*frais;  //on ferme la posision short au prix de la smma

                wallet_usd = wallet_usd + wallet_actif;                          // on remet le wallet full usdc car on était en short et le prix et passer en dessous de smma
                wallet_actif = 0;
                while (abs(ad) <= nb_ordre){                                    //tant qu'on dépasse pas le nb ordre max, en ajoutant ad
                    double niveau_long = moy[i-1] * (1.0 - palier * ad);    //on détermine le prix du short

                    if (b_low[i] < niveau_long) {                                         //si le plus haut dépasse le niveau de short donc si on est fil

                        double taille_ordre = wallet_usd / (nb_ordre -(abs(palier_close[i-1])));    //donne la taille des ordre en usdc quand on ce fait fill

                        trade = trade + taille_ordre * (niveau_long/b_close[i]);            // trade c'est un wallet temporaire pour comter tout les trade que on fait dans la bougie, pour calculer indépendament le wallet_actif 
                        wallet_usd -= taille_ordre + (taille_ordre-(taille_ordre*frais));
                        ad++;

                        printf("LONG à %.2f | pos=%.4f\n", niveau_long, taille_ordre);
                    }
                }
                wallet_actif = wallet_actif + trade;
            }
        }
                                // 🔵 LONG

        if (b_close[i-1] < moy[i-1]){
        //if (palier_close[i-1] < palier_close[i]){           //pour savoir si le prix monte ou dessens, si il monte on comte d'abord les long et après short si il dessend on compte d'abord les short et après les long

            if (palier_low[i] < palier_close[i-1] ){          //si le low est aller au minumum 1 palier en dessous de close
                int ad = -1;
                double trade = 0;
                while (abs(palier_close[i-1]) + abs(ad) <= nb_ordre){                                    //tant qu'on dépasse pas le nb ordre max, en ajoutant ad
                    double niveau_long = moy[i-1] * (1.0 - palier * (palier_close[i-1]+ad));    //on détermine le prix du long

                    if (b_low[i] < niveau_long) {                                         //si le plus haut dépasse le niveau de short donc si on est fil

                        double taille_ordre = wallet_usd / (nb_ordre -(abs(palier_close[i-1])));    //donne la taille des ordre en usdc quand on ce fait fill

                        trade = trade + taille_ordre * (b_close[i]/niveau_long);            // trade c'est un wallet temporaire pour comter tout les trade que on fait dans la bougie, pour calculer indépendament le wallet_actif 
                        wallet_usd -= taille_ordre + (taille_ordre-(taille_ordre*frais));
                        ad++;

                        printf("LONG à %.2f | pos=%.4f\n", niveau_long, taille_ordre);
                    }
                }
                wallet_actif = wallet_actif + trade;
            }
            
            //if (palier_close[i-1] > palier_close[i]){           //pour savoir si le prix monte ou dessens, si il monte on comte d'abord les long et après short si il dessend on compte d'abord les short et après les long

            if (palier_high[i] >= 0 ){          //si le high est aller au dessus de smma donc on ferme la position
                int ad = 1;
                double trade = 0;
                wallet_actif = (wallet_actif * (moy[i-1]/b_close[i-1]))*frais;  //on ferme la posision long au prix de la smma

                wallet_usd = wallet_usd + wallet_actif;                          // on remet le wallet full usdc car on était en long et le prix et passer en au dessus de smma
                wallet_actif = 0;
                while (abs(ad) <= nb_ordre){                                    //tant qu'on dépasse pas le nb ordre max, en ajoutant ad
                    double niveau_short = moy[i-1] * (1.0 + palier * ad);    //on détermine le prix du short

                    if (b_high[i] > niveau_short) {                                         //si le plus haut dépasse le niveau de short donc si on est fil

                        double taille_ordre = wallet_usd / (nb_ordre -(abs(palier_close[i-1])));    //donne la taille des ordre en usdc quand on ce fait fill

                        trade = trade + taille_ordre * (niveau_short/b_close[i]);            // trade c'est un wallet temporaire pour comter tout les trade que on fait dans la bougie, pour calculer indépendament le wallet_actif 
                        wallet_usd -= taille_ordre + (taille_ordre-(taille_ordre*frais));       //on retire la valeur de l'ordre du wallet_usd
                        ad++;

                        printf("SHORT à %.2f | pos=%.4f\n", niveau_short, taille_ordre);
                    }
                }
                wallet_actif = wallet_actif + trade;
            }
        }
    }
    return wallet_usd + wallet_actif;
    
}