/* cpu.h - tiny CPU / memory / stack / queue model 
 * Used by core.c (multi-process) AND bench.c (single-process baseline),
 * so the benchmark compares identical simulator logic.
 * REPLACE this with your real simulator logic.
 */
#ifndef CPU_H
#define CPU_H

#include <string.h>
#include "protocol.h"

enum { OP_HALT, OP_LOADI, OP_ADD, OP_DEC, OP_STORE, OP_PUSH, OP_POP,
       OP_ENQ, OP_DEQ, OP_JNZ };

enum { E_OK = 0, E_HALT, E_STACK_OVF, E_STACK_UND, E_Q_OVF, E_Q_EMPTY, E_BAD_PC };

static const char *ERR_NAME[] = {
    "ok", "halted", "stack overflow", "stack underflow",
    "queue overflow", "queue empty", "bad pc"
};

/* {opcode, a, b} */
static const int PROGRAM[][3] = {
    {OP_LOADI, 0, 5},    /* 0: r0 = 5          */
    {OP_LOADI, 1, 0},    /* 1: r1 = 0          */
    {OP_ADD,   1, 0},    /* 2: r1 += r0        */
    {OP_PUSH,  1, 0},    /* 3: push r1         */
    {OP_ENQ,   1, 0},    /* 4: enqueue r1      */
    {OP_DEC,   0, 0},    /* 5: r0--            */
    {OP_JNZ,   0, 2},    /* 6: if r0 != 0 goto 2 */
    {OP_POP,   2, 0},    /* 7: r2 = pop        */
    {OP_DEQ,   3, 0},    /* 8: r3 = dequeue    */
    {OP_STORE, 1, 0},    /* 9: mem[0] = r1     */
    {OP_HALT,  0, 0},    /* 10                 */
};
#define PROG_LEN ((int)(sizeof PROGRAM / sizeof PROGRAM[0]))

static inline void cpu_reset(shm_state_t *c) { memset(c, 0, sizeof *c); }

/* Execute one instruction. Returns E_OK or an error / halt code. */
static inline int cpu_step(shm_state_t *c) {
    if (c->halted) return E_HALT;
    if (c->pc < 0 || c->pc >= PROG_LEN) { c->halted = 1; return E_BAD_PC; }

    const int *i = PROGRAM[c->pc];
    int op = i[0], a = i[1], b = i[2];
    c->pc++;

    switch (op) {
    case OP_HALT:  c->halted = 1; return E_HALT;
    case OP_LOADI: c->reg[a] = b; break;
    case OP_ADD:   c->reg[a] += c->reg[b]; break;
    case OP_DEC:   c->reg[a]--; break;
    case OP_STORE: c->mem[b] = c->reg[a]; break;
    case OP_PUSH:
        if (c->sp >= STACK_SIZE) { c->halted = 1; return E_STACK_OVF; }
        c->stack[c->sp++] = c->reg[a]; break;
    case OP_POP:
        if (c->sp <= 0) { c->halted = 1; return E_STACK_UND; }
        c->reg[a] = c->stack[--c->sp]; break;
    case OP_ENQ:
        if (c->qcount >= QUEUE_SIZE) { c->halted = 1; return E_Q_OVF; }
        c->queue[c->qcount++] = c->reg[a]; break;
    case OP_DEQ:
        if (c->qcount <= 0) { c->halted = 1; return E_Q_EMPTY; }
        c->reg[a] = c->queue[0];
        memmove(c->queue, c->queue + 1, sizeof(int32_t) * (size_t)(--c->qcount));
        break;
    case OP_JNZ:   if (c->reg[a] != 0) c->pc = b; break;
    }
    return E_OK;
}

#endif
