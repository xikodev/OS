#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define MAX_LEVEL 4
#define STACK_SIZE 32

volatile sig_atomic_t running = 1;

/* 0 means the main program is running, anything else is an ISR level */
volatile sig_atomic_t current_priority = 0;

/* if pending[i] is 1, that interrupt level is waiting to be handled */
volatile sig_atomic_t pending[MAX_LEVEL + 1] = {0};

/* fake call stack where I keep old priorities while nesting interrupts */
volatile sig_atomic_t prio_stack[STACK_SIZE];
volatile sig_atomic_t sp = 0;

struct timespec t0;

/* ---------- time helpers ---------- */

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

#define PRINTF(...)            \
    do {                       \
        print_timestamp();     \
        printf(__VA_ARGS__);   \
        fflush(stdout);        \
    } while (0)

/* sleep for whole seconds even if a signal wakes nanosleep up in the middle */
static void good_sleep(time_t sec)
{
    struct timespec req, rem;
    req.tv_sec = sec;
    req.tv_nsec = 0;

    while (nanosleep(&req, &rem) == -1 && errno == EINTR) {
        req = rem;
    }
}

/* ---------- printing helpers ---------- */

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
    PRINTF("STATE CHANGE: %s\n", where);
    PRINTF("  Current priority (C_P): %d\n", current_priority);
    PRINTF("  Pending flags   (C_F): ");
    print_pending();
    printf("\n");
    PRINTF("  Stack: ");
    print_stack();
    printf("\n");

    if (current_priority == 0) {
        PRINTF("  Running: MAIN PROGRAM\n");
    } else {
        PRINTF("  Running: ISR level %d\n", current_priority);
    }

    printf("\n");
    fflush(stdout);
}

/* ---------- interrupt logic ---------- */

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
        PRINTF("Enter interrupt priority (1-4): ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            clearerr(stdin);
            continue;
        }

        if (sscanf(line, "%d", &level) == 1 && level >= 1 && level <= 4) {
            return level;
        }

        PRINTF("Invalid input. Please enter 1, 2, 3 or 4.\n");
    }
}

static void process_ready_interrupts(void);

static void execute_isr(int level)
{
    int i;
    int old_priority;

    if (sp >= STACK_SIZE) {
        PRINTF("ERROR: simulated stack overflow.\n");
        running = 0;
        return;
    }

    /* save what was running so we can come back after this ISR finishes */
    old_priority = current_priority;
    prio_stack[sp++] = old_priority;

    pending[level] = 0;
    current_priority = level;
    print_state("Interrupt accepted, context saved, ISR started");

    for (i = 1; i <= 5; i++) {
        PRINTF("ISR level %d: step %d/5\n", level, i);
        good_sleep(1);
    }

    PRINTF("ISR level %d finished\n", level);

    /* this is the "return from interrupt" part of the simulation */
    if (sp <= 0) {
        PRINTF("ERROR: simulated stack underflow.\n");
        running = 0;
        return;
    }

    current_priority = prio_stack[--sp];
    print_state("ISR ended, previous context restored");

    /* once we return, check if something more important is still waiting */
    process_ready_interrupts();
}

static void process_ready_interrupts(void)
{
    int next;

    while ((next = highest_pending_above(current_priority)) != 0) {
        execute_isr(next);
    }
}

/* ---------- signal handlers ---------- */

static void sigint_handler(int sig)
{
    int level;
    (void)sig;

    PRINTF("SIGINT received\n");

    level = read_interrupt_priority();

    pending[level] = 1;
    print_state("New interrupt request registered");

    /*
     * simple rule for this simulator:
     * higher priority interrupts preempt immediately,
     * lower/equal ones just stay pending for later
     */
    process_ready_interrupts();
}

static void sigterm_handler(int sig)
{
    (void)sig;
    running = 0;
    PRINTF("SIGTERM received -> simulator stopping\n");
}

/* ---------- setup ---------- */

static void initialize_signals(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);

    /*
     * without SA_NODEFER, a SIGINT handler would block another SIGINT.
     * here we want nested interrupts, so we leave that door open.
     */
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

/* ---------- main ---------- */

int main(void)
{
    int iteration = 1;

    /* setup first so timestamps and handlers are ready before anything prints */
    set_start_time();
    initialize_signals();

    PRINTF("Hardware interrupt simulator started\n");
    PRINTF("PID = %ld\n", (long)getpid());
    PRINTF("Send SIGINT to simulate an interrupt request.\n");
    PRINTF("Then type interrupt priority 1-4.\n");
    PRINTF("Send SIGTERM to stop the simulator.\n\n");

    print_state("Initial state");

    while (running) {
        /* this loop is just the "normal" CPU work between interrupts */
        PRINTF("MAIN PROGRAM: iteration %d\n", iteration++);
        good_sleep(1);
    }

    PRINTF("Simulator finished\n");
    return 0;
}
