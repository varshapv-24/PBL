CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lrt

all: logger core ui

logger: logger.c
	$(CC) $(CFLAGS) -o logger logger.c $(LDFLAGS)

core: core.c
	$(CC) $(CFLAGS) -o core core.c $(LDFLAGS)

ui: ui.c
	$(CC) $(CFLAGS) -o ui ui.c $(LDFLAGS)

clean:
	rm -f logger core ui cpu_log.txt error_log.txt
