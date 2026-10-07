/*
 * core.c - Core process of the multi-process simulator
 *
 * Executes CPU instructions (registers, memory, stack, queue) and talks to
 * the other processes over POSIX message queues:
 *
 *   Manager/UI --/pbl_manager_core--> Core --/pbl_core_logger--> Logger
 *   Manager/UI <--/pbl_core_manager-- Core <--/pbl_logger_core-- Logger
 *
 * START ORDER:  Logger  ->  Core  ->  Manager/UI
 *   - The Logger creates /pbl_core_logger and /pbl_logger_core
 *     (mq_maxmsg = 10, mq_msgsize = 256 recommended).
 *   - Core creates /pbl_manager_core and /pbl_core_manager, so the
 *     Manager/UI must open them only after Core has started.
 *
 * LOGGING MODES:
 *   default  : synchronous - Core waits for the Logger's ACK (with timeout).
 *   --async  : Core does not wait for ACKs (stale ACKs are drained instead).
 *              Use this to measure IPC overhead for the benchmark.
 *
 * Build:  gcc -Wall -Wextra -o core core.c -lrt
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>

#define MSG_SIZE 256
#define MEMORY_SIZE 100
#define STACK_SIZE 100
#define QUEUE_SIZE 100
#define QUEUE_MAX_MSG 10
#define IPC_TIMEOUT_SEC 2
#define MAX_TOKENS 4  /* command + up to 3 arguments */
#define TOKEN_SIZE 32

#define MANAGER_TO_CORE "/pbl_manager_core"
#define CORE_TO_MANAGER "/pbl_core_manager"
#define CORE_TO_LOGGER "/pbl_core_logger"
#define LOGGER_TO_CORE "/pbl_logger_core"

/* Everything the handlers need to talk to the other processes. */
typedef struct
{
    mqd_t manager_in;
    mqd_t manager_out;
    mqd_t logger_out;
    mqd_t logger_in;
    long log_msgsize;            /* max message size the Logger queue accepts */
    char *ack_buf;               /* receive buffer for Logger ACKs            */
    size_t ack_size;             /* mq_msgsize of the Logger->Core queue      */
    int async_logging;           /* 1 = do not wait for ACKs                  */
    unsigned long log_failures;  /* log messages that could not be delivered  */
} core_ctx;

static volatile sig_atomic_t running = 1;

static int R1 = 0;
static int R2 = 0;
static int R3 = 0;
static int R4 = 0;

static int memory[MEMORY_SIZE];

static int stack_data[STACK_SIZE];
static int top = -1;

static int queue_data[QUEUE_SIZE];
static int queue_count = 0;

static void on_signal(int sig)
{
    (void)sig;
    running = 0;
}

static int *get_register(const char *name)
{
    if (strcmp(name, "R1") == 0)
        return &R1;

    if (strcmp(name, "R2") == 0)
        return &R2;

    if (strcmp(name, "R3") == 0)
        return &R3;

    if (strcmp(name, "R4") == 0)
        return &R4;

    return NULL;
}

/* Replace non-printable characters so user input cannot forge log lines. */
static void sanitize(char *dst, size_t size, const char *src)
{
    size_t i;

    for (i = 0; i + 1 < size && src[i] != '\0'; i++)
    {
        unsigned char ch = (unsigned char)src[i];
        dst[i] = (ch >= 32 && ch < 127) ? (char)ch : '?';
    }

    dst[i] = '\0';
}

/* ------------------------------------------------------------------ */
/* IPC helpers (all blocking calls have a timeout)                     */
/* ------------------------------------------------------------------ */

static void deadline_in(struct timespec *ts, int seconds)
{
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec += seconds;
}

static int timed_send(mqd_t queue, const char *text, long limit)
{
    char buf[MSG_SIZE];
    struct timespec ts;
    size_t len;

    snprintf(buf, sizeof(buf), "%s", text);
    len = strlen(buf);

    /* Never exceed the queue's mq_msgsize (otherwise EMSGSIZE). */
    if (limit > 0 && len + 1 > (size_t)limit)
        len = (size_t)limit - 1;

    buf[len] = '\0';

    deadline_in(&ts, IPC_TIMEOUT_SEC);

    while (mq_timedsend(queue, buf, len + 1, 0, &ts) == -1)
    {
        if (errno == EINTR)
            continue;

        return -1;
    }

    return 0;
}

/* Discard ACKs that are already waiting (late or unwanted ones). */
static void drain_acks(core_ctx *c)
{
    struct timespec past = {0, 0}; /* already expired: returns immediately */

    while (mq_timedreceive(c->logger_in, c->ack_buf, c->ack_size,
                           NULL, &past) != -1)
    {
        /* keep draining */
    }
}

static int wait_ack(core_ctx *c)
{
    struct timespec ts;

    deadline_in(&ts, IPC_TIMEOUT_SEC);

    while (mq_timedreceive(c->logger_in, c->ack_buf, c->ack_size,
                           NULL, &ts) == -1)
    {
        if (errno == EINTR)
            continue;

        return -1;
    }

    return 0;
}

static int send_log(core_ctx *c, const char *level, const char *text)
{
    char line[MSG_SIZE];

    snprintf(line, sizeof(line), "[%s] %s", level, text);

    drain_acks(c);

    if (timed_send(c->logger_out, line, c->log_msgsize) == -1)
    {
        c->log_failures++;
        fprintf(stderr, "Core: log not delivered (%s)\n", strerror(errno));
        return -1;
    }

    if (c->async_logging)
        return 0;

    if (wait_ack(c) == -1)
    {
        c->log_failures++;
        fprintf(stderr, "Core: no ACK from Logger (%s)\n", strerror(errno));
        return -1;
    }

    return 0;
}

static void send_response(core_ctx *c, const char *text)
{
    if (timed_send(c->manager_out, text, MSG_SIZE) == -1)
        fprintf(stderr, "Core: response not delivered (%s)\n",
                strerror(errno));
}

/* Reply to the UI first, then log, so the UI never waits for the Logger. */
static void finish(core_ctx *c, int is_error,
                   const char *log_message, const char *response)
{
    send_response(c, response);
    send_log(c, is_error ? "ERROR" : "INFO", log_message);
}

/* Syntax errors, unknown and empty commands are errors too: log them. */
static void reject(core_ctx *c, const char *clean_command, const char *error)
{
    char response[MSG_SIZE];
    char log_message[MSG_SIZE];

    snprintf(response, sizeof(response), "ERROR: %s", error);
    snprintf(log_message, sizeof(log_message),
             "%s -> ERROR: %s", clean_command, error);

    finish(c, 1, log_message, response);
}

/* ------------------------------------------------------------------ */
/* Command parsing (strict: exact argument count, range-checked ints)  */
/* ------------------------------------------------------------------ */

/* Split on whitespace. Returns the token count, MAX_TOKENS + 1 if there
 * are more tokens than fit, or -1 if a token is too long. */
static int split_tokens(const char *s, char tok[][TOKEN_SIZE])
{
    int count = 0;

    while (*s != '\0')
    {
        size_t len = 0;

        while (*s != '\0' && isspace((unsigned char)*s))
            s++;

        if (*s == '\0')
            break;

        if (count == MAX_TOKENS)
            return MAX_TOKENS + 1;

        while (*s != '\0' && !isspace((unsigned char)*s))
        {
            if (len + 1 >= TOKEN_SIZE)
                return -1;

            tok[count][len++] = *s++;
        }

        tok[count][len] = '\0';
        count++;
    }

    return count;
}

/* 0 = ok, 1 = not a number (or trailing junk), 2 = does not fit in int.
 * (sscanf("%d") overflow is undefined behaviour, so strtol is used.) */
static int parse_int(const char *text, int *out)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(text, &end, 10);

    if (end == text || *end != '\0')
        return 1;

    if (errno == ERANGE || v < INT_MIN || v > INT_MAX)
        return 2;

    *out = (int)v;
    return 0;
}

static void reject_syntax(core_ctx *c, const char *clean_command,
                          const char *op)
{
    char text[64];

    snprintf(text, sizeof(text), "Invalid %s syntax", op);
    reject(c, clean_command, text);
}

/* Checks the token count and parses one or two integer arguments.
 * On failure it sends the error reply itself and returns 0. */
static int parse_args(core_ctx *c, const char *clean_command, const char *op,
                      int ntok, int expected, char tok[][TOKEN_SIZE],
                      int idx1, int *v1, int idx2, int *v2)
{
    int status;

    if (ntok != expected)
    {
        reject_syntax(c, clean_command, op);
        return 0;
    }

    status = parse_int(tok[idx1], v1);

    if (status == 0 && idx2 >= 0)
        status = parse_int(tok[idx2], v2);

    if (status == 1)
    {
        reject_syntax(c, clean_command, op);
        return 0;
    }

    if (status == 2)
    {
        reject(c, clean_command, "Number out of range");
        return 0;
    }

    return 1;
}

/* ------------------------------------------------------------------ */
/* Instruction handlers                                                */
/* ------------------------------------------------------------------ */

static void handle_load(core_ctx *c, const char *reg_name, int value)
{
    int *reg = get_register(reg_name);
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];

    if (reg == NULL)
    {
        snprintf(response, sizeof(response), "ERROR: Invalid register");
        snprintf(log_message, sizeof(log_message),
                 "LOAD %s %d -> ERROR: Invalid register", reg_name, value);
        finish(c, 1, log_message, response);
        return;
    }

    *reg = value;

    snprintf(response, sizeof(response), "%s = %d", reg_name, *reg);
    snprintf(log_message, sizeof(log_message),
             "LOAD %s %d -> %s = %d", reg_name, value, reg_name, *reg);

    finish(c, 0, log_message, response);
}

/* ADD / SUB / MUL / DIV share one handler. Arithmetic is done in
 * long long so overflow (and INT_MIN / -1) is detected, not undefined. */
static void handle_arith(core_ctx *c, const char *mnemonic, char op,
                         const char *dest_name, const char *src_name)
{
    int *dest = get_register(dest_name);
    int *src = get_register(src_name);
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];
    const char *error = NULL;
    long long result = 0;

    if (dest == NULL || src == NULL)
    {
        error = "Invalid register";
    }
    else
    {
        switch (op)
        {
        case '+':
            result = (long long)*dest + *src;
            break;
        case '-':
            result = (long long)*dest - *src;
            break;
        case '*':
            result = (long long)*dest * *src;
            break;
        case '/':
            if (*src == 0)
                error = "Division by zero";
            else
                result = (long long)*dest / *src;
            break;
        default:
            error = "Unknown operation";
            break;
        }

        if (error == NULL && (result > INT_MAX || result < INT_MIN))
            error = "Integer overflow";
    }

    if (error != NULL)
    {
        snprintf(response, sizeof(response), "ERROR: %s", error);
        snprintf(log_message, sizeof(log_message),
                 "%s %s %s -> ERROR: %s",
                 mnemonic, dest_name, src_name, error);
        finish(c, 1, log_message, response);
        return;
    }

    *dest = (int)result;

    snprintf(response, sizeof(response), "%s = %d", dest_name, *dest);
    snprintf(log_message, sizeof(log_message),
             "%s %s %s -> %s = %d",
             mnemonic, dest_name, src_name, dest_name, *dest);

    finish(c, 0, log_message, response);
}

static void handle_store(core_ctx *c, const char *reg_name, int address)
{
    int *reg = get_register(reg_name);
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];
    const char *error = NULL;

    if (reg == NULL)
        error = "Invalid register";
    else if (address < 0 || address >= MEMORY_SIZE)
        error = "Invalid memory address";

    if (error != NULL)
    {
        snprintf(response, sizeof(response), "ERROR: %s", error);
        snprintf(log_message, sizeof(log_message),
                 "STORE %s %d -> ERROR: %s", reg_name, address, error);
        finish(c, 1, log_message, response);
        return;
    }

    memory[address] = *reg;

    snprintf(response, sizeof(response),
             "Memory[%d] = %d", address, memory[address]);
    snprintf(log_message, sizeof(log_message),
             "STORE %s %d -> Memory[%d] = %d",
             reg_name, address, address, memory[address]);

    finish(c, 0, log_message, response);
}

static void handle_read(core_ctx *c, int address)
{
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];

    if (address < 0 || address >= MEMORY_SIZE)
    {
        snprintf(response, sizeof(response), "ERROR: Invalid memory address");
        snprintf(log_message, sizeof(log_message),
                 "READ %d -> ERROR: Invalid memory address", address);
        finish(c, 1, log_message, response);
        return;
    }

    snprintf(response, sizeof(response),
             "Memory[%d] = %d", address, memory[address]);
    snprintf(log_message, sizeof(log_message),
             "READ %d -> Memory[%d] = %d", address, address, memory[address]);

    finish(c, 0, log_message, response);
}

static void handle_write(core_ctx *c, int address, int value)
{
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];

    if (address < 0 || address >= MEMORY_SIZE)
    {
        snprintf(response, sizeof(response), "ERROR: Invalid memory address");
        snprintf(log_message, sizeof(log_message),
                 "WRITE %d %d -> ERROR: Invalid memory address",
                 address, value);
        finish(c, 1, log_message, response);
        return;
    }

    memory[address] = value;

    snprintf(response, sizeof(response), "Memory[%d] = %d", address, value);
    snprintf(log_message, sizeof(log_message),
             "WRITE %d %d -> Memory[%d] = %d", address, value, address, value);

    finish(c, 0, log_message, response);
}

static void handle_push(core_ctx *c, int value)
{
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];

    if (top >= STACK_SIZE - 1)
    {
        snprintf(response, sizeof(response), "ERROR: Stack overflow");
        snprintf(log_message, sizeof(log_message),
                 "PUSH %d -> ERROR: Stack overflow", value);
        finish(c, 1, log_message, response);
        return;
    }

    stack_data[++top] = value;

    snprintf(response, sizeof(response), "PUSHED %d", value);
    snprintf(log_message, sizeof(log_message),
             "PUSH %d -> Stack updated", value);

    finish(c, 0, log_message, response);
}

static void handle_pop(core_ctx *c)
{
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];
    int value;

    if (top < 0)
    {
        snprintf(response, sizeof(response), "ERROR: Stack underflow");
        snprintf(log_message, sizeof(log_message),
                 "POP -> ERROR: Stack underflow");
        finish(c, 1, log_message, response);
        return;
    }

    value = stack_data[top--];

    snprintf(response, sizeof(response), "POPPED %d", value);
    snprintf(log_message, sizeof(log_message), "POP -> %d removed", value);

    finish(c, 0, log_message, response);
}

static void handle_peek(core_ctx *c)
{
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];

    if (top < 0)
    {
        snprintf(response, sizeof(response), "ERROR: Stack is empty");
        snprintf(log_message, sizeof(log_message),
                 "PEEK -> ERROR: Stack is empty");
        finish(c, 1, log_message, response);
        return;
    }

    snprintf(response, sizeof(response), "TOP = %d", stack_data[top]);
    snprintf(log_message, sizeof(log_message), "PEEK -> %d", stack_data[top]);

    finish(c, 0, log_message, response);
}

/* Builds "Label: v1 v2 v3"; ends with " ..." if it does not fit. */
static void format_list(char *out, size_t size, const char *label,
                        const int *items, int count, int newest_first)
{
    size_t used;
    int i;

    used = (size_t)snprintf(out, size, "%s:", label);

    if (count == 0)
    {
        snprintf(out, size, "%s: Empty", label);
        return;
    }

    for (i = 0; i < count; i++)
    {
        int value = newest_first ? items[count - 1 - i] : items[i];
        char number[16];
        size_t n = (size_t)snprintf(number, sizeof(number), " %d", value);
        size_t reserve = (i < count - 1) ? 5 : 1; /* " ..." + NUL, or NUL */

        if (used + n + reserve > size)
        {
            snprintf(out + used, size - used, " ...");
            return;
        }

        memcpy(out + used, number, n + 1);
        used += n;
    }
}

static void handle_display_stack(core_ctx *c)
{
    char response[MSG_SIZE];

    format_list(response, sizeof(response), "Stack",
                stack_data, top + 1, 1);

    finish(c, 0, "STACK DISPLAY -> Stack displayed", response);
}

static void handle_enqueue(core_ctx *c, int value)
{
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];

    if (queue_count >= QUEUE_SIZE)
    {
        snprintf(response, sizeof(response), "ERROR: Queue is full");
        snprintf(log_message, sizeof(log_message),
                 "ENQUEUE %d -> ERROR: Queue is full", value);
        finish(c, 1, log_message, response);
        return;
    }

    queue_data[queue_count++] = value;

    snprintf(response, sizeof(response), "ENQUEUED %d", value);
    snprintf(log_message, sizeof(log_message),
             "ENQUEUE %d -> Queue updated", value);

    finish(c, 0, log_message, response);
}

static void handle_dequeue(core_ctx *c)
{
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];
    int value;

    if (queue_count == 0)
    {
        snprintf(response, sizeof(response), "ERROR: Queue is empty");
        snprintf(log_message, sizeof(log_message),
                 "DEQUEUE -> ERROR: Queue is empty");
        finish(c, 1, log_message, response);
        return;
    }

    value = queue_data[0];
    memmove(queue_data, queue_data + 1,
            (size_t)(queue_count - 1) * sizeof(queue_data[0]));
    queue_count--;

    snprintf(response, sizeof(response), "DEQUEUED %d", value);
    snprintf(log_message, sizeof(log_message), "DEQUEUE -> %d removed", value);

    finish(c, 0, log_message, response);
}

static void handle_display_queue(core_ctx *c)
{
    char response[MSG_SIZE];

    format_list(response, sizeof(response), "Queue",
                queue_data, queue_count, 0);

    finish(c, 0, "QUEUE DISPLAY -> Queue displayed", response);
}

static void handle_registers(core_ctx *c)
{
    char log_message[MSG_SIZE];
    char response[MSG_SIZE];

    snprintf(response, sizeof(response),
             "R1=%d R2=%d R3=%d R4=%d", R1, R2, R3, R4);
    snprintf(log_message, sizeof(log_message),
             "REGISTERS -> R1=%d R2=%d R3=%d R4=%d", R1, R2, R3, R4);

    finish(c, 0, log_message, response);
}

/* Tell the Logger to stop. The text must stay exactly "EXIT". */
static void handle_exit(core_ctx *c)
{
    drain_acks(c);

    if (timed_send(c->logger_out, "EXIT", c->log_msgsize) == -1)
        fprintf(stderr, "Core: could not send EXIT to Logger (%s)\n",
                strerror(errno));
    else if (wait_ack(c) == -1)
        fprintf(stderr, "Core: no EXIT confirmation from Logger (%s)\n",
                strerror(errno));

    send_response(c, "CORE TERMINATED");
}

/* ------------------------------------------------------------------ */
/* Setup / cleanup                                                     */
/* ------------------------------------------------------------------ */

static void cleanup(core_ctx *c)
{
    if (c->manager_in != (mqd_t)-1)
        mq_close(c->manager_in);
    if (c->manager_out != (mqd_t)-1)
        mq_close(c->manager_out);
    if (c->logger_out != (mqd_t)-1)
        mq_close(c->logger_out);
    if (c->logger_in != (mqd_t)-1)
        mq_close(c->logger_in);

    /* Core owns these two queues; the Logger removes its own. */
    mq_unlink(MANAGER_TO_CORE);
    mq_unlink(CORE_TO_MANAGER);

    free(c->ack_buf);
    c->ack_buf = NULL;
}

int main(int argc, char **argv)
{
    static const struct
    {
        const char *name;
        char op;
    } arith_ops[] = {{"ADD", '+'}, {"SUB", '-'}, {"MUL", '*'}, {"DIV", '/'}};

    core_ctx ctx;
    struct mq_attr attr;
    struct mq_attr qa;
    struct sigaction sa;
    char *command = NULL;
    size_t command_size = MSG_SIZE;
    char clean[MSG_SIZE];
    char operation[TOKEN_SIZE];
    char tok[MAX_TOKENS][TOKEN_SIZE];
    int ntok;
    ssize_t received;
    size_t k;
    int value;
    int address;
    int exit_code = EXIT_FAILURE;
    int i;

    memset(&ctx, 0, sizeof(ctx));
    ctx.manager_in = (mqd_t)-1;
    ctx.manager_out = (mqd_t)-1;
    ctx.logger_out = (mqd_t)-1;
    ctx.logger_in = (mqd_t)-1;

    for (i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--async") == 0)
        {
            ctx.async_logging = 1;
        }
        else
        {
            fprintf(stderr, "Usage: %s [--async]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }

    /* Ctrl+C / kill: leave the loop so the queues get cleaned up.
     * No SA_RESTART, so a blocked mq_receive returns with EINTR. */
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = QUEUE_MAX_MSG;
    attr.mq_msgsize = MSG_SIZE;

    /* Remove leftovers of a crashed run so attributes are always correct. */
    mq_unlink(MANAGER_TO_CORE);
    mq_unlink(CORE_TO_MANAGER);

    ctx.manager_in = mq_open(MANAGER_TO_CORE, O_CREAT | O_RDONLY, 0600, &attr);
    if (ctx.manager_in == (mqd_t)-1)
    {
        perror("mq_open manager->core");
        goto done;
    }

    ctx.manager_out = mq_open(CORE_TO_MANAGER, O_CREAT | O_WRONLY, 0600, &attr);
    if (ctx.manager_out == (mqd_t)-1)
    {
        perror("mq_open core->manager");
        goto done;
    }

    ctx.logger_out = mq_open(CORE_TO_LOGGER, O_WRONLY);
    if (ctx.logger_out == (mqd_t)-1)
    {
        perror("mq_open core->logger (is the Logger running?)");
        goto done;
    }

    ctx.logger_in = mq_open(LOGGER_TO_CORE, O_RDONLY);
    if (ctx.logger_in == (mqd_t)-1)
    {
        perror("mq_open logger->core (is the Logger running?)");
        goto done;
    }

    /* Size buffers from the real queue attributes: mq_receive() fails
     * with EMSGSIZE if the buffer is smaller than mq_msgsize. */
    if (mq_getattr(ctx.manager_in, &qa) == -1)
    {
        perror("mq_getattr manager->core");
        goto done;
    }
    command_size = (size_t)qa.mq_msgsize;

    if (mq_getattr(ctx.logger_out, &qa) == -1)
    {
        perror("mq_getattr core->logger");
        goto done;
    }
    ctx.log_msgsize = qa.mq_msgsize;

    if (mq_getattr(ctx.logger_in, &qa) == -1)
    {
        perror("mq_getattr logger->core");
        goto done;
    }
    ctx.ack_size = (size_t)qa.mq_msgsize;

    command = malloc(command_size + 1);
    ctx.ack_buf = malloc(ctx.ack_size + 1);

    if (command == NULL || ctx.ack_buf == NULL)
    {
        perror("malloc");
        goto done;
    }

    printf("Core process started (%s logging).\n",
           ctx.async_logging ? "async" : "sync");
    fflush(stdout);

    while (running)
    {
        received = mq_receive(ctx.manager_in, command, command_size, NULL);

        if (received == -1)
        {
            if (errno == EINTR)
                continue;

            perror("mq_receive");
            break;
        }

        command[received] = '\0';
        sanitize(clean, sizeof(clean), command);

        ntok = split_tokens(command, tok);

        if (ntok == 0)
        {
            reject(&ctx, "(empty command)", "Empty command");
            continue;
        }

        if (ntok < 0)
        {
            reject(&ctx, clean, "Argument too long");
            continue;
        }

        for (i = 0; i < ntok && i < MAX_TOKENS; i++)
            sanitize(tok[i], sizeof(tok[i]), tok[i]);

        snprintf(operation, sizeof(operation), "%s", tok[0]);

        /* ADD / SUB / MUL / DIV */
        for (k = 0; k < sizeof(arith_ops) / sizeof(arith_ops[0]); k++)
            if (strcmp(operation, arith_ops[k].name) == 0)
                break;

        if (k < sizeof(arith_ops) / sizeof(arith_ops[0]))
        {
            if (ntok == 3)
                handle_arith(&ctx, arith_ops[k].name, arith_ops[k].op,
                             tok[1], tok[2]);
            else
                reject_syntax(&ctx, clean, operation);
            continue;
        }

        if (strcmp(operation, "LOAD") == 0)
        {
            if (parse_args(&ctx, clean, operation, ntok, 3, tok,
                           2, &value, -1, NULL))
                handle_load(&ctx, tok[1], value);
        }
        else if (strcmp(operation, "STORE") == 0)
        {
            if (parse_args(&ctx, clean, operation, ntok, 3, tok,
                           2, &address, -1, NULL))
                handle_store(&ctx, tok[1], address);
        }
        else if (strcmp(operation, "READ") == 0)
        {
            if (parse_args(&ctx, clean, operation, ntok, 2, tok,
                           1, &address, -1, NULL))
                handle_read(&ctx, address);
        }
        else if (strcmp(operation, "WRITE") == 0)
        {
            if (parse_args(&ctx, clean, operation, ntok, 3, tok,
                           1, &address, 2, &value))
                handle_write(&ctx, address, value);
        }
        else if (strcmp(operation, "PUSH") == 0)
        {
            if (parse_args(&ctx, clean, operation, ntok, 2, tok,
                           1, &value, -1, NULL))
                handle_push(&ctx, value);
        }
        else if (strcmp(operation, "POP") == 0)
        {
            handle_pop(&ctx);
        }
        else if (strcmp(operation, "PEEK") == 0)
        {
            handle_peek(&ctx);
        }
        else if (strcmp(operation, "DISPLAY_STACK") == 0)
        {
            handle_display_stack(&ctx);
        }
        else if (strcmp(operation, "ENQUEUE") == 0)
        {
            if (parse_args(&ctx, clean, operation, ntok, 2, tok,
                           1, &value, -1, NULL))
                handle_enqueue(&ctx, value);
        }
        else if (strcmp(operation, "DEQUEUE") == 0)
        {
            handle_dequeue(&ctx);
        }
        else if (strcmp(operation, "DISPLAY_QUEUE") == 0)
        {
            handle_display_queue(&ctx);
        }
        else if (strcmp(operation, "REGISTERS") == 0)
        {
            handle_registers(&ctx);
        }
        else if (strcmp(operation, "EXIT") == 0)
        {
            handle_exit(&ctx);
            break;
        }
        else
        {
            reject(&ctx, clean, "Unknown command");
        }
    }

    exit_code = EXIT_SUCCESS;

    if (ctx.log_failures > 0)
        fprintf(stderr, "Core: %lu log message(s) were not delivered.\n",
                ctx.log_failures);

done:
    cleanup(&ctx);
    free(command);

    return exit_code;
}
