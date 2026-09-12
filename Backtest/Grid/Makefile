# Fonctionne que les sources soient dans src/ + include/ ou toutes a plat.
SRC := $(if $(wildcard src/engine.c),src/,)
INC := $(if $(wildcard include/l2.h),include,.)

CC     = gcc
CFLAGS = -O2 -Wall -Wextra -I$(INC)
LDLIBS = -lm
CORE   = $(SRC)book.c $(SRC)indicators.c

# L'extracteur est parallele par defaut (la decompression domine le temps).
# make OMP=0  si ta chaine de compilation n'a pas OpenMP.
OMP ?= 1
ifneq ($(OMP),0)
  OMPFLAGS = -fopenmp
endif

# make STATIC=1 -> executables autonomes (aucune DLL a cote, pratique sous
# Windows pour lancer les .exe depuis n'importe quel terminal)
ifdef STATIC
  LDFLAGS += -static
endif

all: extract info backtest probas

extract:  $(SRC)extract.c
	$(CC) $(CFLAGS) $(OMPFLAGS) -o $@ $^ -lz $(LDLIBS)

info:     $(SRC)info.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

STRATS = $(SRC)strategy_envelope.c $(SRC)strategy_envelope_b.c $(SRC)strategy_grid.c

backtest: $(SRC)backtest.c $(SRC)engine.c $(STRATS) $(CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

probas:   $(SRC)probabilities.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f extract info backtest probas

.PHONY: all clean
