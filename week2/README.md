# Multi-Process CPU Simulator

A multi-process CPU simulator developed using C and POSIX Message Queues.

## Processes

- UI Process - accepts commands from the user
- Core Process - executes CPU operations
- Logger Process - records execution logs

## IPC Communication

The processes communicate using POSIX Message Queues.

UI → Core → Logger

## Supported Operations

- LOAD
- ADD
- SUB
- MUL
- DIV
- STORE
- READ
- WRITE
- PUSH
- POP
- PEEK
- DISPLAY_STACK
- ENQUEUE
- DEQUEUE
- DISPLAY_QUEUE
- REGISTERS
- EXIT

## Compilation

```bash
gcc -Wall -Wextra -o logger logger.c -lrt
gcc -Wall -Wextra -o core core.c -lrt
gcc -Wall -Wextra -o ui ui.c -lrt

