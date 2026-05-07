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
    int team_id;
    int member_id;
    unsigned int seed;
} ThreadArgs;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t driver_cv;
    pthread_cond_t mechanic_cv;
    pthread_cond_t flag_cv;
    int team_id;
    int waiting_drivers;
    int next_ticket;
    int ticket_to_enter;
    bool pit_reserved;
    bool driver_in_pit;
    bool service_active;
    bool service_done;
    int mechanics_remaining;
    int service_cycle;
} TeamMonitor;

static TeamMonitor teams[TEAM_COUNT];
static pthread_mutex_t race_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t race_start_cv = PTHREAD_COND_INITIALIZER;
static bool race_started = false;
static bool race_running = true;
static int finished_drivers = 0;

static void die_pthread(int code, const char *what)
{
    fprintf(stderr, "%s: %s\n", what, strerror(code));
    exit(EXIT_FAILURE);
}

static void lock_mutex(pthread_mutex_t *mutex, const char *what)
{
    int code = pthread_mutex_lock(mutex);
    if (code != 0) {
        die_pthread(code, what);
    }
}

static void unlock_mutex(pthread_mutex_t *mutex, const char *what)
{
    int code = pthread_mutex_unlock(mutex);
    if (code != 0) {
        die_pthread(code, what);
    }
}

static void wait_cond(pthread_cond_t *cond, pthread_mutex_t *mutex, const char *what)
{
    int code = pthread_cond_wait(cond, mutex);
    if (code != 0) {
        die_pthread(code, what);
    }
}

static void signal_cond(pthread_cond_t *cond, const char *what)
{
    int code = pthread_cond_signal(cond);
    if (code != 0) {
        die_pthread(code, what);
    }
}

static void broadcast_cond(pthread_cond_t *cond, const char *what)
{
    int code = pthread_cond_broadcast(cond);
    if (code != 0) {
        die_pthread(code, what);
    }
}

static void sleep_ms(int ms)
{
    struct timespec req;
    struct timespec rem;

    req.tv_sec = ms / 1000;
    req.tv_nsec = (long)(ms % 1000) * 1000000L;

    while (nanosleep(&req, &rem) == -1) {
        if (errno != EINTR) {
            perror("nanosleep");
            exit(EXIT_FAILURE);
        }
        req = rem;
    }
}

static int random_range(unsigned int *seed, int min_value, int max_value)
{
    return min_value + (int)(rand_r(seed) % (unsigned int)(max_value - min_value + 1));
}

static void wait_for_race_start(void)
{
    lock_mutex(&race_mutex, "pthread_mutex_lock race_mutex");
    while (!race_started) {
        wait_cond(&race_start_cv, &race_mutex, "pthread_cond_wait race_start_cv");
    }
    unlock_mutex(&race_mutex, "pthread_mutex_unlock race_mutex");
}

static void maybe_finish_race(void)
{
    int team;

    lock_mutex(&race_mutex, "pthread_mutex_lock race_mutex");
    finished_drivers++;
    if (finished_drivers == TEAM_COUNT * DRIVERS_PER_TEAM) {
        race_running = false;
        unlock_mutex(&race_mutex, "pthread_mutex_unlock race_mutex");

        for (team = 0; team < TEAM_COUNT; team++) {
            lock_mutex(&teams[team].mutex, "pthread_mutex_lock team mutex");
            broadcast_cond(&teams[team].driver_cv, "pthread_cond_broadcast driver_cv");
            broadcast_cond(&teams[team].mechanic_cv, "pthread_cond_broadcast mechanic_cv");
            broadcast_cond(&teams[team].flag_cv, "pthread_cond_broadcast flag_cv");
            unlock_mutex(&teams[team].mutex, "pthread_mutex_unlock team mutex");
        }
        return;
    }
    unlock_mutex(&race_mutex, "pthread_mutex_unlock race_mutex");
}

static bool is_race_running(void)
{
    bool running_now;

    lock_mutex(&race_mutex, "pthread_mutex_lock race_mutex");
    running_now = race_running;
    unlock_mutex(&race_mutex, "pthread_mutex_unlock race_mutex");
    return running_now;
}

static void request_pit_stop(int team_index, int driver_id)
{
    TeamMonitor *team = &teams[team_index];
    int my_ticket;

    lock_mutex(&team->mutex, "pthread_mutex_lock team mutex");

    my_ticket = team->next_ticket;
    team->next_ticket++;
    team->waiting_drivers++;
    printf("TEAM %d DRIVER %d waiting for pit stop\n", team->team_id, driver_id);
    fflush(stdout);

    signal_cond(&team->flag_cv, "pthread_cond_signal flag_cv");

    while (is_race_running() && (!team->pit_reserved || team->ticket_to_enter != my_ticket)) {
        wait_cond(&team->driver_cv, &team->mutex, "pthread_cond_wait driver_cv");
    }

    if (!is_race_running()) {
        unlock_mutex(&team->mutex, "pthread_mutex_unlock team mutex");
        return;
    }

    team->waiting_drivers--;
    team->driver_in_pit = true;
    printf("TEAM %d DRIVER %d entered pit\n", team->team_id, driver_id);
    fflush(stdout);
    signal_cond(&team->flag_cv, "pthread_cond_signal flag_cv");

    while (is_race_running() && !team->service_done) {
        wait_cond(&team->driver_cv, &team->mutex, "pthread_cond_wait driver_cv");
    }

    if (team->service_done) {
        printf("TEAM %d DRIVER %d leaving pit\n", team->team_id, driver_id);
        fflush(stdout);
        team->service_done = false;
        team->driver_in_pit = false;
        team->pit_reserved = false;
        team->ticket_to_enter++;
        signal_cond(&team->flag_cv, "pthread_cond_signal flag_cv");
    }

    unlock_mutex(&team->mutex, "pthread_mutex_unlock team mutex");
}

static void *driver_thread(void *arg)
{
    ThreadArgs *info = (ThreadArgs *)arg;
    int first_drive;
    int second_drive;
    int final_drive;

    wait_for_race_start();

    first_drive = random_range(&info->seed, 2000, 5000);
    second_drive = 7000 - first_drive;
    final_drive = random_range(&info->seed, 2000, 3000);

    printf("TEAM %d DRIVER %d driving for %d ms\n", info->team_id, info->member_id, first_drive);
    fflush(stdout);
    sleep_ms(first_drive);

    request_pit_stop(info->team_id - 1, info->member_id);

    printf("TEAM %d DRIVER %d driving for %d ms\n", info->team_id, info->member_id, second_drive);
    fflush(stdout);
    sleep_ms(second_drive);

    request_pit_stop(info->team_id - 1, info->member_id);

    printf("TEAM %d DRIVER %d driving for %d ms\n", info->team_id, info->member_id, final_drive);
    fflush(stdout);
    sleep_ms(final_drive);

    printf("TEAM %d DRIVER %d finished race\n", info->team_id, info->member_id);
    fflush(stdout);
    maybe_finish_race();
    return NULL;
}

static void *mechanic_thread(void *arg)
{
    ThreadArgs *info = (ThreadArgs *)arg;
    TeamMonitor *team = &teams[info->team_id - 1];
    int last_cycle = 0;

    wait_for_race_start();

    while (1) {
        int cycle_to_process;
        int work_time;

        lock_mutex(&team->mutex, "pthread_mutex_lock team mutex");
        while (race_running && (!team->service_active || team->service_cycle == last_cycle)) {
            wait_cond(&team->mechanic_cv, &team->mutex, "pthread_cond_wait mechanic_cv");
        }

        if (!race_running) {
            unlock_mutex(&team->mutex, "pthread_mutex_unlock team mutex");
            break;
        }

        cycle_to_process = team->service_cycle;
        unlock_mutex(&team->mutex, "pthread_mutex_unlock team mutex");

        work_time = random_range(&info->seed, 2000, 4000);
        printf("TEAM %d MECHANIC %d changing wheel for %d ms\n",
               info->team_id, info->member_id, work_time);
        fflush(stdout);
        sleep_ms(work_time);

        lock_mutex(&team->mutex, "pthread_mutex_lock team mutex");
        if (team->service_active && team->service_cycle == cycle_to_process && last_cycle != cycle_to_process) {
            team->mechanics_remaining--;
            last_cycle = cycle_to_process;
            printf("TEAM %d MECHANIC %d finished wheel, remaining %d\n",
                   info->team_id, info->member_id, team->mechanics_remaining);
            fflush(stdout);
            if (team->mechanics_remaining == 0) {
                signal_cond(&team->flag_cv, "pthread_cond_signal flag_cv");
            }
        }
        unlock_mutex(&team->mutex, "pthread_mutex_unlock team mutex");
    }

    printf("TEAM %d MECHANIC %d stopping\n", info->team_id, info->member_id);
    fflush(stdout);
    return NULL;
}

static void *flag_thread(void *arg)
{
    ThreadArgs *info = (ThreadArgs *)arg;
    TeamMonitor *team = &teams[info->team_id - 1];

    wait_for_race_start();

    while (1) {
        lock_mutex(&team->mutex, "pthread_mutex_lock team mutex");

        while (race_running && team->waiting_drivers == 0) {
            wait_cond(&team->flag_cv, &team->mutex, "pthread_cond_wait flag_cv");
        }
        if (!race_running) {
            unlock_mutex(&team->mutex, "pthread_mutex_unlock team mutex");
            break;
        }

        while (race_running && team->pit_reserved) {
            wait_cond(&team->flag_cv, &team->mutex, "pthread_cond_wait flag_cv");
        }
        if (!race_running) {
            unlock_mutex(&team->mutex, "pthread_mutex_unlock team mutex");
            break;
        }

        team->pit_reserved = true;
        printf("TEAM %d FLAG: raised, driver may enter pit\n", info->team_id);
        fflush(stdout);
        broadcast_cond(&team->driver_cv, "pthread_cond_broadcast driver_cv");

        while (race_running && !team->driver_in_pit) {
            wait_cond(&team->flag_cv, &team->mutex, "pthread_cond_wait flag_cv");
        }
        if (!race_running) {
            unlock_mutex(&team->mutex, "pthread_mutex_unlock team mutex");
            break;
        }

        printf("TEAM %d FLAG: lowered, mechanics start\n", info->team_id);
        fflush(stdout);
        team->service_active = true;
        team->mechanics_remaining = MECHANICS_PER_TEAM;
        team->service_cycle++;
        broadcast_cond(&team->mechanic_cv, "pthread_cond_broadcast mechanic_cv");

        while (race_running && team->mechanics_remaining > 0) {
            wait_cond(&team->flag_cv, &team->mutex, "pthread_cond_wait flag_cv");
        }
        if (!race_running) {
            unlock_mutex(&team->mutex, "pthread_mutex_unlock team mutex");
            break;
        }

        team->service_active = false;
        team->service_done = true;
        printf("TEAM %d FLAG: raised, driver released\n", info->team_id);
        fflush(stdout);
        broadcast_cond(&team->driver_cv, "pthread_cond_broadcast driver_cv");

        while (race_running && team->pit_reserved) {
            wait_cond(&team->flag_cv, &team->mutex, "pthread_cond_wait flag_cv");
        }

        unlock_mutex(&team->mutex, "pthread_mutex_unlock team mutex");
    }

    printf("TEAM %d FLAG: stopping\n", info->team_id);
    fflush(stdout);
    return NULL;
}

static void initialize_team(TeamMonitor *team, int team_id)
{
    memset(team, 0, sizeof(*team));
    team->team_id = team_id;
    team->next_ticket = 1;
    team->ticket_to_enter = 1;

    {
        int code = pthread_mutex_init(&team->mutex, NULL);
        if (code != 0) {
            die_pthread(code, "pthread_mutex_init");
        }
    }
    {
        int code = pthread_cond_init(&team->driver_cv, NULL);
        if (code != 0) {
            die_pthread(code, "pthread_cond_init driver_cv");
        }
    }
    {
        int code = pthread_cond_init(&team->mechanic_cv, NULL);
        if (code != 0) {
            die_pthread(code, "pthread_cond_init mechanic_cv");
        }
    }
    {
        int code = pthread_cond_init(&team->flag_cv, NULL);
        if (code != 0) {
            die_pthread(code, "pthread_cond_init flag_cv");
        }
    }
}

static void destroy_team(TeamMonitor *team)
{
    int code;

    code = pthread_mutex_destroy(&team->mutex);
    if (code != 0) {
        die_pthread(code, "pthread_mutex_destroy");
    }
    code = pthread_cond_destroy(&team->driver_cv);
    if (code != 0) {
        die_pthread(code, "pthread_cond_destroy driver_cv");
    }
    code = pthread_cond_destroy(&team->mechanic_cv);
    if (code != 0) {
        die_pthread(code, "pthread_cond_destroy mechanic_cv");
    }
    code = pthread_cond_destroy(&team->flag_cv);
    if (code != 0) {
        die_pthread(code, "pthread_cond_destroy flag_cv");
    }
}

int main(void)
{
    pthread_t drivers[TEAM_COUNT][DRIVERS_PER_TEAM];
    pthread_t mechanics[TEAM_COUNT][MECHANICS_PER_TEAM];
    pthread_t flags[TEAM_COUNT];
    ThreadArgs driver_args[TEAM_COUNT][DRIVERS_PER_TEAM];
    ThreadArgs mechanic_args[TEAM_COUNT][MECHANICS_PER_TEAM];
    ThreadArgs flag_args[TEAM_COUNT];
    unsigned int base_seed = (unsigned int)time(NULL);
    int team;
    int member;
    int code;

    for (team = 0; team < TEAM_COUNT; team++) {
        initialize_team(&teams[team], team + 1);
    }

    for (team = 0; team < TEAM_COUNT; team++) {
        for (member = 0; member < DRIVERS_PER_TEAM; member++) {
            driver_args[team][member].team_id = team + 1;
            driver_args[team][member].member_id = member + 1;
            driver_args[team][member].seed = base_seed + (unsigned int)(team * 100 + member * 7 + 1);
            code = pthread_create(&drivers[team][member], NULL, driver_thread, &driver_args[team][member]);
            if (code != 0) {
                die_pthread(code, "pthread_create driver");
            }
        }

        for (member = 0; member < MECHANICS_PER_TEAM; member++) {
            mechanic_args[team][member].team_id = team + 1;
            mechanic_args[team][member].member_id = member + 1;
            mechanic_args[team][member].seed = base_seed + (unsigned int)(team * 100 + member * 11 + 50);
            code = pthread_create(&mechanics[team][member], NULL, mechanic_thread, &mechanic_args[team][member]);
            if (code != 0) {
                die_pthread(code, "pthread_create mechanic");
            }
        }

        flag_args[team].team_id = team + 1;
        flag_args[team].member_id = 1;
        flag_args[team].seed = base_seed + (unsigned int)(team * 100 + 90);
        code = pthread_create(&flags[team], NULL, flag_thread, &flag_args[team]);
        if (code != 0) {
            die_pthread(code, "pthread_create flag");
        }
    }

    printf("Race setup complete: 6 drivers, 8 mechanics, 2 flag persons\n");
    printf("Race started\n");
    fflush(stdout);

    lock_mutex(&race_mutex, "pthread_mutex_lock race_mutex");
    race_started = true;
    broadcast_cond(&race_start_cv, "pthread_cond_broadcast race_start_cv");
    unlock_mutex(&race_mutex, "pthread_mutex_unlock race_mutex");

    for (team = 0; team < TEAM_COUNT; team++) {
        for (member = 0; member < DRIVERS_PER_TEAM; member++) {
            code = pthread_join(drivers[team][member], NULL);
            if (code != 0) {
                die_pthread(code, "pthread_join driver");
            }
        }
    }

    for (team = 0; team < TEAM_COUNT; team++) {
        code = pthread_join(flags[team], NULL);
        if (code != 0) {
            die_pthread(code, "pthread_join flag");
        }

        for (member = 0; member < MECHANICS_PER_TEAM; member++) {
            code = pthread_join(mechanics[team][member], NULL);
            if (code != 0) {
                die_pthread(code, "pthread_join mechanic");
            }
        }
    }

    for (team = 0; team < TEAM_COUNT; team++) {
        destroy_team(&teams[team]);
    }

    printf("Race finished\n");
    return EXIT_SUCCESS;
}
