/* logger.c - LOGGING PROCESS
 * Receives log messages from Core through a POSIX message queue and
 * writes them to sim.log. Errors are also echoed to stderr.
 * start this first
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <mqueue.h>
#include "protocol.h"

int main(void) {
    struct mq_attr attr = { .mq_flags = 0, .mq_maxmsg = 10,
                            .mq_msgsize = sizeof(log_msg_t), .mq_curmsgs = 0 };
    mqd_t mq = mq_open(MQ_NAME, O_CREAT | O_RDONLY, 0666, &attr);
    if (mq == (mqd_t)-1) { perror("mq_open"); return 1; }

    struct mq_attr got;
    mq_getattr(mq, &got);
    char *buf = malloc((size_t)got.mq_msgsize);

    FILE *f = fopen("sim.log", "a");
    if (!f) { perror("fopen sim.log"); return 1; }
    printf("[logger] waiting for messages, writing to sim.log\n");

    long count = 0;
    for (;;) {
        ssize_t n = mq_receive(mq, buf, (size_t)got.mq_msgsize, NULL);
        if (n < 0) { perror("mq_receive"); break; }
        log_msg_t *m = (log_msg_t *)buf;
        if (m->level == LOG_QUIT) break;

        time_t sec = (time_t)(m->ts_ns / 1000000000LL);
        int ms = (int)((m->ts_ns / 1000000LL) % 1000);
        struct tm tmv;
        localtime_r(&sec, &tmv);
        char ts[32];
        strftime(ts, sizeof ts, "%H:%M:%S", &tmv);

        const char *lvl = (m->level == LOG_ERROR) ? "ERROR" : "INFO ";
        fprintf(f, "[%s.%03d] [%s] %s\n", ts, ms, lvl, m->text);
        if (m->level == LOG_ERROR) {
            fprintf(stderr, "[%s.%03d] [ERROR] %s\n", ts, ms, m->text);
            fflush(f);
        }
        count++;
    }
    fprintf(f, "-- logger exiting after %ld messages --\n", count);
    fclose(f);
    mq_close(mq);
    printf("[logger] bye (%ld messages)\n", count);
    return 0;
}
