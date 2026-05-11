#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


#define BUFFER_SIZE     5

typedef struct {
    int in;
    int out;
    char buffer[BUFFER_SIZE];
    sem_t mtx;
    sem_t full;
    sem_t empty;

} SharedData;

static void die(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}


static void producer(SharedData *shared, const char *text, int id) {
    int i = 0;

    while (1) {
        char c = text[i];

        while (sem_wait(&shared->empty) == -1) {
            if (errno != EINTR) {
                die("sem_wait");

            }
        }
        while (sem_wait(&shared->mtx) == -1) {
            if (errno != EINTR) {
                die("sem_wait");
            }
        }

        shared->buffer[shared->in] = c;
        shared->in = (shared->in + 1) % BUFFER_SIZE;

        if (sem_post(&shared->mtx) == -1) {
            die("sem_post");
        }
        if (sem_post(&shared->full) == -1) {
            die("sem_post");
        }
        if (c == '\0') {
            printf("PRODUCER%d ->\n", id);
        } else {
            printf("PRODUSER%d -> %c\n", id, c);

        }
        fflush(stdout);
        sleep(1);

        if (c == '\0') {
            break;
        }
        i++;
    }

    _exit(EXIT_SUCCESS);
}

static void consumer(SharedData *shared, int producer_count, size_t total_len) {
    char *result;
    size_t pos = 0;
    int finished = 0;

    result = malloc(total_len + 1);
    if (result == NULL) {

        die("malloc");
    }
    while (finished < producer_count) {
        char c;

        while (sem_wait(&shared->full) == -1) {
            if (errno != EINTR) {
                die("sem_wait");
            }
        }
        while (sem_wait(&shared->mtx) == -1) {
            if (errno != EINTR) {

                die("sem_wait");
            }
        }

        c = shared->buffer[shared->out];
        shared->out = (shared->out + 1) % BUFFER_SIZE;

        if (sem_post(&shared->mtx) == -1) {
            die("sem_post");
        }
        if (sem_post(&shared->empty) == -1) {
            die("sem_post");
        }
        if (c == '\0') {
            printf("CONSUMER<-\n");

        } else {
            printf("CONSUMER <-%c\n", c);
        }
        fflush(stdout);
        if (c == '\0') {
            finished++;
        } else {
            result[pos] = c;
            pos++;
        }
    }

    result[pos] = '\0';
    printf("\nReceived : %s\n", result);
    fflush(stdout);
    free(result);
    _exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    SharedData *shared;
    pid_t consumer_pid;
    int producer_count;
    size_t total_len = 0;
    int i;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <string1> [string2 ...]\n", argv[0]);
        return EXIT_FAILURE;
    }
    producer_count = argc - 1;
    for (i = 1; i < argc; i++) {
        total_len += strlen(argv[i]);
    }
    shared = mmap(NULL, sizeof(*shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared == MAP_FAILED) {
        die("mmap");
    }

    shared->in = 0;

    shared->out = 0;

    if (sem_init(&shared->mtx, 1, 1) == -1) {
        die("sem_init");
    }
    if (sem_init(&shared->full, 1, 0) == -1) {
        die("sem_init");
    }
    if (sem_init(&shared->empty, 1, BUFFER_SIZE) == -1) {
        die("sem_init");
    }
    consumer_pid = fork();
    if (consumer_pid == -1) {
        die("fork");
    }
    if (consumer_pid == 0) {
        consumer(shared, producer_count, total_len);
    }
    for (i = 0; i < producer_count; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            die("fork");
        }
        if (pid == 0) {
            producer(shared, argv[i + 1], i + 1);
        }
    }
    for (i = 0; i < producer_count + 1; i++) {
        if (wait(NULL) == -1) {
            die("wait");
        }
    }
    if (sem_destroy(&shared->mtx) == -1) {
        die("sem_destroy");
    }
    if (sem_destroy(&shared->full) == -1) {
        die("sem_destroy");
    }
    if (sem_destroy(&shared->empty) == -1) {
        die("sem_destroy");
    }
    if (munmap(shared, sizeof(*shared)) == -1) {
        die("munmap");
    }

    return EXIT_SUCCESS;
}
