# OWNER: Member 2 - run all commands from this folder
CC     = gcc
CFLAGS = -Wall -Wextra -g -Imember1_logic
L      = member1_logic

all: server

test: $(L)/test_main.c $(L)/deadlock.c $(L)/deadlock.h
	$(CC) $(CFLAGS) -o test_console $(L)/test_main.c $(L)/deadlock.c
	./test_console

test-edge: $(L)/test_edge.c $(L)/deadlock.c $(L)/deadlock.h
	$(CC) $(CFLAGS) -o test_edge $(L)/test_edge.c $(L)/deadlock.c
	./test_edge

tests: test test-edge

server: member2_backend/server.c $(L)/deadlock.c $(L)/deadlock.h
	$(CC) $(CFLAGS) -o server member2_backend/server.c $(L)/deadlock.c

run: server
	./server 8080

clean:
	rm -f test_console test_edge server
