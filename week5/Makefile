# Makefile
# Build with:  make        Run with:  ./pagesim        Clean with:  make clean
CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -O2 -D_POSIX_C_SOURCE=200809L
OBJS    = server.o validation.o fifo.o lru.o json.o optimal.o stats.o

pagesim: $(OBJS)
	$(CC) $(CFLAGS) -o pagesim $(OBJS)

%.o: %.c pageswap.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o pagesim
