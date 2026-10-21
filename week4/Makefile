CC=gcc
CFLAGS=-Wall -Wextra -g -std=c11

all: server test_main test_edge

server: server.c deadlock.c deadlock.h
	$(CC) $(CFLAGS) -o server server.c deadlock.c

test_main: test_main.c deadlock.c deadlock.h
	$(CC) $(CFLAGS) -o test_main test_main.c deadlock.c

test_edge: test_edge.c deadlock.c deadlock.h
	$(CC) $(CFLAGS) -o test_edge test_edge.c deadlock.c

test: test_main test_edge
	./test_main
	./test_edge

clean:
	rm -f server test_main test_edge

.PHONY: all test clean
