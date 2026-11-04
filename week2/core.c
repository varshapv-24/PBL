/* core.c - CORE PROCESS 
 *  - Socket server: receives commands from UI
 *  - CPU thread: runs instructions when in RUN mode
 *  - Shared memory writer (guarded by a semaphore): publishes state for UI
 *  - Message queue sender: sends log events to Logger
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <semaphore.h>
#include <mqueue.h>
#include "protocol.h"
#include "cpu.h"

static shm_state_t *shm;
static sem_t *sem;
static mqd_t mq;

static shm_state_t cpu;                       /* private working copy   */
static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;  /* guards cpu+running */
static int running = 0;
static volatile int quit_flag = 0;

static int64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void log_event(int level, const char *fmt, ...) {
    log_msg_t m;
    memset(&m, 0, sizeof m);
    m.level = level;
    m.ts_ns = now_ns();
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(m.text, sizeof m.text, fmt, ap);
    va_end(ap);
    if (mq_send(mq, (const char *)&m, sizeof m, 0) < 0) perror("mq_send");
}

/* copy private state into shared memory (caller holds lk) */
static void publish(void) {
    sem_wait(sem);
    uint64_t v = shm->version + 1;
    memcpy((void *)shm, &cpu, sizeof cpu);
    shm->version = v;
    sem_post(sem);
}

/* execute one instruction + publish + log (caller holds lk) */
static int do_step(void) {
    int e = cpu_step(&cpu);
    publish();
    if (e == E_OK)        log_event(LOG_INFO, "STEP pc=%d sp=%d q=%d", cpu.pc, cpu.sp, cpu.qcount);
    else if (e == E_HALT) log_event(LOG_INFO, "CPU halted at pc=%d", cpu.pc);
    else                  log_event(LOG_ERROR, "%s at pc=%d", ERR_NAME[e], cpu.pc);
    return e;
}

/* CPU thread: executes while 'running' is set */
static void *cpu_thread(void *arg) {
    (void)arg;
    while (!quit_flag) {
        int did = 0;
        pthread_mutex_lock(&lk);
        if (running) {
            if (do_step() != E_OK) running = 0;
            did = 1;
        }
        pthread_mutex_unlock(&lk);
        usleep(did ? 100000 : 10000);   /* slow enough for the UI to watch */
    }
    return NULL;
}

static void handle(const cmd_msg_t *c, reply_msg_t *r) {
    memset(r, 0, sizeof *r);
    strcpy(r->text, "ok");

    switch (c->cmd) {
    case CMD_STEP:
        pthread_mutex_lock(&lk);
        if (running) { r->status = -1; strcpy(r->text, "busy: CPU is running"); }
        else { int e = do_step(); r->status = e; snprintf(r->text, sizeof r->text, "%s", ERR_NAME[e]); }
        pthread_mutex_unlock(&lk);
        break;
    case CMD_RUN:
        pthread_mutex_lock(&lk);
        if (cpu.halted) { r->status = -2; strcpy(r->text, "halted: reset first"); }
        else { running = 1; log_event(LOG_INFO, "RUN started"); }
        pthread_mutex_unlock(&lk);
        break;
    case CMD_STOP:
        pthread_mutex_lock(&lk);
        running = 0;
        log_event(LOG_INFO, "RUN stopped");
        pthread_mutex_unlock(&lk);
        break;
    case CMD_RESET:
        pthread_mutex_lock(&lk);
        running = 0;
        cpu_reset(&cpu);
        publish();
        log_event(LOG_INFO, "CPU reset");
        pthread_mutex_unlock(&lk);
        break;
    case CMD_PING:
        break;
    case CMD_QUIT:
        quit_flag = 1;
        strcpy(r->text, "core shutting down");
        break;
    default:
        r->status = -3;
        strcpy(r->text, "unknown command");
        log_event(LOG_ERROR, "unknown command %d", c->cmd);
    }
}

int main(void) {
    /* --- message queue (to logger) --- */
    struct mq_attr attr = { .mq_flags = 0, .mq_maxmsg = 10,
                            .mq_msgsize = sizeof(log_msg_t), .mq_curmsgs = 0 };
    mq = mq_open(MQ_NAME, O_CREAT | O_WRONLY, 0666, &attr);
    if (mq == (mqd_t)-1) { perror("mq_open"); return 1; }

    /* --- shared memory + semaphore --- */
    int fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (fd < 0) { perror("shm_open"); return 1; }
    if (ftruncate(fd, sizeof(shm_state_t)) < 0) { perror("ftruncate"); return 1; }
    shm = mmap(NULL, sizeof(shm_state_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);
    sem = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (sem == SEM_FAILED) { perror("sem_open"); return 1; }

    cpu_reset(&cpu);
    publish();

    /* --- socket server --- */
    int srv = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCK_PATH, sizeof addr.sun_path - 1);
    unlink(SOCK_PATH);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    listen(srv, 4);

    pthread_t t;
    pthread_create(&t, NULL, cpu_thread, NULL);
    log_event(LOG_INFO, "Core started");
    printf("[core] ready on %s\n", SOCK_PATH);

    while (!quit_flag) {
        int cl = accept(srv, NULL, NULL);
        if (cl < 0) continue;
        cmd_msg_t c;
        reply_msg_t r;
        while (!quit_flag && read_full(cl, &c, sizeof c) == 0) {
            handle(&c, &r);
            if (write_full(cl, &r, sizeof r) < 0) break;
        }
        close(cl);
    }

    log_event(LOG_INFO, "Core shutting down");
    log_event(LOG_QUIT, "quit");          /* tells logger to exit */
    pthread_join(t, NULL);

    close(srv);
    unlink(SOCK_PATH);
    mq_close(mq);
    mq_unlink(MQ_NAME);
    munmap(shm, sizeof(shm_state_t));
    shm_unlink(SHM_NAME);
    sem_close(sem);
    sem_unlink(SEM_NAME);
    printf("[core] bye\n");
    return 0;
}
