# ============================================================
# Makefile -- Blockchain-Based Library Book Lending Tracker
#
#   make            build the library_tracker executable
#   make test       build and run every unit test in tests/
#   make seed       copy sample_data/ into data/ (first run only)
#   make run        seed, build and start the CLI
#   make clean      remove build products
#   make reset      delete data/ (chain, keys, state) and re-seed it
# ============================================================

.RECIPEPREFIX = >

CC      ?= gcc
CFLAGS  = -Wall -Wextra -std=c11 -g -Iinclude
LDLIBS  = -lssl -lcrypto

BUILD   = build
TARGET  = library_tracker

# Files that make up the command-line front end; everything else in
# src/ is a reusable module that the unit tests link against.
APP_SRC = $(wildcard src/main.c src/cli.c src/cmd_*.c)
LIB_SRC = $(filter-out $(APP_SRC),$(wildcard src/*.c))
LIB_OBJ = $(patsubst src/%.c,$(BUILD)/%.o,$(LIB_SRC))
APP_OBJ = $(patsubst src/%.c,$(BUILD)/%.o,$(APP_SRC))

TEST_SRC = $(wildcard tests/test_*.c)
TEST_BIN = $(patsubst tests/%.c,$(BUILD)/%,$(TEST_SRC))

.PHONY: all lib test seed run clean reset

all: lib
ifneq ($(APP_SRC),)
all: $(TARGET)
endif

lib: $(LIB_OBJ)

$(TARGET): $(LIB_OBJ) $(APP_OBJ)
> $(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o: src/%.c $(wildcard include/*.h) | $(BUILD)
> $(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test_%: tests/test_%.c $(LIB_OBJ) | $(BUILD)
> $(CC) $(CFLAGS) $< $(LIB_OBJ) -o $@ $(LDLIBS)

$(BUILD):
> mkdir -p $(BUILD)

test: $(TEST_BIN)
> @fail=0; for t in $(TEST_BIN); do \
>     echo "== $$t"; ./$$t || fail=1; \
> done; \
> if [ $$fail -eq 0 ]; then echo "ALL TESTS PASSED"; else echo "SOME TESTS FAILED"; exit 1; fi

seed:
> @if [ ! -d data ]; then cp -r sample_data data && echo "Seeded data/ from sample_data/"; \
> else echo "data/ already exists (use 'make reset' to start over)"; fi

run: seed all
> ./$(TARGET) data

clean:
> rm -rf $(BUILD) $(TARGET)

reset:
> rm -rf data
> cp -r sample_data data
> @echo "data/ reset to the sample registries; chain, keys and ledgers removed."
