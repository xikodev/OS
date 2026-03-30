#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define INITIAL_HISTORY_CAPACITY 32
#define INITIAL_JOB_CAPACITY 16

typedef struct {
    char **items;
    size_t count;
    size_t capacity;
} History;

typedef struct {
    pid_t pid;
    pid_t pgid;
    char *command;
    bool active;
} Job;

typedef struct {
    Job *items;
    size_t count;
    size_t capacity;
} JobTable;

typedef struct {
    char **argv;
    int argc;
    bool background;
} ParsedCommand;

static History g_history = {0};
static JobTable g_jobs = {0};
static struct termios g_shell_termios;
static pid_t g_shell_pgid = -1;
static volatile sig_atomic_t g_foreground_pgid = 0;
static int g_interactive = 0;

static void fatal(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}

static void *xmalloc(size_t size)
{
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fatal("Out of memory");
    }
    return ptr;
}

static void *xrealloc(void *ptr, size_t size)
{
    void *new_ptr = realloc(ptr, size);
    if (new_ptr == NULL) {
        fatal("Out of memory");
    }
    return new_ptr;
}

static char *xstrdup(const char *s)
{
    char *copy = strdup(s);
    if (copy == NULL) {
        fatal("Out of memory");
    }
    return copy;
}

static void init_history(void)
{
    g_history.capacity = INITIAL_HISTORY_CAPACITY;
    g_history.items = xmalloc(g_history.capacity * sizeof(*g_history.items));
}

static void history_append(const char *line)
{
    if (g_history.count == g_history.capacity) {
        g_history.capacity *= 2;
        g_history.items = xrealloc(g_history.items, g_history.capacity * sizeof(*g_history.items));
    }
    g_history.items[g_history.count++] = xstrdup(line);
}

static const char *history_get(size_t index)
{
    if (index == 0 || index > g_history.count) {
        return NULL;
    }
    return g_history.items[index - 1];
}

static void print_history(void)
{
    size_t i;

    for (i = 0; i < g_history.count; i++) {
        printf("%5zu %s\n", i + 1, g_history.items[i]);
    }
}

static void free_history(void)
{
    size_t i;

    for (i = 0; i < g_history.count; i++) {
        free(g_history.items[i]);
    }
    free(g_history.items);
}

static void init_jobs(void)
{
    g_jobs.capacity = INITIAL_JOB_CAPACITY;
    g_jobs.items = xmalloc(g_jobs.capacity * sizeof(*g_jobs.items));
}

static void ensure_job_capacity(void)
{
    if (g_jobs.count == g_jobs.capacity) {
        g_jobs.capacity *= 2;
        g_jobs.items = xrealloc(g_jobs.items, g_jobs.capacity * sizeof(*g_jobs.items));
    }
}

static Job *add_job(pid_t pid, pid_t pgid, const char *command)
{
    Job *job;

    ensure_job_capacity();
    job = &g_jobs.items[g_jobs.count++];
    job->pid = pid;
    job->pgid = pgid;
    job->command = xstrdup(command);
    job->active = true;
    return job;
}

static Job *find_job(pid_t pid)
{
    size_t i;

    for (i = 0; i < g_jobs.count; i++) {
        if (g_jobs.items[i].active && g_jobs.items[i].pid == pid) {
            return &g_jobs.items[i];
        }
    }
    return NULL;
}

static void mark_job_finished(pid_t pid)
{
    Job *job = find_job(pid);
    if (job != NULL) {
        job->active = false;
    }
}

static void print_jobs(void)
{
    size_t i;
    bool printed = false;

    printf("%-8s %s\n", "PID", "command");
    for (i = 0; i < g_jobs.count; i++) {
        if (!g_jobs.items[i].active) {
            continue;
        }
        printf("%-8ld %s\n", (long)g_jobs.items[i].pid, g_jobs.items[i].command);
        printed = true;
    }

    if (!printed) {
        printf("(no running processes)\n");
    }
}

static void free_jobs(void)
{
    size_t i;

    for (i = 0; i < g_jobs.count; i++) {
        free(g_jobs.items[i].command);
    }
    free(g_jobs.items);
}

static void reap_background_jobs(void)
{
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        mark_job_finished(pid);
    }
}

static void kill_all_jobs(int sig)
{
    size_t i;

    for (i = 0; i < g_jobs.count; i++) {
        if (g_jobs.items[i].active) {
            kill(-g_jobs.items[i].pgid, sig);
        }
    }

    while (waitpid(-1, NULL, 0) > 0) {
    }
}

static void sigint_handler(int sig)
{
    pid_t foreground_pgid;

    (void)sig;
    foreground_pgid = (pid_t)g_foreground_pgid;
    if (foreground_pgid > 0) {
        kill(-foreground_pgid, SIGINT);
    }
}

static void install_signal_handlers(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sigint_handler;
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        fatal("sigaction(SIGINT) failed: %s", strerror(errno));
    }

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
}

static void setup_shell(void)
{
    g_interactive = isatty(STDIN_FILENO);
    if (!g_interactive) {
        return;
    }

    while (tcgetpgrp(STDIN_FILENO) != (g_shell_pgid = getpgrp())) {
        kill(-g_shell_pgid, SIGTTIN);
    }

    g_shell_pgid = getpid();
    if (setpgid(g_shell_pgid, g_shell_pgid) == -1 && errno != EACCES) {
        fatal("setpgid(shell) failed: %s", strerror(errno));
    }

    if (tcsetpgrp(STDIN_FILENO, g_shell_pgid) == -1) {
        fatal("tcsetpgrp(shell) failed: %s", strerror(errno));
    }

    if (tcgetattr(STDIN_FILENO, &g_shell_termios) == -1) {
        fatal("tcgetattr failed: %s", strerror(errno));
    }
}

static char *trim_whitespace(char *s)
{
    char *end;

    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }

    if (*s == '\0') {
        return s;
    }

    end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
    return s;
}

static void free_parsed_command(ParsedCommand *cmd)
{
    int i;

    for (i = 0; i < cmd->argc; i++) {
        free(cmd->argv[i]);
    }
    free(cmd->argv);
    cmd->argv = NULL;
    cmd->argc = 0;
    cmd->background = false;
}

static void append_arg(ParsedCommand *cmd, char *arg, int *capacity)
{
    if (cmd->argc + 1 >= *capacity) {
        *capacity *= 2;
        cmd->argv = xrealloc(cmd->argv, (size_t)(*capacity) * sizeof(*cmd->argv));
    }
    cmd->argv[cmd->argc++] = arg;
    cmd->argv[cmd->argc] = NULL;
}

static int parse_command(const char *line, ParsedCommand *cmd)
{
    int capacity = 8;
    size_t i = 0;
    size_t len = strlen(line);

    memset(cmd, 0, sizeof(*cmd));
    cmd->argv = xmalloc((size_t)capacity * sizeof(*cmd->argv));
    cmd->argv[0] = NULL;

    while (i < len) {
        char *token;
        size_t token_capacity = len + 1;
        size_t token_length = 0;
        bool in_single = false;
        bool in_double = false;

        while (i < len && isspace((unsigned char)line[i])) {
            i++;
        }
        if (i >= len) {
            break;
        }

        token = xmalloc(token_capacity);

        while (i < len) {
            char c = line[i];

            if (!in_single && !in_double && isspace((unsigned char)c)) {
                break;
            }
            if (!in_double && c == '\'') {
                in_single = !in_single;
                i++;
                continue;
            }
            if (!in_single && c == '"') {
                in_double = !in_double;
                i++;
                continue;
            }
            if (c == '\\' && i + 1 < len) {
                i++;
                c = line[i];
            }

            token[token_length++] = c;
            i++;
        }

        if (in_single || in_double) {
            free(token);
            free_parsed_command(cmd);
            fprintf(stderr, "Unterminated quote in command\n");
            return -1;
        }

        token[token_length] = '\0';
        append_arg(cmd, token, &capacity);
    }

    if (cmd->argc > 0 && strcmp(cmd->argv[cmd->argc - 1], "&") == 0) {
        free(cmd->argv[cmd->argc - 1]);
        cmd->argv[--cmd->argc] = NULL;
        cmd->background = true;
    }

    return 0;
}

static bool try_expand_history(const char *input, char **expanded)
{
    long requested_index;
    char *endptr;
    const char *replayed;

    *expanded = NULL;
    if (input[0] != '!') {
        return false;
    }

    errno = 0;
    requested_index = strtol(input + 1, &endptr, 10);
    if (errno != 0 || *endptr != '\0' || requested_index <= 0) {
        fprintf(stderr, "Invalid history reference: %s\n", input);
        *expanded = xstrdup("");
        return true;
    }

    replayed = history_get((size_t)requested_index);
    if (replayed == NULL) {
        fprintf(stderr, "History entry %ld does not exist\n", requested_index);
        *expanded = xstrdup("");
        return true;
    }

    printf("%s\n", replayed);
    *expanded = xstrdup(replayed);
    return true;
}

static int builtin_cd(ParsedCommand *cmd)
{
    const char *path;

    if (cmd->argc > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    path = (cmd->argc == 1) ? getenv("HOME") : cmd->argv[1];
    if (path == NULL) {
        fprintf(stderr, "cd: HOME is not set\n");
        return 1;
    }

    if (chdir(path) == -1) {
        fprintf(stderr, "cd: %s: %s\n", path, strerror(errno));
        return 1;
    }

    return 0;
}

static int builtin_kill(ParsedCommand *cmd)
{
    long pid_value;
    long sig_value;
    char *endptr;
    Job *job;

    if (cmd->argc != 3) {
        fprintf(stderr, "Usage: kill <pid> <signal>\n");
        return 1;
    }

    errno = 0;
    pid_value = strtol(cmd->argv[1], &endptr, 10);
    if (errno != 0 || *endptr != '\0' || pid_value <= 0) {
        fprintf(stderr, "kill: invalid pid '%s'\n", cmd->argv[1]);
        return 1;
    }

    errno = 0;
    sig_value = strtol(cmd->argv[2], &endptr, 10);
    if (errno != 0 || *endptr != '\0' || sig_value <= 0) {
        fprintf(stderr, "kill: invalid signal '%s'\n", cmd->argv[2]);
        return 1;
    }

    job = find_job((pid_t)pid_value);
    if (job == NULL) {
        fprintf(stderr, "kill: pid %ld is not managed by this shell\n", pid_value);
        return 1;
    }

    if (kill((pid_t)pid_value, (int)sig_value) == -1) {
        fprintf(stderr, "kill: %s\n", strerror(errno));
        return 1;
    }

    return 0;
}

static int execute_builtin(ParsedCommand *cmd, bool *should_exit)
{
    if (strcmp(cmd->argv[0], "cd") == 0) {
        return builtin_cd(cmd);
    }
    if (strcmp(cmd->argv[0], "exit") == 0) {
        *should_exit = true;
        return 0;
    }
    if (strcmp(cmd->argv[0], "ps") == 0) {
        print_jobs();
        return 0;
    }
    if (strcmp(cmd->argv[0], "kill") == 0) {
        return builtin_kill(cmd);
    }
    if (strcmp(cmd->argv[0], "history") == 0) {
        print_history();
        return 0;
    }
    return -1;
}

static void restore_shell_terminal(void)
{
    if (!g_interactive) {
        return;
    }

    tcsetpgrp(STDIN_FILENO, g_shell_pgid);
    tcsetattr(STDIN_FILENO, TCSADRAIN, &g_shell_termios);
}

static int launch_program(ParsedCommand *cmd, const char *original_line)
{
    pid_t pid;
    int status = 0;

    pid = fork();
    if (pid == -1) {
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        return 1;
    }

    if (pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        if (setpgid(0, 0) == -1) {
            perror("setpgid");
            _exit(EXIT_FAILURE);
        }

        if (g_interactive && !cmd->background) {
            tcsetpgrp(STDIN_FILENO, getpid());
        }

        execvp(cmd->argv[0], cmd->argv);
        perror(cmd->argv[0]);
        _exit(127);
    }

    if (setpgid(pid, pid) == -1 && errno != EACCES) {
        fprintf(stderr, "setpgid failed: %s\n", strerror(errno));
    }

    add_job(pid, pid, original_line);

    if (cmd->background) {
        printf("[background pid %ld]\n", (long)pid);
        return 0;
    }

    g_foreground_pgid = pid;
    if (g_interactive) {
        tcsetpgrp(STDIN_FILENO, pid);
    }

    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
            break;
        }
    }

    g_foreground_pgid = 0;
    mark_job_finished(pid);
    restore_shell_terminal();
    return 0;
}

int main(void)
{
    char *line = NULL;
    size_t line_capacity = 0;

    init_history();
    init_jobs();
    setup_shell();
    install_signal_handlers();

    for (;;) {
        ParsedCommand cmd = {0};
        char *trimmed;
        char *expanded = NULL;
        char *active_line;
        bool should_exit = false;

        reap_background_jobs();

        if (g_interactive) {
            printf("osh$ ");
            fflush(stdout);
        }

        if (getline(&line, &line_capacity, stdin) == -1) {
            if (feof(stdin)) {
                break;
            }
            if (errno == EINTR) {
                clearerr(stdin);
                continue;
            }
            fprintf(stderr, "getline failed: %s\n", strerror(errno));
            break;
        }

        line[strcspn(line, "\n")] = '\0';
        trimmed = trim_whitespace(line);
        if (*trimmed == '\0') {
            continue;
        }

        history_append(trimmed);

        if (try_expand_history(trimmed, &expanded)) {
            if (expanded[0] == '\0') {
                free(expanded);
                continue;
            }
            active_line = expanded;
        } else {
            active_line = trimmed;
        }

        if (parse_command(active_line, &cmd) == -1) {
            free(expanded);
            continue;
        }

        if (cmd.argc == 0) {
            free(expanded);
            free_parsed_command(&cmd);
            continue;
        }

        if (execute_builtin(&cmd, &should_exit) == -1) {
            launch_program(&cmd, active_line);
        }

        free(expanded);
        free_parsed_command(&cmd);

        if (should_exit) {
            break;
        }
    }

    free(line);
    kill_all_jobs(SIGKILL);
    free_jobs();
    free_history();
    return 0;
}
