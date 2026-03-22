#include <stdio.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

struct timespec t0; /* program start time */

/* sets the current time in t0 */
void set_start_time()
{
    clock_gettime(CLOCK_REALTIME, &t0);
}

/* retrieves the time elapsed since the program was started */
void print_timestamp(void)
{
    struct timespec t;

    clock_gettime(CLOCK_REALTIME, &t);

    t.tv_sec -= t0.tv_sec;
    t.tv_nsec -= t0.tv_nsec;
    if (t.tv_nsec < 0) {
        t.tv_nsec += 1000000000;
        t.tv_sec--;
    }

    printf("%03ld.%03ld:\t", t.tv_sec, t.tv_nsec/1000000);
}

/* print as "printf" with the addition of the current time at the beginning */
#define PRINTF(format, ...)       \
do {                              \
print_timestamp();              \
printf(format, ##__VA_ARGS__);  \
}                                 \
while(0)

/*
 * sleeps for a given number of seconds
 * if interrupted by a signal, it later resumes sleep
 */
void good_sleep(time_t sekundi)
{
    struct timespec t;
    t.tv_sec = sekundi;
    t.tv_nsec = 0;

    while (nanosleep(&t, &t) == -1 && errno == EINTR)
        PRINTF("Continuing, after being interrupted\n");
}

void sigint_handler(int sig)
{
    PRINTF("SIGINT: processing started\n");
    good_sleep(5);
    PRINTF("SIGINT: processing finished\n");
}

void initialization()
{
    struct sigaction act;

    act.sa_handler = sigint_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);

    set_start_time();
}

int main()
{
    initialization();

    PRINTF("G: Start of the main program\n");
    good_sleep(10);
    PRINTF("G: End of the main program\n");

    return 0;
}

/* example run:
$ gcc lab1-example__sleep.c -o lab1ps
$ ./lab1ps
000.000:        G: Start of the main program
^C002.046:      SIGINT: processing started
006.047:        SIGINT: processing finished
006.047:        Continuing, after being interrupted
014.003:        G: End of the main program
$
*/