CFLAGS = -Wall -Wextra -Wpedantic -std=c11

ls-daemon: main.c
	$(CC) $(CFLAGS) -o $@ $<

test: ls-daemon
	@sh test.sh

clean:
	rm -f ls-daemon

.PHONY: test clean
