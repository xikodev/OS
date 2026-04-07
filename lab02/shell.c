#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define MAX_LINE 1024
#define MAX_ARGS 64
#define MAX_HISTORY 100
#define MAX_PROCESSES 64

typedef struct {
    pid_t pid;
    char command[MAX_LINE];
    int active;
} ProcessInfo;

static ProcessInfo processes[MAX_PROCESSES];
static int process_count = 0;

static char history_list[MAX_HISTORY][MAX_LINE];
static int history_count = 0;

static pid_t foreground_pid = 0;
static pid_t shell_pgid = 0;
static int interactive_shell = 0;
static struct termios terminal_settings;

static void trim_spaces(char *text)
{
    int len;
    int i = 0;
    int j = 0;

    while (text[i] != '\0' && isspace((unsigned char)text[i])) {
        i++;
    }

    if (i > 0) {
        while (text[i] != '\0') {
            text[j] = text[i];
            j++;
            i++;
        }
        text[j] = '\0';
    }

    len = (int)strlen(text);
    while (len > 0 && isspace((unsigned char)text[len - 1])) {
        text[len - 1] = '\0';
        len--;
    }
}

static void add_to_history(const char *line)
{
    int i;

    if (history_count < MAX_HISTORY) {
        strncpy(history_list[history_count], line, MAX_LINE - 1);
        history_list[history_count][MAX_LINE - 1] = '\0';
        history_count++;
        return;
    }

    for (i = 1; i < MAX_HISTORY; i++) {
        strcpy(history_list[i - 1], history_list[i]);
    }

    strncpy(history_list[MAX_HISTORY - 1], line, MAX_LINE - 1);
    history_list[MAX_HISTORY - 1][MAX_LINE - 1] = '\0';
}

static void print_history(void)
{
    int i;

    for (i = 0; i < history_count; i++) {
        printf("%5d %s\n", i + 1, history_list[i]);
    }
}

static void add_process(pid_t pid, const char *command)
{
    if (process_count >= MAX_PROCESSES) {
        fprintf(stderr, "Too many proceses in table\n");
        return;
    }

    processes[process_count].pid = pid;
    processes[process_count].active = 1;
    strncpy(processes[process_count].command, command, MAX_LINE - 1);
    processes[process_count].command[MAX_LINE - 1] = '\0';
    process_count++;
}

static void mark_finished(pid_t pid)
{
    int i;

    for (i = 0; i < process_count; i++) {
        if (processes[i].pid == pid && processes[i].active) {
            processes[i].active = 0;
            return;
        }
    }
}

static int process_exists(pid_t pid)
{
    int i;

    for (i = 0; i < process_count; i++) {
        if (processes[i].pid == pid && processes[i].active) {
            return 1;
        }
    }

    return 0;
}

static void check_background_processes(void)
{
    pid_t finished;

    while ((finished = waitpid(-1, NULL, WNOHANG)) > 0) {
        mark_finished(finished);
    }
}

static void print_processes(void)
{
    int i;
    int found = 0;

    printf("%-8s %s\n", "PID", "command");
    for (i = 0; i < process_count; i++) {
        if (processes[i].active) {
            printf("%-8ld %s\n", (long)processes[i].pid, processes[i].command);
            found = 1;
        }
    }

    if (!found) {
        printf("(no running processes)\n");
    }
}

static void stop_all_processes(void)
{
    int i;

    for (i = 0; i < process_count; i++) {
        if (processes[i].active) {
            kill(processes[i].pid, SIGKILL);
        }
    }

    while (waitpid(-1, NULL, 0) > 0) {
    }
}

static void handle_sigint(int sig)
{
    (void)sig;

    if (foreground_pid > 0) {
        kill(foreground_pid, SIGINT);
    }
}

static void setup_signals(void)
{
    struct sigaction act;

    memset(&act, 0, sizeof(act));
    sigemptyset(&act.sa_mask);
    act.sa_handler = handle_sigint;
    act.sa_flags = SA_RESTART;
    sigaction(SIGINT, &act, NULL);

    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
}

static void setup_shell(void)
{
    interactive_shell = isatty(STDIN_FILENO);
    if (!interactive_shell) {
        return;
    }

    while (tcgetpgrp(STDIN_FILENO) != getpgrp()) {
        kill(-getpgrp(), SIGTTIN);
    }

    shell_pgid = getpid();
    if (setpgid(shell_pgid, shell_pgid) == -1 && errno != EACCES) {
        perror("setpgid");
        exit(1);
    }

    if (tcsetpgrp(STDIN_FILENO, shell_pgid) == -1) {
        perror("tcsetpgrp");
        exit(1);
    }

    if (tcgetattr(STDIN_FILENO, &terminal_settings) == -1) {
        perror("tcgetattr");
        exit(1);
    }
}

static int parse_command(char *line, char *argv[])
{
    int argc = 0;
    int i = 0;

    while (line[i] != '\0') {
        while (line[i] != '\0' && isspace((unsigned char)line[i])) {
            i++;
        }

        if (line[i] == '\0') {
            break;
        }

        if (argc >= MAX_ARGS - 1) {
            fprintf(stderr, "Too many arguments\n");
            return -1;
        }

        argv[argc] = &line[i];

        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            int start = i;
            int j = 0;

            i++;
            while (line[i] != '\0' && line[i] != quote) {
                line[start + j] = line[i];
                i++;
                j = j + 1;
            }

            if (line[i] != quote) {
                fprintf(stderr, "Quote error\n");
                return -1;
            }

            line[start + j] = '\0';
            argv[argc] = &line[start];
            i++;
            argc++;
            continue;
        }

        while (line[i] != '\0' && !isspace((unsigned char)line[i])) {
            i++;
        }

        if (line[i] != '\0') {
            line[i] = '\0';
            i++;
        }

        argc++;
    }

    argv[argc] = NULL;
    return argc;
}

static int is_background_command(char *argv[], int *argc)
{
    if (*argc > 0 && strcmp(argv[*argc - 1], "&") == 0) {
        argv[*argc - 1] = NULL;
        *argc = *argc - 1;
        return 1;
    }

    return 0;
}

static int builtin_cd(char *argv[], int argc)
{
    char *path;

    if (argc > 2) {
        fprintf(stderr, "cd: too many arguments\n");
        return 1;
    }

    if (argc == 1) {
        path = getenv("HOME");
        if (path == NULL) {
            fprintf(stderr, "cd: HOME is not set\n");
            return 1;
        }
    } else {
        path = argv[1];
    }

    if (chdir(path) == -1) {
        fprintf(stderr, "cd: %s: %s\n", path, strerror(errno));
        return 1;
    }

    return 0;
}

static int builtin_kill(char *argv[], int argc)
{
    long pid;
    long sig;
    char *endptr;

    if (argc != 3) {
        fprintf(stderr, "Usge: kill <pid> <signal>\n");
        return 1;
    }

    pid = strtol(argv[1], &endptr, 10);
    if (*endptr != '\0' || pid <= 0) {
        fprintf(stderr, "kill: invalid pid '%s'\n", argv[1]);
        return 1;
    }

    sig = strtol(argv[2], &endptr, 10);
    if (*endptr != '\0' || sig <= 0) {
        fprintf(stderr, "kill: invalid signal '%s'\n", argv[2]);
        return 1;
    }

    if (!process_exists((pid_t)pid)) {
        fprintf(stderr, "kill: pid %ld is not managed by this shell\n", pid);
        return 1;
    }

    if (kill((pid_t)pid, (int)sig) == -1) {
        fprintf(stderr, "kill: %s\n", strerror(errno));
        return 1;
    }

    return 0;
}

static int launch_program(char *argv[], int background, const char *original)
{
    pid_t child_pid;

    child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        return 1;
    }

    if (child_pid == 0) {
        signal(SIGINT, SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);

        setpgid(0, 0);

        if (interactive_shell && !background) {
            tcsetpgrp(STDIN_FILENO, getpid());
        }

        execvp(argv[0], argv);
        perror(argv[0]);
        _exit(1);
    }

    setpgid(child_pid, child_pid);
    add_process(child_pid, original);

    if (background) {
        printf("[background pid %ld]\n", (long)child_pid);
        return 0;
    }

    foreground_pid = child_pid;

    if (interactive_shell) {
        tcsetpgrp(STDIN_FILENO, child_pid);
    }

    while (waitpid(child_pid, NULL, 0) == -1) {
        if (errno != EINTR) {
            perror("waitpid");
            break;
        }
    }

    foreground_pid = 0;
    mark_finished(child_pid);

    if (interactive_shell) {
        tcsetpgrp(STDIN_FILENO, shell_pgid);
        tcsetattr(STDIN_FILENO, TCSADRAIN, &terminal_settings);
    }

    return 0;
}

int main(void)
{
    char line[MAX_LINE];

    setup_shell();
    setup_signals();

    while (1) {
        char working_line[MAX_LINE];
        char expanded_line[MAX_LINE];
        char *argv[MAX_ARGS];
        char *active_line;
        int argc;
        int background;
        int history_number;
        char *endptr;

        check_background_processes();

        if (interactive_shell) {
            printf("osh$ ");
            fflush(stdout);
        }

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        if (strchr(line, '\n') == NULL) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {
            }
        } else {
            line[strcspn(line, "\n")] = '\0';
        }

        trim_spaces(line);
        if (line[0] == '\0') {
            continue;
        }

        add_to_history(line);

        if (line[0] == '!') {
            history_number = (int)strtol(line + 1, &endptr, 10);
            if (*endptr != '\0' || history_number <= 0 || history_number > history_count) {
                fprintf(stderr, "Invald history reference: %s\n", line);
                continue;
            }

            strcpy(expanded_line, history_list[history_number - 1]);
            printf("%s\n", expanded_line);
            active_line = expanded_line;
        } else {
            active_line = line;
        }

        strncpy(working_line, active_line, MAX_LINE - 1);
        working_line[MAX_LINE - 1] = '\0';

        argc = parse_command(working_line, argv);
        if (argc <= 0) {
            continue;
        }

        background = is_background_command(argv, &argc);
        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "exit") == 0) {
            break;
        } else if (strcmp(argv[0], "cd") == 0) {
            builtin_cd(argv, argc);
        } else if (strcmp(argv[0], "ps") == 0) {
            print_processes();
        } else if (strcmp(argv[0], "kill") == 0) {
            builtin_kill(argv, argc);
        } else if (strcmp(argv[0], "history") == 0) {
            print_history();
        } else {
            launch_program(argv, background, active_line);
        }
    }

    stop_all_processes();
    return 0;
}
