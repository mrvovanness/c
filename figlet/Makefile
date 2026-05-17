CFLAGS = -Wall -Wextra -Wpedantic -std=c11

all: solution

solution: main.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f solution

.PHONY: all clean
