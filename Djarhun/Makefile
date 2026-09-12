CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -Wno-missing-field-initializers -O2
LDLIBS = -lncurses
SRC    = src/board.c src/data.c src/ui.c src/game.c
OBJ    = $(SRC:.c=.o)
BIN    = djarhun

all: $(BIN)
$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)
$(OBJ): src/djarhun.h
src/data.o: src/cards.inc src/heroes.inc

# The card table is generated, but src/cards.inc is committed and the TSV it
# comes from is raw material that does not travel with the source.  So the
# rule only exists where the TSV does; elsewhere the checked-in table stands.
ifneq ($(wildcard HEROES.tsv),)
src/heroes.inc: HEROES.tsv gen-heroes.py
	python3 gen-heroes.py > $@
endif

ifneq ($(wildcard CARDS-djarhun.tsv),)
src/cards.inc: CARDS-djarhun.tsv gen-cards.py
	python3 gen-cards.py > $@
endif
# Sanitisers find what the soak cannot: reads off the end of a table that
# happen to land on something harmless.  Two of this game's bugs were that.
san: clean
	$(MAKE) CFLAGS="-std=c99 -Wall -Wextra -Wno-missing-field-initializers -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"

check: $(BIN)
	@rm -f /tmp/djarhun-check.log; i=1; \
	while [ $$i -le 20 ]; do \
	  TERM=xterm LINES=40 COLUMNS=120 DJARHUN_AUTO=1 DJARHUN_CHECK=1 \
	  DJARHUN_SEED=$$i DJARHUN_TRACE=/tmp/djarhun-check-$$i.log \
	  ./$(BIN) >/dev/null 2>&1 || { echo "seed $$i crashed"; exit 1; }; \
	  i=$$((i+1)); done; \
	n=`cat /tmp/djarhun-check-*.log | grep -c '^INV ' || true`; \
	rm -f /tmp/djarhun-check-*.log; \
	if [ "$$n" -gt 0 ]; then echo "$$n invariant failures"; exit 1; \
	else echo "20 games, no invariant broken"; fi

clean:
	rm -f $(OBJ) $(BIN)
.PHONY: all clean san check
