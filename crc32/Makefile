CC     = gcc
CFLAGS = -Wall -Wextra -Wpedantic -std=c11 -g -O0

UNAME  := $(shell uname -s)
TARGET := crc32-$(UNAME)

.PHONY: all clean

all: $(TARGET)

$(TARGET): crc32.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf crc32-*
