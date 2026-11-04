/* protocol.h - the INTERFACE CONTRACT shared by UI, Core and Logger.
 * Everyone includes this file. Agree on it before writing any other code.
 *
 *   UI  --(UNIX domain socket: commands/replies)-->  CORE
 *   UI  <--(shared memory + semaphore: state)------  CORE
 *   CORE --(POSIX message queue: log events)------>  LOGGER
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <unistd.h>

/* ---- IPC object names ---- */
#define SOCK_PATH "/tmp/sim_core.sock"
#define MQ_NAME   "/sim_log_mq"
#define SHM_NAME  "/sim_state_shm"
#define SEM_NAME  "/sim_state_sem"

/* ---- Simulator sizes ---- */
#define NUM_REGS   4
#define STACK_SIZE 16
#define QUEUE_SIZE 16
#define MEM_SIZE   64

/* ---- Socket: UI -> Core commands ---- */
enum { CMD_STEP = 1, CMD_RUN, CMD_STOP, CMD_RESET, CMD_PING, CMD_QUIT };

typedef struct { int32_t cmd; int32_t arg; } cmd_msg_t;

/* Core -> UI reply (status 0 = OK, >0 = simulator code, <0 = command error) */
typedef struct { int32_t status; char text[64]; } reply_msg_t;

/* ---- Message queue: Core -> Logger ---- */
enum { LOG_INFO = 0, LOG_ERROR = 1, LOG_QUIT = 2 };

typedef struct {
    int32_t level;
    int64_t ts_ns;       /* CLOCK_REALTIME nanoseconds */
    char    text[128];
} log_msg_t;

/* ---- Shared memory: Core writes, UI reads (guarded by SEM_NAME) ---- */
typedef struct {
    volatile uint64_t version;   /* incremented on every update */
    int32_t pc, sp, qcount;
    int32_t halted;
    int32_t reg[NUM_REGS];
    int32_t stack[STACK_SIZE];
    int32_t queue[QUEUE_SIZE];
    int32_t mem[MEM_SIZE];
} shm_state_t;

/* ---- helpers for stream sockets ---- */
static inline int read_full(int fd, void *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, (char *)buf + got, n - got);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}
static inline int write_full(int fd, const void *buf, size_t n) {
    size_t put = 0;
    while (put < n) {
        ssize_t w = write(fd, (const char *)buf + put, n - put);
        if (w <= 0) return -1;
        put += (size_t)w;
    }
    return 0;
}

#endif
