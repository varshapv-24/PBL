#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

#define MSG_SIZE 256
#define QUEUE_MAX_MSG 10
#define CORE_TO_LOGGER "/pbl_core_logger"
#define LOGGER_TO_CORE "/pbl_logger_core"
#define LOG_FILE "cpu_log.txt"

void get_timestamp(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm *time_info = localtime(&now);

    if (time_info == NULL)
    {
        snprintf(buffer, size, "00:00:00");
        return;
    }

    strftime(buffer, size, "%H:%M:%S", time_info);
}

int main(void)
{
    struct mq_attr attr;
    mqd_t incoming;
    mqd_t outgoing;
    FILE *log_file;
    char message[MSG_SIZE];
    char timestamp[20];
    ssize_t received;

    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = QUEUE_MAX_MSG;
    attr.mq_msgsize = MSG_SIZE;

    incoming = mq_open(CORE_TO_LOGGER, O_CREAT | O_RDONLY, 0666, &attr);

    if (incoming == (mqd_t)-1)
    {
        perror("mq_open core->logger");
        return EXIT_FAILURE;
    }

    outgoing = mq_open(LOGGER_TO_CORE, O_CREAT | O_WRONLY, 0666, &attr);

    if (outgoing == (mqd_t)-1)
    {
        perror("mq_open logger->core");
        mq_close(incoming);
        mq_unlink(CORE_TO_LOGGER);
        return EXIT_FAILURE;
    }

    log_file = fopen(LOG_FILE, "a");

    if (log_file == NULL)
    {
        perror("fopen");
        mq_close(incoming);
        mq_close(outgoing);
        mq_unlink(CORE_TO_LOGGER);
        mq_unlink(LOGGER_TO_CORE);
        return EXIT_FAILURE;
    }

    while (1)
    {
        received = mq_receive(incoming, message, MSG_SIZE, NULL);

        if (received == -1)
        {
            if (errno == EINTR)
                continue;

            perror("mq_receive");
            break;
        }

        if (received >= MSG_SIZE)
            message[MSG_SIZE - 1] = '\0';
        else
            message[received] = '\0';

        get_timestamp(timestamp, sizeof(timestamp));

        fprintf(log_file, "[%s] %s\n", timestamp, message);
        fflush(log_file);

        if (strcmp(message, "EXIT") == 0)
        {
            const char *response = "LOGGER TERMINATED";

            if (mq_send(outgoing, response, strlen(response) + 1, 0) == -1)
                perror("mq_send");

            break;
        }

        {
            const char *response = "LOG CONFIRMED";

            if (mq_send(outgoing, response, strlen(response) + 1, 0) == -1)
            {
                perror("mq_send");
                break;
            }
        }
    }

    fclose(log_file);
    mq_close(incoming);
    mq_close(outgoing);
    mq_unlink(CORE_TO_LOGGER);
    mq_unlink(LOGGER_TO_CORE);

    return EXIT_SUCCESS;
}
