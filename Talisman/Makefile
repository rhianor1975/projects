# Talisman -- C99 + curses.  Builds on macOS, Linux and Windows.
#
#   make                 the game
#   make check           the invariant soak
#   make shot            rebuild, then walk every screen at 86x29
#
# Curses is the only dependency.  macOS ships ncurses; on Debian/Ubuntu it is
# libncurses-dev, on Fedora ncurses-devel; on Windows build under MSYS2 with
# mingw-w64-x86_64-ncurses, or point CURSES at PDCurses.
#
#   pacman -S mingw-w64-x86_64-ncurses    (MSYS2 MINGW64 shell)

CC      ?= cc
BIN      = talisman
SRC      = src/board.c src/items.c src/data.c src/ui.c src/ai.c src/game.c
OBJ      = $(SRC:.c=.o)

WARN     = -std=c99 -Wall -Wextra
OPT     ?= -O2

# -std=c99 is strict ISO, which hides POSIX declarations -- getpid() among
# them -- so ask for them explicitly rather than fall back to -std=gnu99.
POSIX    = -D_POSIX_C_SOURCE=200809L

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(OS),Windows_NT)
  # MSYS2/MinGW.  PDCurses works too: make CURSES=-lpdcurses
  CURSES ?= -lncurses
  PLATFORM = -D_WIN32_WINNT=0x0600
  EXE      = .exe
else ifeq ($(UNAME_S),Darwin)
  CURSES ?= -lncurses
  PLATFORM =
  EXE      =
else
  # Linux and the BSDs.  Some ncurses builds split the terminfo half out.
  CURSES ?= $(shell pkg-config --libs ncursesw 2>/dev/null || echo "-lncurses -ltinfo")
  PLATFORM = $(POSIX)
  EXE      =
endif

CFLAGS  ?= $(WARN) $(OPT) $(PLATFORM)
LDLIBS   = $(CURSES)

all: $(BIN)$(EXE)

$(BIN)$(EXE): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(OBJ): src/talisman.h

# Sanitisers find what the soak cannot: reads off the end of a table that
# happen to land on something harmless.
san: clean
	$(MAKE) OPT="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"

check: $(BIN)$(EXE)
	@i=1; while [ $$i -le 30 ]; do \
	  TERM=xterm LINES=40 COLUMNS=120 TALISMAN_AUTO=1 TALISMAN_SEED=$$i \
	  TALISMAN_CHECK=1 ./$(BIN)$(EXE) >/dev/null 2>&1 || exit 1; \
	  i=$$((i+1)); done; echo "30 games, no invariant broken"

# Every layout claim is otherwise arithmetic, and arithmetic is what put the
# character list's footer onto the prompt row at thirty rows and nowhere else.
shot: $(BIN)$(EXE)
	@tools/screenshot.sh 29 86 all

clean:
	rm -f $(OBJ) $(BIN) $(BIN).exe

.PHONY: all san check shot clean
