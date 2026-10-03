CC := cc
CFLAGS := -std=c11 -Wall -Wextra -O2
LDFLAGS := -lncurses
SRC := $(wildcard src/*.c)
OBJ := $(SRC:src/%.c=build/%.o)
BIN := bin/aether-descent

# notcurses build. Separate object dir and binary so the two backends never
# share stale objects. Needs a terminal that answers capability queries --
# it will refuse to start under a pipe or a detached tmux session.
NC_PREFIX  ?= $(shell brew --prefix notcurses 2>/dev/null || echo /usr/local)
NC_CFLAGS  := -DTUI_NOTCURSES -I$(NC_PREFIX)/include
NC_LDFLAGS := -L$(NC_PREFIX)/lib -lnotcurses -lnotcurses-core
NC_OBJ     := $(SRC:src/%.c=build-nc/%.o)
NC_BIN     := bin/aether-descent-nc

# Test binaries link against the real game modules (never a copy of the
# logic), minus main.c so there's no duplicate main().
TEST_SRC := $(wildcard tests/*.c)
TEST_BIN := $(TEST_SRC:tests/%.c=bin/test-%)
GAME_OBJ := $(filter-out build/main.o,$(OBJ))

.PHONY: all clean run test asan notcurses run-nc art serve serve-nc trace sim shot

all: $(BIN)

notcurses: $(NC_BIN)

$(NC_BIN): $(NC_OBJ) | bin
	$(CC) $(NC_OBJ) -o $(NC_BIN) $(NC_LDFLAGS)

build-nc/%.o: src/%.c src/*.h | build-nc
	$(CC) $(CFLAGS) $(NC_CFLAGS) -c $< -o $@

build-nc:
	mkdir -p build-nc

run-nc: notcurses
	./$(NC_BIN)

$(BIN): $(OBJ) | bin
	$(CC) $(OBJ) -o $(BIN) $(LDFLAGS)

build/%.o: src/%.c src/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

bin:
	mkdir -p bin

run: all
	./$(BIN)

bin/test-%: tests/%.c $(GAME_OBJ) | bin
	$(CC) $(CFLAGS) $< $(GAME_OBJ) -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	@fail=0; for t in $(TEST_BIN); do \
		echo "=== $$t ==="; ./$$t || fail=1; echo; \
	done; exit $$fail

# Renders the tile art to PNGs. The graphical render modes only run on the
# notcurses backend, which won't start under a pipe or a detached terminal --
# so this is the only way to review the art without a human at a real
# terminal. Writes build/art-atlas.png (every tile, lit and dimmed) and
# build/art-scene.png (a laid-out room, so tiles can be judged against each
# other rather than in isolation).
art: | build bin
	$(CC) $(CFLAGS) tools/artdump.c src/tileart.c -o bin/artdump
	./bin/artdump build
	@for f in atlas scene scene-jungle scene-industrial scene-ruins scene-wastes scene-abyss scene-city; do \
		sips -s format png build/art-$$f.ppm --out build/art-$$f.png >/dev/null && \
		rm -f build/art-$$f.ppm && echo "build/art-$$f.png"; \
	done

# Headless winnability check: is each difficulty actually completable?
# Drives the real mapgen, monsters and combat -- see tools/simulate.c for
# what the simulated player will and won't do.
#   make sim                          -- 40 runs per mode, fair start
#   ./bin/simulate RUNS MODE GOLD PARTY
#     MODE  -1 = all, 0 Normal 1 Hard 2 Swarm 3 Hardcore
#     GOLD  starting purse (control: 2000000 buys out every shop)
#     PARTY 1 = hire the full five (control: the best case the game offers)
sim: | build bin
	$(CC) $(CFLAGS) tools/simulate.c $(GAME_OBJ) -o bin/simulate $(LDFLAGS)
	./bin/simulate 40

# Renders one screen at a given size and prints it, or walks every screen and
# reports the ones that overflow -- see tools/screenshot.c. Needs a pty, hence
# the wrapper.
#   make shot                        -- every screen at 80x24, the minimum
#   tools/screenshot.sh 30 100 armory      -- one screen, printed
#   tools/screenshot.sh 24 80 all          -- the sweep
# $(GAME_OBJ) is a real prerequisite, not an order-only one. It was order-only,
# so `make shot` linked whatever objects happened to be lying in build/ and
# reported on a binary that did not contain the change being tested -- which
# is worse than not running it, because it answers.
shot: $(GAME_OBJ) | build bin
	$(CC) $(CFLAGS) tools/screenshot.c $(GAME_OBJ) -o bin/screenshot $(LDFLAGS)
	@tools/screenshot.sh 24 80 all

# Per-tick position trace for the player and hires -- see tools/trace.c.
#   make trace                 -- 5 hires, 800 ticks, the Shaft -> trace.csv
#   ./bin/trace HIRES TICKS WORLD OUT.csv
trace: | build bin
	$(CC) $(CFLAGS) tools/trace.c $(GAME_OBJ) -o bin/trace $(LDFLAGS)
	./bin/trace 5 800 0 trace.csv

# Play in a web browser, over the local network -- see tools/serve.sh.
# Needs ttyd (brew install ttyd). serve-nc uses the notcurses build, which
# works here because a browser terminal is a real pty: it answers the
# capability queries that make notcurses refuse to start under a pipe.
serve: all
	@tools/serve.sh bin/aether-descent

serve-nc: notcurses
	@tools/serve.sh bin/aether-descent-nc

# Memory-safety build. Play it exactly like the normal binary; any
# out-of-bounds access or UB aborts with a report instead of going unnoticed.
asan:
	$(CC) -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SRC) -o bin/aether-asan $(LDFLAGS)

clean:
	rm -rf build build-nc bin
