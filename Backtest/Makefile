# Makefile — moteur de backtest L2
#
#   make              -> bin/backtest bin/info bin/probas bin/extract
#   make OMP=1        -> extract parallele (OpenMP)
#   make DEBUG=1      -> -O0 -g, asserts actifs
#   make clean
#
# Ajouter une strategie = deposer strategies/<nom>/<nom>.c (+ une ligne dans
# registry.c). Le Makefile ramasse tout strategies/*/*.c automatiquement.

CC      ?= gcc
CFLAGS  ?= -O2 -std=gnu11 -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -Icore -Istrategies
STRAT_DIRS := $(sort $(dir $(wildcard strategies/*/*.c)))
CFLAGS  += $(addprefix -I,$(STRAT_DIRS))
LDLIBS   = -lm

ifeq ($(DEBUG),1)
  CFLAGS := $(filter-out -O2,$(CFLAGS)) -O0 -g
else
  CFLAGS += -DNDEBUG
endif
ifeq ($(OMP),1)
  CFLAGS += -fopenmp
  LDLIBS += -fopenmp
endif

# .exe sous MSYS2 / MinGW
ifeq ($(OS),Windows_NT)
  EXE := .exe
else
  EXE :=
endif

BIN   := bin
BUILD := build

CORE_SRC  := core/book.c core/indicators.c core/engine.c
STRAT_SRC := strategies/registry.c $(wildcard strategies/*/*.c)

CORE_OBJ  := $(patsubst %.c,$(BUILD)/%.o,$(CORE_SRC))
STRAT_OBJ := $(patsubst %.c,$(BUILD)/%.o,$(STRAT_SRC))

TARGETS := $(BIN)/backtest$(EXE) $(BIN)/info$(EXE) $(BIN)/probas$(EXE) $(BIN)/extract$(EXE)

all: $(TARGETS)

$(BIN)/backtest$(EXE): $(BUILD)/core/backtest.o $(CORE_OBJ) $(STRAT_OBJ) | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BIN)/info$(EXE): $(BUILD)/core/info.o $(BUILD)/core/book.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BIN)/probas$(EXE): $(BUILD)/core/probabilities.o $(BUILD)/core/book.o $(BUILD)/core/indicators.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

$(BIN)/extract$(EXE): $(BUILD)/core/extract.o | $(BIN)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS) -lz

$(BUILD)/%.o: %.c core/l2.h strategies/strategy.h $(wildcard strategies/*/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN):
	@mkdir -p $(BIN)

clean:
	rm -rf $(BUILD) $(TARGETS)

.PHONY: all clean
