/*
 * ui.c - UI (Manager) process of the multi-process simulator
 *
 * Reads commands from the user, sends them to Core and prints Core's reply.
 *
 *   UI --/pbl_manager_core--> Core
 *   UI <--/pbl_core_manager-- Core
 *
 * START ORDER:  Logger  ->  Core  ->  UI
 *   Core creates both queues, so start the UI only after Core is running.
 *   If Core is restarted, restart the UI too (it holds the old queues).
 *
 * Build:  gcc -Wall -Wextra -o ui ui.c -lrt
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>

#define MSG_SIZE 256
#define REPLY_TIMEOUT_SEC 5 /* must be longer than Core's 2 s Logger wait */
#define SEND_TIMEOUT_SEC 2

#define MANAGER_TO_CORE "/pbl_manager_core"
#define CORE_TO_MANAGER "/pbl_core_manager"

static void deadline_in(struct timespec *ts, int seconds)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += seconds;
}

/* Send with a timeout so the UI cannot hang if Core stopped reading. */
static int timed_send(mqd_t queue, const char *text)
{
    struct timespec ts;

    deadline_in(&ts, SEND_TIMEOUT_SEC);

    while (mq_timedsend(queue, text, strlen(text) + 1, 0, &ts) == -1)
    {
        if (errno == EINTR)
            continue;

        return -1;
    }

    return 0;
}

/* Wait for Core's reply; returns its length, or -1 (errno = ETIMEDOUT). */
static ssize_t wait_reply(mqd_t queue, char *buf, size_t size)
{
    struct timespec ts;
    ssize_t received;

    deadline_in(&ts, REPLY_TIMEOUT_SEC);

    do
    {
        received = mq_timedreceive(queue, buf, size, NULL, &ts);
    } while (received == -1 && errno == EINTR);

    return received;
}

/* Throw away replies that arrived late, so they are not mistaken for the
 * answer to the next command. Returns how many were discarded. */
static int drain_replies(mqd_t queue, char *buf, size_t size)
{
    struct timespec past = {0, 0}; /* already expired: returns immediately */
    int count = 0;

    while (mq_timedreceive(queue, buf, size, NULL, &past) != -1)
        count++;

    return count;
}

/* Trim spaces and convert to upper case (Core's commands are upper case). */
static void normalize(char *s)
{
    size_t start = 0;
    size_t len;
    size_t i;

    while (s[start] != '\0' && isspace((unsigned char)s[start]))
        start++;

    if (start > 0)
        memmove(s, s + start, strlen(s + start) + 1);

    len = strlen(s);

    while (len > 0 && isspace((unsigned char)s[len - 1]))
        s[--len] = '\0';

    for (i = 0; i < len; i++)
        s[i] = (char)toupper((unsigned char)s[i]);
}

static void print_help(void)
{
    printf("\nAvailable commands (case-insensitive):\n");
    printf("LOAD R1 10\n");
    printf("ADD R1 R2\n");
    printf("SUB R1 R2\n");
    printf("MUL R1 R2\n");
    printf("DIV R1 R2\n");
    printf("STORE R1 10\n");
    printf("READ 10\n");
    printf("WRITE 10 50\n");
    printf("PUSH 10\n");
    printf("POP\n");
    printf("PEEK\n");
    printf("DISPLAY_STACK\n");
    printf("ENQUEUE 10\n");
    printf("DEQUEUE\n");
    printf("DISPLAY_QUEUE\n");
    printf("REGISTERS\n");
    printf("EXIT   (stops Core and Logger, then quits)\n");
}

int main(void)
{
    mqd_t to_core;
    mqd_t from_core;
    struct mq_attr qa;
    char command[MSG_SIZE];
    char *response = NULL;
    size_t response_size;
    size_t send_limit;
    ssize_t received;
    int stale;
    int exit_code = EXIT_SUCCESS;

    to_core = mq_open(MANAGER_TO_CORE, O_WRONLY);

    if (to_core == (mqd_t)-1)
    {
        perror("mq_open manager->core");
        fprintf(stderr, "Start the Logger and then Core before the UI.\n");
        return EXIT_FAILURE;
    }

    from_core = mq_open(CORE_TO_MANAGER, O_RDONLY);

    if (from_core == (mqd_t)-1)
    {
        perror("mq_open core->manager");
        mq_close(to_core);
        return EXIT_FAILURE;
    }

    /* Size buffers from the real queue attributes: mq_receive() fails
     * with EMSGSIZE if the buffer is smaller than mq_msgsize. */
    if (mq_getattr(to_core, &qa) == -1)
    {
        perror("mq_getattr manager->core");
        exit_code = EXIT_FAILURE;
        goto done;
    }
    send_limit = (size_t)qa.mq_msgsize;

    if (mq_getattr(from_core, &qa) == -1)
    {
        perror("mq_getattr core->manager");
        exit_code = EXIT_FAILURE;
        goto done;
    }
    response_size = (size_t)qa.mq_msgsize;

    response = malloc(response_size + 1);

    if (response == NULL)
    {
        perror("malloc");
        exit_code = EXIT_FAILURE;
        goto done;
    }

    printf("====================================\n");
    printf("      MULTI-PROCESS SIMULATOR\n");
    printf("====================================\n");
    printf("Type HELP to see available commands.\n");

    while (1)
    {
        printf("\nUI> ");
        fflush(stdout);

        if (fgets(command, sizeof(command), stdin) == NULL)
            break;

        /* Line too long: discard the rest (so it is not read as a second
         * command) and reject it instead of sending a cut-off command. */
        if (strchr(command, '\n') == NULL && !feof(stdin))
        {
            int ch;

            while ((ch = getchar()) != '\n' && ch != EOF)
                ;

            printf("Input too long (maximum %d characters).\n", MSG_SIZE - 2);
            continue;
        }

        normalize(command);

        if (command[0] == '\0')
            continue;

        if (strcmp(command, "HELP") == 0)
        {
            print_help();
            continue;
        }

        if (strlen(command) + 1 > send_limit)
        {
            printf("Input too long (maximum %zu characters).\n",
                   send_limit - 1);
            continue;
        }

        stale = drain_replies(from_core, response, response_size);

        if (stale > 0)
            fprintf(stderr, "UI: discarded %d late reply(ies) from Core.\n",
                    stale);

        if (timed_send(to_core, command) == -1)
        {
            if (errno == ETIMEDOUT)
            {
                printf("Could not send the command: Core is not reading. "
                       "Is Core running?\n");
                continue;
            }

            perror("mq_send");
            exit_code = EXIT_FAILURE;
            break;
        }

        received = wait_reply(from_core, response, response_size);

        if (received == -1)
        {
            if (errno == ETIMEDOUT)
            {
                printf("No reply from Core within %d seconds. "
                       "Is Core running?\n", REPLY_TIMEOUT_SEC);
                continue;
            }

            perror("mq_receive");
            exit_code = EXIT_FAILURE;
            break;
        }

        response[received] = '\0';

        printf("Core: %s\n", response);

        /* Core confirms shutdown after it has stopped the Logger. */
        if (strcmp(response, "CORE TERMINATED") == 0)
            break;
    }

done:
    mq_close(to_core);
    mq_close(from_core);
    free(response);

    return exit_code;
}
