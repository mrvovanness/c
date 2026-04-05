CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11
TARGET  = ls-daemon
SRCS    = main.c config.c daemon.c server.c
OBJS    = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TARGET)
	@sh test.sh

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean test
