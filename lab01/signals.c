#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define MAX_LEVEL 4
#define STACK_SIZE 32

volatile sig_atomic_t running = 1;
volatile sig_atomic_t current_priority = 0;
volatile sig_atomic_t pending[MAX_LEVEL + 1] = {0};
volatile sig_atomic_t prio_stack[STACK_SIZE];
volatile sig_atomic_t sp = 0;
volatile sig_atomic_t sigint_requests = 0;

struct timespec t0;

static void set_start_time(void)
{
    clock_gettime(CLOCK_REALTIME, &t0);
}

static void print_timestamp(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);

    t.tv_sec -= t0.tv_sec;
    t.tv_nsec -= t0.tv_nsec;
    if (t.tv_nsec < 0) {
        t.tv_nsec += 1000000000L;
        t.tv_sec--;
    }

    printf("%03ld.%03ld:\t", t.tv_sec, t.tv_nsec / 1000000L);
}

static void log_printf(const char *fmt, ...)
{
    va_list args;

    print_timestamp();
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

static void good_sleep(time_t sec)
{
    struct timespec req, rem;
    req.tv_sec = sec;
    req.tv_nsec = 0;

    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}

static void print_pending(void)
{
    int i;
    printf("[");
    for (i = 1; i <= MAX_LEVEL; i++) {
        printf("%d", pending[i]);
        if (i < MAX_LEVEL) {
            printf(" ");
        }
    }
    printf("]");
}

static void print_stack(void)
{
    int i;
    printf("[");
    for (i = 0; i < sp; i++) {
        printf("%d", prio_stack[i]);
        if (i < sp - 1) {
            printf(" ");
        }
    }
    printf("]");
}

static void print_state(const char *where)
{
    log_printf("STATE CHANGE: %s\n", where);
    log_printf("Current priority (C_P): %d\n", current_priority);
    log_printf("Pending flags   (C_F): ");
    print_pending();
    printf("\n");
    log_printf("Stack: ");
    print_stack();
    printf("\n");

    if (current_priority == 0) {
        log_printf("Running:MAIN PROGRAM\n");
    } else {
        log_printf("Running: ISR level %d\n", current_priority);
    }

    printf("\n");
    fflush(stdout);
}

static int highest_pending_above(int level)
{
    int i;
    for (i = MAX_LEVEL; i > level; i--) {
        if (pending[i]) {
            return i;
        }
    }
    return 0;
}

static int read_interrupt_priority(void)
{
    char line[64];
    int level = 0;

    while (1) {
        log_printf("Enter interupt priority (1- 4):");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            clearerr(stdin);
            continue;
        }

        if (sscanf(line, "%d", &level) == 1 && level >= 1 && level <= 4) {
            return level;
        }

        log_printf("Invalid input. Please enter 1, 2,3 or 4.\n");
    }
}

static void process_ready_interrupts(void);

static void execute_isr(int level)
{
    int i;
    int old_priority;

    if (sp >= STACK_SIZE) {
        log_printf("ERROR: simulated stack overflow.\n");
        running = 0;
        return;
    }

    old_priority = current_priority;
    prio_stack[sp++] = old_priority;

    pending[level] = 0;
    current_priority = level;
    print_state("Interrupt acepted, context saved, ISR started");

    for (i = 1; i <= 5; i++) {
        log_printf("ISR level %d: step %d/5\n", level, i);
        good_sleep(1);
    }

    log_printf("ISR level %d finisged\n", level);

    if (sp <= 0) {
        log_printf("ERROR: simulated stack underflow.\n");
        running = 0;
        return;
    }

    current_priority = prio_stack[--sp];
    print_state("ISR ended, previous context restored");

    process_ready_interrupts();
}

static void process_ready_interrupts(void)
{
    int next;

    while ((next = highest_pending_above(current_priority)) != 0) {
        execute_isr(next);
    }
}

static void sigint_handler(int sig)
{
    (void)sig;
    sigint_requests++;
}

static void sigterm_handler(int sig)
{
    (void)sig;
    running = 0;
    log_printf("SIGTERM received -> simulator stopping\n");
    print_state("Termination request registered");
}

static void initialize_signals(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);

    act.sa_flags = SA_NODEFER;
    act.sa_handler = sigint_handler;
    if (sigaction(SIGINT, &act, NULL) == -1) {
        perror("sigaction SIGINT");
        exit(EXIT_FAILURE);
    }

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = sigterm_handler;
    if (sigaction(SIGTERM, &act, NULL) == -1) {
        perror("sigaction SIGTERM");
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    int iteration = 1;

    set_start_time();
    initialize_signals();

    log_printf("Hardware interrupt simulator started\n");
    log_printf("PID = %ld\n", (long)getpid());
    log_printf("Send SIGINT to simulate an interrupt request.\n");
    log_printf("Then type interrupt priority 1-4.\n");
    log_printf("Send SIGTERM to stop the simulator.\n\n");

    print_state("Initial state");

    while (running) {
        while (sigint_requests > 0 && running) {
            int level;

            sigint_requests--;
            log_printf("SIGINT recived\n");
            level = read_interrupt_priority();
            pending[level] = 1;
            print_state("New interupt request registered");
            process_ready_interrupts();
        }

        log_printf("MAIN PROGRAM: iteration %d\n", iteration++);
        good_sleep(1);
    }

    log_printf("Simulator finished\n");
    return 0;
}
