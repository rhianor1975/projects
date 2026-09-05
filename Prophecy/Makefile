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
clean:
	rm -f $(OBJ) $(BIN)
.PHONY: all clean
