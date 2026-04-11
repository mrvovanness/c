CC      = clang
CFLAGS  = -Wall -Wextra -Wpedantic -std=c11 -g
AR      = ar
ARFLAGS = rcs

LIB     = liblogger.a
TEST    = test_logger

.PHONY: all clean

all: $(LIB) $(TEST)

# --- static library ---
logger.o: logger.c logger.h
	$(CC) $(CFLAGS) -c logger.c -o logger.o

$(LIB): logger.o
	$(AR) $(ARFLAGS) $(LIB) logger.o

# --- test application ---
main.o: main.c logger.h
	$(CC) $(CFLAGS) -c main.c -o main.o

$(TEST): main.o $(LIB)
	$(CC) $(CFLAGS) main.o -L. -llogger -lpthread -o $(TEST)

clean:
	rm -f *.o $(LIB) $(TEST) test.log
