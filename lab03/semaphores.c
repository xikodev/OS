#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUFFER_SIZE 5

typedef struct {
    int in;
    int out;
    char buffer[BUFFER_SIZE];
    sem_t write_sem;
    sem_t messages_sem;
    sem_t slots_sem;
} SharedData;

static void die(const char *message)
{
    perror(message);
    exit(EXIT_FAILURE);
}

static void wait_sem(sem_t *sem)
{
    while (sem_wait(sem) == -1) {
        if (errno != EINTR) {
            die("sem_wait");
        }
    }
}

static void signal_sem(sem_t *sem)
{
    if (sem_post(sem) == -1) {
        die("sem_post");
    }
}

static void print_transfer(const char *prefix, int id, char value, const char *arrow)
{
    if (id > 0) {
        if (value == '\0') {
            printf("%s%d %s\n", prefix, id, arrow);
        } else {
            printf("%s%d %s %c\n", prefix, id, arrow, value);
        }
    } else {
        if (value == '\0') {
            printf("%s %s\n", prefix, arrow);
        } else {
            printf("%s %s %c\n", prefix, arrow, value);
        }
    }
    fflush(stdout);
}

static void producer_process(SharedData *shared, const char *text, int producer_id)
{
    size_t i = 0;

    do {
        char value = text[i];

        wait_sem(&shared->slots_sem);
        wait_sem(&shared->write_sem);

        shared->buffer[shared->in] = value;
        shared->in = (shared->in + 1) % BUFFER_SIZE;

        signal_sem(&shared->write_sem);
        signal_sem(&shared->messages_sem);

        print_transfer("PRODUCER", producer_id, value, "->");
        sleep(1);
        i++;
    } while (text[i - 1] != '\0');

    _exit(EXIT_SUCCESS);
}

static void consumer_process(SharedData *shared, int producer_count, size_t total_chars)
{
    char *received;
    size_t index = 0;
    int finished_producers = 0;

    received = malloc(total_chars + 1);
    if (received == NULL) {
        die("malloc");
    }

    while (finished_producers < producer_count) {
        char value;

        wait_sem(&shared->messages_sem);

        value = shared->buffer[shared->out];
        shared->out = (shared->out + 1) % BUFFER_SIZE;

        signal_sem(&shared->slots_sem);
        print_transfer("CONSUMER", 0, value, "<-");

        if (value == '\0') {
            finished_producers++;
        } else {
            received[index] = value;
            index++;
        }
    }

    received[index] = '\0';
    printf("\nReceived: %s\n", received);
    fflush(stdout);

    free(received);
    _exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
    SharedData *shared;
    pid_t consumer_pid;
    int producer_count;
    size_t total_chars = 0;
    int i;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <string1> [string2 ...]\n", argv[0]);
        return EXIT_FAILURE;
    }

    producer_count = argc - 1;
    for (i = 1; i < argc; i++) {
        total_chars += strlen(argv[i]);
    }

    shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE,
                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED) {
        die("mmap");
    }

    shared->in = 0;
    shared->out = 0;

    if (sem_init(&shared->write_sem, 1, 1) == -1) {
        die("sem_init write_sem");
    }
    if (sem_init(&shared->messages_sem, 1, 0) == -1) {
        die("sem_init messages_sem");
    }
    if (sem_init(&shared->slots_sem, 1, BUFFER_SIZE) == -1) {
        die("sem_init slots_sem");
    }

    consumer_pid = fork();
    if (consumer_pid == -1) {
        die("fork consumer");
    }
    if (consumer_pid == 0) {
        consumer_process(shared, producer_count, total_chars);
    }

    for (i = 0; i < producer_count; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            die("fork producer");
        }
        if (pid == 0) {
            producer_process(shared, argv[i + 1], i + 1);
        }
    }

    for (i = 0; i < producer_count + 1; i++) {
        if (wait(NULL) == -1) {
            die("wait");
        }
    }

    if (sem_destroy(&shared->write_sem) == -1) {
        die("sem_destroy write_sem");
    }
    if (sem_destroy(&shared->messages_sem) == -1) {
        die("sem_destroy messages_sem");
    }
    if (sem_destroy(&shared->slots_sem) == -1) {
        die("sem_destroy slots_sem");
    }
    if (munmap(shared, sizeof(*shared)) == -1) {
        die("munmap");
    }

    return EXIT_SUCCESS;
}
