AS = nasm  # NOTE: yasm тоже подойдёт

all: asm-prog

asm-prog: main.o
	$(CC) -g -no-pie $^ -o $@

main.o: main.asm
	$(AS) -g -felf64 $^

clean:
	rm -f asm-prog *.o core

solution: main.c
	$(CC) $^ -o $@ -Wall -Wextra -Wpedantic -std=c11

.PHONY: clean
