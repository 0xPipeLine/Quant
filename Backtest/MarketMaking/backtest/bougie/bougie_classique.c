#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bougie_classique.h"

// ============================================================
// VARIABLES GLOBALES
// ============================================================

struct bougie*ma_bougie = NULL;
double *close;
double *high;
double *low;
int line_count = 0;


// ============================================================
// charger_bougies
// Lit le CSV principal (timestamp,open,high,low,close,volume,...)
// Alloue ma_bougie et remplit chaque champ
// ============================================================

int charger_bougies(const char *chemin) {
    char line[80];
    int  colone = 0;

    FILE *f = fopen(chemin, "r");
    if (f == NULL) {
        printf("Erreur ouverture fichier : %s\n", chemin);
        return 0;
    }

    // Compter les lignes pour le malloc
    while (fgets(line, sizeof(line), f))
        line_count++;

    ma_bougie = malloc(line_count * sizeof(struct bougie));
    close = malloc (line_count * sizeof(double));
    high = malloc (line_count * sizeof(double));
    low = malloc (line_count * sizeof(double));


    if (ma_bougie == NULL) {
        printf("Erreur malloc ma_bougie\n");
        fclose(f);
        return 0;
    }

    rewind(f);
    int ligne = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        char *token = strtok(line, ",");

        while (token != NULL) {
            if      (colone == 0)  ma_bougie[ligne].open_ts = atol(token);
            else if (colone == 1)  ma_bougie[ligne].open    = atof(token);
            else if (colone == 2)  {ma_bougie[ligne].high   = atof(token);  high[ligne]     = atof(token);}
            else if (colone == 3)  {ma_bougie[ligne].low    = atof(token);  low[ligne]      = atof(token);}         
            else if (colone == 4)  {ma_bougie[ligne].close  = atof(token);  close[ligne]    = atof(token);}
            else if (colone == 5)  ma_bougie[ligne].volume  = atof(token);

            token = strtok(NULL, ",");
            colone++;
        }
        colone = 0;
        ligne++;
    }

    fclose(f);
    return line_count;
}


// ============================================================
// liberer_bougies
// ============================================================

void liberer_bougies(void) {
    free(ma_bougie);
    free(close);
    free(high);
    free(low);
    ma_bougie   = NULL;
    close       = NULL;
    high        = NULL;
    low         = NULL;
}
