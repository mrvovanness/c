CC      = clang
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -O2 -g -pthread

BIN     = log-stats
OBJS    = main.o parser.o stats.o

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(BIN)

main.o: main.c parser.h stats.h util.h
	$(CC) $(CFLAGS) -c main.c -o main.o

parser.o: parser.c parser.h
	$(CC) $(CFLAGS) -c parser.c -o parser.o

stats.o: stats.c stats.h util.h
	$(CC) $(CFLAGS) -c stats.c -o stats.o

test: $(BIN) tests/test_parser tests/test_stats
	./tests/test_parser
	./tests/test_stats

tests/test_parser: tests/parser_test.c tests/check.h parser.o
	$(CC) $(CFLAGS) tests/parser_test.c parser.o -o tests/test_parser

tests/test_stats: tests/stats_test.c tests/check.h stats.o
	$(CC) $(CFLAGS) tests/stats_test.c stats.o -o tests/test_stats

clean:
	rm -f *.o $(BIN)
	rm -f tests/test_parser tests/test_stats
