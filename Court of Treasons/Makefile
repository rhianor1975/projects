CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -O2
SRC    = src/data.c src/rules.c src/phases.c src/ai.c src/seat.c src/wire.c src/net.c src/serve.c src/sha256.c src/users.c src/lobby.c src/main.c
OBJ    = $(SRC:.c=.o)
BIN    = court

all: $(BIN)
$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

# "Generated card tables are declared build dependencies, so that a table
# cannot be regenerated without relinking the binary that reads it."  In
# the sibling project one of these was left out, a table was regenerated
# without relinking, and an A/B test spent an afternoon comparing a binary
# against itself.
$(OBJ):     src/court.h
src/data.o: src/cards.inc

# The rules exist only where the design does: design.xml is raw material,
# cards.inc is committed, and a checkout without the XML still builds.
ifneq ($(wildcard CARDS.tsv),)
# Written to a temporary and moved on success.  Redirecting straight to
# the target truncates it to nothing the moment the classifier refuses a
# card, so one unreadable effect took out the whole table and the next
# build failed for a different reason than the real one.
src/cards.inc: CARDS.tsv gen-cards.py
	@python3 gen-cards.py > $@.tmp && mv $@.tmp $@ || { rm -f $@.tmp; exit 1; }
endif
# Generated again, as of 2026-09-20.  It stopped being generated when
# the pool conversion wrote 1,037 cards into CARDS.tsv while design.xml
# still held the 157 the design calls a seed; with this rule live and
# the two out of step, editing the authority regenerated the live set
# from a sixth of it.  All 1,037 are now in design.xml, so the rule is
# true again and the two cannot drift.
#
# extract-cards.py still refuses to write a table with fewer rows than
# the one already there.  That guard is the reason this is safe to
# turn back on rather than a thing made redundant by it.
ifneq ($(wildcard design.xml),)
CARDS.tsv HOUSES.tsv: design.xml extract-cards.py
	python3 extract-cards.py
endif

san: clean
	$(MAKE) CFLAGS="-std=c99 -Wall -Wextra -O1 -g \
	  -fsanitize=address,undefined -fno-omit-frame-pointer"

# 200 games with the invariants armed.  An INV line is a bug, never a
# quirk, so any at all fails the target.
check: $(BIN)
	@rm -f /tmp/court-inv.log; i=1; while [ $$i -le 200 ]; do \
	  COURT_CHECK=1 COURT_SEED=$$i COURT_HOUSES=$$(( $$i % 3 + 2 )) \
	  COURT_TRACE=/tmp/court-check-$$i.log ./$(BIN) >/dev/null 2>>/tmp/court-inv.log \
	    || { echo "seed $$i crashed (stderr in /tmp/court-inv.log)"; exit 1; }; \
	  i=$$((i+1)); done; \
	n=`cat /tmp/court-check-*.log 2>/dev/null | grep -c '^INV ' || true`; \
	s=`grep -ciE 'runtime error|AddressSanitizer|UndefinedBehaviorSanitizer|LeakSanitizer' /tmp/court-inv.log 2>/dev/null || true`; \
	rm -f /tmp/court-check-*.log; \
	if [ "$$n" -gt 0 ]; then echo "$$n invariant failures"; exit 1; fi; \
	if [ "$$s" -gt 0 ]; then \
	  echo "$$s sanitizer diagnostics -- kept in /tmp/court-inv.log:"; \
	  grep -iE 'runtime error|Sanitizer' /tmp/court-inv.log | sort -u | head -5; \
	  exit 1; fi; \
	rm -f /tmp/court-inv.log; \
	echo "200 games, no invariant broken"

# Everything the engine's stderr said used to go to /tmp/court-inv.log
# and then be deleted unread.  ASan aborts, so a crash was still caught
# by the exit code -- but UBSan only prints and carries on, so a whole
# class of diagnostic was collected and thrown away.  The log is now
# read, and kept when it has anything in it.

soak: $(BIN)
	@./tools/soak.sh 2000 1

clean:
	rm -f $(OBJ) $(BIN)
.PHONY: all clean san check soak test test-all

# Unit tests over the tables and the View: what can be checked without
# playing a game.  `make check` plays 200 with the invariants armed.
test: src/tests.c src/data.o src/rules.o src/phases.o src/ai.o src/seat.o src/court.h
	@$(CC) $(CFLAGS) -o /tmp/court-tests src/tests.c src/data.o \
	  src/rules.o src/phases.o src/ai.o src/seat.o
	@/tmp/court-tests

help:
	@python3 tools/mkhelp.py

test-all: $(BIN)
	@echo "== help vs design";  $(MAKE) -s help
	@echo "== unit tests";       $(MAKE) -s test
	@echo "== rules invariants"; $(MAKE) -s check
	@echo "== the wire";         python3 tools/wire-test.py | tail -1
	@echo "== over a socket";    python3 tools/net-test.py 2>/dev/null | tail -2
	@echo "== text fits";      ./tools/fit-test.sh
	@echo "== action labels";   python3 tools/label-test.py 4 | tail -2
	@echo "== two people";      python3 tools/pvp-test.py | tail -5
	@echo "== the room";        python3 tools/lobby-test.py | tail -6
	@echo "== the batch";        ./tools/soak.sh 400 1 | tail -22
