/*
 * logger.c - Logging process of the multi-process simulator
 *
 * Receives log lines from Core, writes them to files and confirms each one.
 *
 *   Core --/pbl_core_logger--> Logger
 *   Core <--/pbl_logger_core-- Logger   (ACKs)
 *
 * Output files (in the current directory):
 *   cpu_log.txt    every message received (execution log)
 *   error_log.txt  only messages that start with "[ERROR]" (error log)
 *
 * START ORDER:  Logger  ->  Core  ->  UI
 *   The Logger creates both of its queues, so it must start first.
 *   It removes them again when it exits (EXIT message, Ctrl+C or SIGTERM).
 *
 * Build:  gcc -Wall -Wextra -o logger logger.c -lrt
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>

#define MSG_SIZE 256
#define QUEUE_MAX_MSG 10
#define ACK_TIMEOUT_SEC 1

#define CORE_TO_LOGGER "/pbl_core_logger"
#define LOGGER_TO_CORE "/pbl_logger_core"

#define LOG_FILE "cpu_log.txt"
#define ERROR_LOG_FILE "error_log.txt"

static volatile sig_atomic_t running = 1;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static void get_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm time_info;

    if (localtime_r(&now, &time_info) == NULL)
    {
        snprintf(buffer, size, "0000-00-00 00:00:00");
        return;
    }

    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &time_info);
}

/* Keep every message on one line, whatever the sender put in it. */
static void sanitize(char *s)
{
    for (; *s != '\0'; s++)
    {
        unsigned char ch = (unsigned char)*s;

        if (ch < 32 || ch >= 127)
            *s = '?';
    }
}

/* Returns 0 on success, -1 if the line could not be written to disk. */
static int write_line(FILE *file, const char *timestamp, const char *text)
{
    if (fprintf(file, "[%s] %s\n", timestamp, text) < 0 || fflush(file) != 0)
    {
        perror("Logger: write to log file");
        return -1;
    }

    return 0;
}

/* The ACK must not block forever if Core stopped reading its queue. */
static int send_ack(mqd_t queue, const char *text, long limit)
{
    struct timespec ts;
    size_t len = strlen(text);

    if (limit > 0 && len + 1 > (size_t)limit)
        len = (size_t)limit - 1;

    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ACK_TIMEOUT_SEC;

    while (mq_timedsend(queue, text, len + 1, 0, &ts) == -1)
    {
        if (errno == EINTR)
            continue;

        return -1;
    }

    return 0;
}

int main(void)
{
    struct mq_attr attr;
    struct mq_attr qa;
    struct sigaction sa;
    mqd_t incoming = (mqd_t)-1;
    mqd_t outgoing = (mqd_t)-1;
    FILE *log_file = NULL;
    FILE *error_file = NULL;
    char *message = NULL;
    size_t message_size = MSG_SIZE;
    long ack_limit = MSG_SIZE;
    char timestamp[32];
    char summary[MSG_SIZE];
    const char *stop_reason = "signal";
    unsigned long total = 0;
    unsigned long errors = 0;
    unsigned long ack_failures = 0;
    int exit_requested = 0;
    int exit_code = EXIT_FAILURE;
    ssize_t received;

    /* Ctrl+C / kill: leave the loop so the queues get removed.
     * No SA_RESTART, so a blocked mq_receive returns with EINTR. */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Open the files first, so Core is never started against a Logger
     * that cannot write its logs. */
    log_file = fopen(LOG_FILE, "a");

    if (log_file == NULL)
    {
        perror("fopen " LOG_FILE);
        goto done;
    }

    error_file = fopen(ERROR_LOG_FILE, "a");

    if (error_file == NULL)
    {
        perror("fopen " ERROR_LOG_FILE);
        goto done;
    }

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = QUEUE_MAX_MSG;
    attr.mq_msgsize = MSG_SIZE;

    /* Remove leftovers of a crashed run so attributes are always correct. */
    mq_unlink(CORE_TO_LOGGER);
    mq_unlink(LOGGER_TO_CORE);

    incoming = mq_open(CORE_TO_LOGGER, O_CREAT | O_RDONLY, 0600, &attr);

    if (incoming == (mqd_t)-1)
    {
        perror("mq_open core->logger");
        goto done;
    }

    outgoing = mq_open(LOGGER_TO_CORE, O_CREAT | O_WRONLY, 0600, &attr);

    if (outgoing == (mqd_t)-1)
    {
        perror("mq_open logger->core");
        goto done;
    }

    /* Size the receive buffer from the real queue attributes. */
    if (mq_getattr(incoming, &qa) == -1)
    {
        perror("mq_getattr core->logger");
        goto done;
    }
    message_size = (size_t)qa.mq_msgsize;

    if (mq_getattr(outgoing, &qa) == -1)
    {
        perror("mq_getattr logger->core");
        goto done;
    }
    ack_limit = qa.mq_msgsize;

    message = malloc(message_size + 1);

    if (message == NULL)
    {
        perror("malloc");
        goto done;
    }

    printf("Logger process started.\n");
    printf("Execution log: %s | Error log: %s\n", LOG_FILE, ERROR_LOG_FILE);
    fflush(stdout);

    while (running)
    {
        received = mq_receive(incoming, message, message_size, NULL);

        if (received == -1)
        {
            if (errno == EINTR)
                continue; /* the loop condition checks the signal flag */

            perror("mq_receive");
            stop_reason = "receive error";
            break;
        }

        message[received] = '\0';
        sanitize(message);

        if (strcmp(message, "EXIT") == 0)
        {
            exit_requested = 1;
            stop_reason = "EXIT received";
            break;
        }

        get_timestamp(timestamp, sizeof(timestamp));
        total++;

        {
            int ok = (write_line(log_file, timestamp, message) == 0);

            if (strncmp(message, "[ERROR]", 7) == 0)
            {
                errors++;

                if (write_line(error_file, timestamp, message) != 0)
                    ok = 0;
            }

            printf("[%s] %s\n", timestamp, message);
            fflush(stdout);

            /* The ACK is sent only after the line is on disk. A failed
             * ACK must not stop the Logger: the log line is already
             * written, so keep logging. */
            if (send_ack(outgoing, ok ? "LOG CONFIRMED" : "LOG FAILED",
                         ack_limit) == -1)
            {
                ack_failures++;
                fprintf(stderr, "Logger: ACK not delivered (%s)\n",
                        strerror(errno));
            }
        }
    }

    exit_code = EXIT_SUCCESS;

    /* Closing summary, written before Core is told we have stopped. */
    get_timestamp(timestamp, sizeof(timestamp));
    snprintf(summary, sizeof(summary),
             "[INFO] Logger stopped (%s): %lu message(s), %lu error(s), "
             "%lu ACK failure(s)",
             stop_reason, total, errors, ack_failures);
    write_line(log_file, timestamp, summary);
    printf("%s\n", summary);
    fflush(stdout);

    if (exit_requested)
    {
        if (send_ack(outgoing, "LOGGER TERMINATED", ack_limit) == -1)
            fprintf(stderr, "Logger: EXIT confirmation not delivered (%s)\n",
                    strerror(errno));
    }

done:
    if (log_file != NULL)
        fclose(log_file);
    if (error_file != NULL)
        fclose(error_file);
    if (incoming != (mqd_t)-1)
        mq_close(incoming);
    if (outgoing != (mqd_t)-1)
        mq_close(outgoing);

    /* The Logger owns these two queues; Core removes its own. */
    mq_unlink(CORE_TO_LOGGER);
    mq_unlink(LOGGER_TO_CORE);

    free(message);

    return exit_code;
}
