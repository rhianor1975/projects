CC = gcc
CFLAGS = -Wall -Wextra -lncurses -lpthread
TARGET = tunecurses
SRC = tunecurses.c

$(TARGET): $(SRC)
	$(CC) -o $(TARGET) $(SRC) $(CFLAGS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

.PHONY: clean install
