#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

/* signal handlers declarations */
void proces_event(int sig);
void process_sigterm(int sig);
void process_sigint(int sig);

int run = 1;

int main()
{
    struct sigaction act;

    /* 1. masking signal SIGUSR1 */

    /* signal handler function */
    act.sa_handler = proces_event;

    /* additionally block SIGTERM in handler function */
    sigemptyset(&act.sa_mask);
    sigaddset(&act.sa_mask, SIGTERM);

    act.sa_flags = 0; /* advanced features not used */

    /* mask the signal SIGUSR1 as described above */
    sigaction(SIGUSR1, &act, NULL);

    /* 2. masking signal SIGTERM */
    act.sa_handler = process_sigterm;
    sigemptyset(&act.sa_mask);
    sigaction(SIGTERM, &act, NULL);

    /* 3. masking signal SIGINT */
    act.sa_handler = process_sigint;
    sigaction(SIGINT, &act, NULL);

    printf("Process with PID=%ld started\n", (long) getpid());

    /* processing simulation */
    int i = 1;
    while(run) {
        printf("Process: iteration %d\n", i++);
        sleep(1);
    }

    printf("Process with PID=%ld finished\n", (long) getpid());

    return 0;
}

void proces_event(int sig)
{
    int i;
    printf("Event processing started for signal %d (SIGINT)\n", sig);
    for (i = 1; i <= 5; i++) {
        printf("Processing signal %d: %d/5\n", sig, i);
        sleep(1);
    }
    printf("Event processing completed for signal %d (SIGINT)\n", sig);
}

void process_sigterm(int sig)
{
    printf("Received SIGTERM, saving data before exit\n");
    run = 0;
}

void process_sigint(int sig)
{
    printf("Received SIGINT, canceling process\n");
    exit(1);
}