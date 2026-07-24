CC = gcc
CFLAGS = -O2 -Wall -Wextra -pthread
LDLIBS = -lm
TARGET = fidch
SRC = fidhcheall.c

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDLIBS)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/

.PHONY: clean install
