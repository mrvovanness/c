CC      = clang
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -O2 -g -pthread

BIN     = log-stats
OBJS    = main.o parser.o stats.o

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(BIN)

main.o: main.c parser.h stats.h
	$(CC) $(CFLAGS) -c main.c -o main.o

parser.o: parser.c parser.h
	$(CC) $(CFLAGS) -c parser.c -o parser.o

stats.o: stats.c stats.h
	$(CC) $(CFLAGS) -c stats.c -o stats.o

clean:
	rm -f *.o $(BIN)
