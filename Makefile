# SPDX-License-Identifier: MIT
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2

.PHONY: all clean

all: watch

watch: main.o watch.o
	$(CC) $(CFLAGS) -o $@ main.o watch.o

main.o: main.c watch.h
	$(CC) $(CFLAGS) -c main.c

watch.o: watch.c watch.h
	$(CC) $(CFLAGS) -c watch.c

clean:
	rm -f watch *.o
