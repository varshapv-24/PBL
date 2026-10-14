# Makefile -- CPU Scheduling Simulator
#
# Usage:
#   make          build the program (creates ./sim)
#   make run      build and run the program
#   make clean    delete the compiled files
#
# All .c files and scheduler.h must be in the same folder as this Makefile.

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -g
TARGET  = sim

SRCS = main.c metrics.c gantt.c round_robin.c \
       fcfs_sjf_priority.c srtf_priority.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

# every .c file is recompiled if scheduler.h changes
%.o: %.c scheduler.h
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all run clean
