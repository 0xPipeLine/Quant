#include <stdio.h>
#include <stdlib.h>
#include "sma.h"

// ============================================================
// calculer_sma
//
//  periode             : fenêtre de lissage
//  ma_bougie           : tableau de valeurs (ex: rsi[], close[], high[], low[])
//  nb_ligne            : longueur du tableau source
// ============================================================

double *cal_sma (int periode, double *ma_bougie, int nb_ligne){
    double somme = 0;

    double *moyenne;
    moyenne = malloc (nb_ligne * sizeof(double));
    //FILE *a = fopen (fichier_sortie, "w");

    for (int i = 0 ; i < periode -1 ; i++){                  //calculer les moyenne des x premier terme
        for (int j = 0 ; j <= i ; j++){
            somme = somme + ma_bougie[i-j];
        }
        moyenne[i] = somme / (i+1);
        somme = 0;        
        //fprintf (a, "%.2f\n", moyenne[i]);
    }

    for (int i = periode - 1 ; i < nb_ligne ; i++){       //calculer les moyenne des terme suivant
        for (int j=0 ; j < periode ; j++){
            somme = somme + ma_bougie[i-j];
        }
        moyenne[i] = somme / periode;
        somme = 0;
        //printf("Ligne %d (timestamp: %d) - Moyenne: %.2f\n", i + 1, ma_bougie[i].timestamp, moyenne[i]);
        //fprintf (a, "%.2f\n", moyenne[i]);
    }
    //fclose(a);
    return moyenne;
}
