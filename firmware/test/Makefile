# Host build of the topology-independent merge/report code + tests.
# Requires only a C99 compiler. Run: make test

CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Werror -g
MERGE   := ../components/merge
JOYCON  := ../components/joycon
INCLUDES := -I$(MERGE)/include -I$(JOYCON)/include -I.

SRCS := \
	$(MERGE)/merge_engine.c \
	$(MERGE)/xbox_report.c \
	$(JOYCON)/joycon_decode.c \
	test_merge.c \
	test_xbox_report.c \
	test_joycon_decode.c \
	test_main.c

BUILD := build
BIN   := $(BUILD)/run_tests

.PHONY: test clean
test: $(BIN)
	$(BIN)

$(BIN): $(SRCS) $(MERGE)/include/*.h $(JOYCON)/include/*.h minunit.h | $(BUILD)
	$(CC) $(CFLAGS) $(INCLUDES) $(SRCS) -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
