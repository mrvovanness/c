CFLAGS = -Wall -Wextra -Wpedantic -std=c11
LDLIBS = -lcurl

all: solution

solution: main.c cJSON.c
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

clean:
	rm -f solution core

.PHONY: all clean
