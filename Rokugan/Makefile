CC     = cc
CFLAGS = -std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O2
SRC    = src/data.c src/rules.c src/ai.c src/view.c src/decks.c src/main.c
OBJ    = $(SRC:.c=.o)
BIN    = rokugan
ERAS   = gold celestial ivory

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

$(OBJ): src/rokugan.h

# What a fresh clone needs before anything will run: the card pool and
# the images, from the Oracle of the Void, into data/ (gitignored -- the
# text and the images are AEG's).  A few minutes for the cards; the
# images take longer and are only needed by the client.
setup:
	python3 tools/import-ootv.py fetch
	python3 tools/build-cards.py $(ERAS)
	python3 tools/import-ootv.py images $(ERAS)

cards:
	python3 tools/build-cards.py $(ERAS)

san: clean
	$(MAKE) CFLAGS="-std=c99 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -O1 -g \
	  -fsanitize=address,undefined -fno-omit-frame-pointer"

# 200 machine games per era with the invariants armed.  An INV line is a
# bug, never a quirk, so any at all fails the target.
check: $(BIN)
	@for e in $(ERAS); do \
	  ROKUGAN_CHECK=1 ./$(BIN) --era $$e --games 200 --seed 1 2>/tmp/rokugan-inv.log \
	    || { echo "$$e: crashed"; exit 1; }; \
	  n=`grep -c '^INV' /tmp/rokugan-inv.log || true`; \
	  if [ "$$n" -gt 0 ]; then echo "$$e: $$n invariant failures:"; \
	    sort /tmp/rokugan-inv.log | uniq -c | sort -rn | head -5; exit 1; fi; \
	done; rm -f /tmp/rokugan-inv.log; echo "no invariant broken"

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all setup cards san check clean
