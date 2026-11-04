/* bench.c - standalone vs multi-process benchmark in ONE file.
 *   ./bench standalone [runs]   single-process baseline (no IPC)
 *   ./bench ipc [runs]          multi-process (core + logger must be running)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include "protocol.h"
#include "cpu.h"

static double now_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static int rpc(int s, int cmd, reply_msg_t *r) {
    cmd_msg_t c = { cmd, 0 };
    if (write_full(s, &c, sizeof c) < 0) return -1;
    return read_full(s, r, sizeof *r);
}

static void bench_standalone(int runs) {
    shm_state_t c;
    long steps = 0;
    double t0 = now_s();
    for (int i = 0; i < runs; i++) {
        cpu_reset(&c);
        int e;
        do { e = cpu_step(&c); steps++; } while (e == E_OK);
    }
    double t1 = now_s();
    printf("Standalone:    %ld steps in %.6f s -> %.4f us/step, %.0f steps/s\n",
           steps, t1 - t0, (t1 - t0) / steps * 1e6, steps / (t1 - t0));
}

static int bench_ipc(int runs) {
    int pings = 20000;
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un addr = { .sun_family = AF_UNIX };
    strncpy(addr.sun_path, SOCK_PATH, sizeof addr.sun_path - 1);
    if (connect(s, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("connect (are core and logger running?)");
        return 1;
    }
    reply_msg_t r;

    /* 1. pure socket round-trip latency */
    double t0 = now_s();
    for (int i = 0; i < pings; i++) rpc(s, CMD_PING, &r);
    double t1 = now_s();
    printf("IPC round trip (socket PING): %.2f us avg over %d pings\n",
           (t1 - t0) / pings * 1e6, pings);

    /* 2. full simulator work through socket + shared memory + message queue */
    long steps = 0;
    t0 = now_s();
    for (int i = 0; i < runs; i++) {
        rpc(s, CMD_RESET, &r);
        do { rpc(s, CMD_STEP, &r); steps++; } while (r.status == 0);
    }
    t1 = now_s();
    printf("Multi-process: %ld steps in %.3f s -> %.2f us/step, %.0f steps/s\n",
           steps, t1 - t0, (t1 - t0) / steps * 1e6, steps / (t1 - t0));
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s standalone|ipc [runs]\n", argv[0]); return 1; }
    int runs = argc > 2 ? atoi(argv[2]) : 500;
    if (!strcmp(argv[1], "standalone")) { bench_standalone(runs); return 0; }
    if (!strcmp(argv[1], "ipc"))        return bench_ipc(runs);
    fprintf(stderr, "unknown mode '%s'\n", argv[1]);
    return 1;
}
