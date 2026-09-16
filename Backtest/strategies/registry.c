/* registry.c — la seule liste des strategies disponibles. */
#include <stdio.h>
#include <string.h>
#include "strategy.h"

extern const StratDef strat_envelope;
extern const StratDef strat_grid;
extern const StratDef strat_dumb;
extern const StratDef strat_touch;
extern const StratDef strat_stoikov;
extern const StratDef strat_volband;

static const StratDef *ALL[] = {
    &strat_envelope,
    &strat_grid,
    &strat_dumb,
    &strat_touch,
    &strat_stoikov,
    &strat_volband,
    NULL
};

const StratDef *strat_find(const char *name)
{
    for (int i = 0; ALL[i]; i++)
        if (!strcmp(ALL[i]->name, name)) return ALL[i];
    return NULL;
}

void strat_list(FILE *f)
{
    for (int i = 0; ALL[i]; i++)
        fprintf(f, "  %-12s %s\n", ALL[i]->name, ALL[i]->summary);
}
