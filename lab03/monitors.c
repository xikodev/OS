
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>

#define TEAM_COUNT 2
#define DRIVERS_PER_TEAM 3
#define MECHANICS_PER_TEAM 4

typedef struct {
    int team;
    int num;
    unsigned int seed;
} Info;

typedef struct {
    pthread_mutex_t m;
    pthread_cond_t driver_cv;
    pthread_cond_t mechanic_cv;
    pthread_cond_t flag_cv;
    int team;
    int waiting;
    int next_ticket;
    int turn;
    int pit_open;
    int in_pit;
    int busy;
    int working;
    int done;
    int cycle;
    int finished_mechanics;
} Pit;

static Pit pits[TEAM_COUNT];
static pthread_mutex_t start_m = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t start_cv = PTHREAD_COND_INITIALIZER;
static int started = 0;
static int running = 1;
static int finished = 0;

static void fail(const char *s) {
    perror(s);
    exit(1);
}

static void check(int code, const char *s) {
    if (code != 0) {
        fprintf(stderr, "%s: %s\n", s, strerror(code));
        exit(1);
    }
}


static void sleep_ms(int ms) {
    struct timespec req;
    struct timespec rem;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;

    while (nanosleep(&req, &rem) == -1) {
        if (errno != EINTR) {
            fail("nanosleep");
    }
        req = rem;
    }
}

static unsigned int next_rand(unsigned int *seed) {
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}
static int rand_between(unsigned int *seed, int a, int b) {
    return a + (int)(next_rand(seed) % (unsigned int)(b - a + 1));
}
static unsigned int make_seed(unsigned int base, int team, int num, int add) {
    return base + (unsigned int)(team * 97 + num * 31 + add * 17 + 11);
}
static void drive(Info *x, int ms) {
    printf("TEAM %d DRIVER %d driving for %d ms\n", x->team, x->num, ms);
    fflush(stdout);
    sleep_ms(ms);
}



static void stop_race(void){
    int i;

    for (i = 0; i < TEAM_COUNT; i++) {
        check(pthread_mutex_lock(&pits[i].m), "pthread_mutex_lock");
        check(pthread_cond_broadcast(&pits[i].driver_cv), "pthread_cond_broadcast");
        check(pthread_cond_broadcast(&pits[i].mechanic_cv), "pthread_cond_broadcast");
        check(pthread_cond_broadcast(&pits[i].flag_cv), "pthread_cond_broadcast");

        check(pthread_mutex_unlock(&pits[i].m), "pthread_mutex_unlock");
    }
}

static void wait_start(void) {
    check(pthread_mutex_lock(&start_m), "pthread_mutex_lock");

    while (!started) {
        check(pthread_cond_wait(&start_cv, &start_m), "pthread_cond_wait");
    }

    check(pthread_mutex_unlock(&start_m), "pthread_mutex_unlock");
}
static int race_running(void){
    int value;

    check(pthread_mutex_lock(&start_m), "pthread_mutex_lock");
    value = running;
    check(pthread_mutex_unlock(&start_m), "pthread_mutex_unlock");
    return value;
}


static void driver_pit_stop(int team_index, int driver_num)
 {
    Pit *p = &pits[team_index];
    int ticket;

    check(pthread_mutex_lock(&p->m), "pthread_mutex_lock");

    ticket = p->next_ticket;
    p->next_ticket++;
    p->waiting++;

    printf("TEAM %d DRIVER %d waiting for pitstop\n", p->team, driver_num);
    fflush(stdout);

    check(pthread_cond_signal(&p->flag_cv), "pthread_cond_signal");

    while (race_running() && (!p->pit_open || p->turn != ticket)) {
        check(pthread_cond_wait(&p->driver_cv, &p->m), "pthread_cond_wait");
    }

    if (!race_running()) {
        check(pthread_mutex_unlock(&p->m), "pthread_mutex_unlock");
        return;
    }

    p->waiting--;
    p->pit_open = 0;
    p->busy = 1;
    p->in_pit = 1;

    printf("TEAM %d  DRIVER %d entered pit\n", p->team, driver_num);
    fflush(stdout);

    check(pthread_cond_signal(&p->flag_cv), "pthread_cond_signal");

    while (race_running() && !p->done) {
        check(pthread_cond_wait(&p->driver_cv, &p->m), "pthread_cond_wait");
    }

    if (p->done) {
        printf("TEAM %d DRIVER  %d leaving pit\n", p->team, driver_num);
        fflush(stdout);

        p->done = 0;
        p->in_pit = 0;
        p->busy = 0;
        p->turn++;

        check(pthread_cond_signal(&p->flag_cv), "pthread_cond_signal");
    }

    check(pthread_mutex_unlock(&p->m), "pthread_mutex_unlock");
}



static void *driver(void *arg) {
    Info *x = (Info *)arg;
    int t1;
    int t2;
    int t3;

    wait_start();

    t1 = rand_between(&x->seed, 2000, 5000);
    t2 = 7000 - t1;
    t3 = rand_between(&x->seed, 2000, 3000);

    drive(x, t1);
    driver_pit_stop(x->team - 1, x->num);
    drive(x, t2);
    driver_pit_stop(x->team - 1, x->num);
    drive(x, t3);
    printf("TEAM %d DRIVER %d finnished race\n", x->team, x->num);
    fflush(stdout);

    {
        int last = 0;
        check(pthread_mutex_lock(&start_m), "pthread_mutex_lock");
        finished++;
        if (finished == TEAM_COUNT * DRIVERS_PER_TEAM) {
            running = 0;
            last = 1;
        }
        check(pthread_mutex_unlock(&start_m), "pthread_mutex_unlock");
        if (last) {
                stop_race();
        }
    }

    return NULL;
}


static void *mechanic(void *arg) {
    Info *x = (Info *)arg;
    Pit *p = &pits[x->team - 1];
    int last_cycle = 0;

    wait_start();

    while (1) {
        int my_cycle;
        int ms;
        check(pthread_mutex_lock(&p->m), "pthread_mutex_lock");

        while (running && (!p->working || p->cycle == last_cycle)) {
            check(pthread_cond_wait(&p->mechanic_cv, &p->m), "pthread_cond_wait");
        }
        if (!running) {
            check(pthread_mutex_unlock(&p->m), "pthread_mutex_unlock");
            break;
        }

        my_cycle = p->cycle;
        check(pthread_mutex_unlock(&p->m), "pthread_mutex_unlock");
        ms = rand_between(&x->seed, 2000, 4000);
        printf("TEAM %d MECANIC %d changing wheel for %d ms\n", x->team, x->num, ms);
        fflush(stdout);
        sleep_ms(ms);

        check(pthread_mutex_lock(&p->m), "pthread_mutex_lock");

        if (p->working && p->cycle == my_cycle && last_cycle != my_cycle) {
            p->finished_mechanics++;
            last_cycle = my_cycle;

            printf("TEAM %d MECHANIC %d finished wheel, remaining %d\n",
                   x->team, x->num, MECHANICS_PER_TEAM - p->finished_mechanics);
            fflush(stdout);

            if (p->finished_mechanics == MECHANICS_PER_TEAM) {
                check(pthread_cond_signal(&p->flag_cv), "pthread_cond_signal");
            }
        }
        check(pthread_mutex_unlock(&p->m), "pthread_mutex_unlock");
    }

    printf("TEAM %d MECHANIC %d stopping\n", x->team, x->num);
    fflush(stdout);
    return NULL;
}
static void *flag_person(void *arg)  {
    Info *x = (Info *)arg;
    Pit *p = &pits[x->team - 1];


    wait_start();

    while (1) {
        check(pthread_mutex_lock(&p->m), "pthread_mutex_lock");


        while (running && p->waiting == 0) {
            check(pthread_cond_wait(&p->flag_cv, &p->m), "pthread_cond_wait");
        }
        if (!running) {
            check(pthread_mutex_unlock(&p->m), "pthread_mutex_unlock");
            break;
        }
        while (running && p->busy) {
            check(pthread_cond_wait(&p->flag_cv, &p->m), "pthread_cond_wait");
        }
        if (!running) {
            check(pthread_mutex_unlock(&p->m), "pthread_mutex_unlock");
            break;
        }
        p->pit_open = 1;
        printf("TEAM %d FLAG: raised, driver may enter pit\n", x->team);
        fflush(stdout);

        check(pthread_cond_broadcast(&p->driver_cv), "pthread_cond_broadcast");

        while (running && !p->in_pit) {
            check(pthread_cond_wait(&p->flag_cv, &p->m), "pthread_cond_wait");
        }
        if (!running) {
            check(pthread_mutex_unlock(&p->m), "pthread_mutex_unlock");
            break;
        }
        p->working = 1;
        p->cycle++;
        p->finished_mechanics = 0;


        printf("TEAM %d FLAG: lowered, mechanics start\n", x->team);
        fflush(stdout);
        check(pthread_cond_broadcast(&p->mechanic_cv), "pthread_cond_broadcast");

        while (running && p->finished_mechanics < MECHANICS_PER_TEAM) {
            check(pthread_cond_wait(&p->flag_cv, &p->m), "pthread_cond_wait");
        }
        if (!running) {
            check(pthread_mutex_unlock(&p->m), "pthread_mutex_unlock");
            break;
        }
        p->working = 0;
        p->done = 1;

        printf("TEAM %d FLAG: raised, driver released\n", x->team);
        fflush(stdout);

        check(pthread_cond_broadcast(&p->driver_cv), "pthread_cond_broadcast");
        while (running && p->busy) {
            check(pthread_cond_wait(&p->flag_cv, &p->m), "pthread_cond_wait");
        }


        check(pthread_mutex_unlock(&p->m), "pthread_mutex_unlock");
    }
    printf("TEAM %d FLAG stoping\n", x->team);
    fflush(stdout);
    return NULL;
}

int main(void){
    pthread_t drivers[TEAM_COUNT][DRIVERS_PER_TEAM];
    pthread_t mechanics[TEAM_COUNT][MECHANICS_PER_TEAM];

    pthread_t flags[TEAM_COUNT];
    Info d[TEAM_COUNT][DRIVERS_PER_TEAM];
    Info m[TEAM_COUNT][MECHANICS_PER_TEAM];
    Info f[TEAM_COUNT];

    unsigned int base = (unsigned int)time(NULL);
    int i;
    int j;

    for (i = 0; i < TEAM_COUNT; i++) {
        memset(&pits[i], 0, sizeof(pits[i]));
        pits[i].team = i + 1;
        pits[i].next_ticket = 1;
        pits[i].turn = 1;
        check(pthread_mutex_init(&pits[i].m, NULL), "pthread_mutex_init");
        check(pthread_cond_init(&pits[i].driver_cv, NULL), "pthread_cond_init");
        check(pthread_cond_init(&pits[i].mechanic_cv, NULL), "pthread_cond_init");
        check(pthread_cond_init(&pits[i].flag_cv, NULL), "pthread_cond_init");
    }
    for (i = 0; i < TEAM_COUNT; i++) {
        for (j = 0; j < DRIVERS_PER_TEAM; j++) {
            d[i][j].team = i + 1;
            d[i][j].num = j + 1;
            d[i][j].seed = make_seed(base, i + 1, j + 1, 1);
            check(pthread_create(&drivers[i][j], NULL, driver, &d[i][j]), "pthread_create");
        }
        for (j = 0; j < MECHANICS_PER_TEAM; j++) {
            m[i][j].team = i + 1;
            m[i][j].num = j + 1;
            m[i][j].seed = make_seed(base, i + 1, j + 1, 50);
            check(pthread_create(&mechanics[i][j], NULL, mechanic, &m[i][j]), "pthread_create");
        }

        f[i].team = i + 1;
        f[i].num = 1;
        f[i].seed = make_seed(base, i + 1, 1, 90);
        check(pthread_create(&flags[i], NULL, flag_person, &f[i]), "pthread_create");
    }

    printf("Race setup complete: 6 drivers, 8mechanics, 2 flag persons \n");
    printf("Race started!! \n");
    fflush(stdout);
    check(pthread_mutex_lock(&start_m), "pthread_mutex_lock");
    started = 1;
    check(pthread_cond_broadcast(&start_cv), "pthread_cond_broadcast");

    check(pthread_mutex_unlock(&start_m), "pthread_mutex_unlock");

    for (i = 0; i < TEAM_COUNT; i++) {
        for (j = 0; j < DRIVERS_PER_TEAM; j++) {
            check(pthread_join(drivers[i][j], NULL), "pthread_join");
        }
    }
    for (i = 0; i < TEAM_COUNT; i++) {
        check(pthread_join(flags[i], NULL), "pthread_join");

        for (j = 0; j < MECHANICS_PER_TEAM; j++) {
            check(pthread_join(mechanics[i][j], NULL), "pthread_join");
        }
    }
    for (i = 0; i < TEAM_COUNT; i++) {
        check(pthread_mutex_destroy(&pits[i].m), "pthread_mutex_destroy");

        check(pthread_cond_destroy(&pits[i].driver_cv), "pthread_cond_destroy");

        check(pthread_cond_destroy(&pits[i].mechanic_cv), "pthread_cond_destroy");
        check(pthread_cond_destroy(&pits[i].flag_cv), "pthread_cond_destroy");
    }

    printf("Race finished\n");

    return 0;
}



