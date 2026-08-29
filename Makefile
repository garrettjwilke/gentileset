CC ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra

.PHONY: all clean

all: gentileset

gentileset: gentileset.c lodepng.c lodepng.h
	$(CC) $(CFLAGS) -o $@ gentileset.c lodepng.c

clean:
	rm -f gentileset
