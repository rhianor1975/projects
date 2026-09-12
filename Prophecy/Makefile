CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -Wno-missing-field-initializers -O2
LDLIBS = -lncurses
SRC    = src/board.c src/items.c src/data.c src/ui.c src/game.c
OBJ    = $(SRC:.c=.o)
BIN    = prophecy

all: $(BIN)
$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)
$(OBJ): src/prophecy.h
# Sanitisers find what the soak cannot: reads off the end of a table that
# happen to land on something harmless.
san: clean
	$(MAKE) CFLAGS="-std=c99 -Wall -Wextra -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"

clean:
	rm -f $(OBJ) $(BIN)
.PHONY: all clean san
