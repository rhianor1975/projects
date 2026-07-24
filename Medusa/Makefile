CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c11
LDLIBS = -lcurl

SRCS = main.c ui.c api.c json.c config.c mpv.c
OBJS = $(SRCS:.c=.o)
TARGET = medusa

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS) $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) medusa-debug.log medusa-debug-item.json

.PHONY: clean
