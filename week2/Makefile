CC     = gcc
CFLAGS = -Wall -Wextra -O2 -pthread
LIBS   = -lrt -pthread

all: core ui logger bench

core: core.c cpu.h protocol.h
	$(CC) $(CFLAGS) -o $@ core.c $(LIBS)
ui: ui.c protocol.h
	$(CC) $(CFLAGS) -o $@ ui.c $(LIBS)
logger: logger.c protocol.h
	$(CC) $(CFLAGS) -o $@ logger.c $(LIBS)
bench: bench.c cpu.h protocol.h
	$(CC) $(CFLAGS) -o $@ bench.c $(LIBS)

clean:
	rm -f core ui logger bench sim.log
	rm -f /dev/shm/sim_state_shm /dev/shm/sem.sim_state_sem /tmp/sim_core.sock
