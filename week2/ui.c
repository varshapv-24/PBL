/* ui.c - UI PROCESS 
 *  - Main thread: reads user commands, sends them over the UNIX socket
 *  - Monitor thread: watches shared memory and prints state when it changes
 * Commands: step | run | stop | reset | ping | show | quit | exit
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <semaphore.h>
#include "protocol.h"

static shm_state_t *shm;
static sem_t *sem;
static volatile int done = 0;

static void snapshot(shm_state_t *out) {
    sem_wait(sem);
    memcpy(out, (const void *)shm, sizeof *out);
    sem_post(sem);
}

static void print_state(const shm_state_t *s) {
    printf("v%llu | pc=%d halted=%d | R0..R3=%d %d %d %d | stack(%d):",
           (unsigned long long)s->version, s->pc, s->halted,
           s->reg[0], s->reg[1], s->reg[2], s->reg[3], s->sp);
    for (int i = 0; i < s->sp; i++) printf(" %d", s->stack[i]);
    printf(" | queue(%d):", s->qcount);
    for (int i = 0; i < s->qcount; i++) printf(" %d", s->queue[i]);
    printf(" | mem[0]=%d\n", s->mem[0]);
}

static void *monitor(void *arg) {
    (void)arg;
    uint64_t last = (uint64_t)-1;
    shm_state_t s;
    while (!done) {
        snapshot(&s);
        if (s.version != last) {
            last = s.version;
            printf("\n[state] ");
            print_state(&s);
            printf("> ");
            fflush(stdout);
        }
        usleep(200000);
    }
    return NULL;
}

int main(void) {
    int fd = shm_open(SHM_NAME, O_RDONLY, 0);
    if (fd < 0) { perror("shm_open (is core running?)"); return 1; }
    shm = mmap(NULL, sizeof(shm_state_t), PROT_READ, MAP_SHARED, fd, 0);
    if (shm == MAP_FAILED) { perror("mmap"); return 1; }
    close(fd);
    sem = sem_open(SEM_NAME, 0);
    if (sem == SEM_FAILED) { perror("sem_open"); return 1; }

    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCK_PATH, sizeof addr.sun_path - 1);
    if (connect(s, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect (is core running?)");
        return 1;
    }

    pthread_t t;
    pthread_create(&t, NULL, monitor, NULL);

    printf("Commands: step run stop reset ping show quit(core) exit(ui only)\n");
    char line[64];
    for (;;) {
        printf("> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        line[strcspn(line, "\n")] = 0;

        cmd_msg_t c = { 0, 0 };
        if      (!strcmp(line, "step"))  c.cmd = CMD_STEP;
        else if (!strcmp(line, "run"))   c.cmd = CMD_RUN;
        else if (!strcmp(line, "stop"))  c.cmd = CMD_STOP;
        else if (!strcmp(line, "reset")) c.cmd = CMD_RESET;
        else if (!strcmp(line, "ping"))  c.cmd = CMD_PING;
        else if (!strcmp(line, "quit"))  c.cmd = CMD_QUIT;
        else if (!strcmp(line, "exit"))  break;
        else if (!strcmp(line, "show")) {
            shm_state_t st; snapshot(&st); print_state(&st); continue;
        }
        else if (line[0] == 0) continue;
        else { printf("unknown command\n"); continue; }

        reply_msg_t r;
        if (write_full(s, &c, sizeof c) < 0 || read_full(s, &r, sizeof r) < 0) {
            printf("lost connection to core\n");
            break;
        }
        printf("reply: status=%d (%s)\n", r.status, r.text);
        if (c.cmd == CMD_QUIT) break;
    }
    done = 1;
    pthread_join(t, NULL);
    close(s);
    return 0;
}
