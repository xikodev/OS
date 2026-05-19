# Operating Systems

Faculty of Electrical Engineering and Computing

## Lab 1

### Problem

In an arbitrarily chosen programming language, simulate:

- an interrupt system with software support for priority interrupting (students whose JMBAG ends with digits `0`, `1`,
  `2`, `3`, or `4`)
- an interrupt system with hardware support (students whose JMBAG ends with digits `5`, `6`, `7`, `8`, or `9`)

Use four interrupt levels (four priorities), where all interrupts are simulated by a single signal (for example,
`SIGINT`), after which the interrupt priority is entered interactively.

The simulator should print the state of the system every time a change occurs, including:

- whether the main program or an interrupt subroutine is being executed
- the values of all relevant data structures

### Detailed Description

The signaling mechanism at the operating system level enables processing of events that occur in parallel with the
normal operation of the program, that is, the process and its threads.

In that sense, a signal is similar to the processor-level interrupt mechanism:

- the processor executes a thread
- a device interrupt occurs
- the processor suspends execution of the thread
- the current context is saved
- execution jumps to the interrupt handler
- after the handler completes, the original context is restored and the thread continues

Signals work similarly. They interrupt thread execution, invoke a signal-processing function (either the default one or
a custom handler), and after completion the processor returns to the interrupted thread.

Consider the signals `SIGINT` (signal interrupt) and `SIGTERM` (terminate):

- `SIGINT` is commonly used to stop a process, often as a forced interruption caused by an error or manual cancellation.
- `SIGTERM` is also used to stop a process, but usually in a controlled way. For example, when the system is shutting
  down, processes are notified with this signal so they can perform cleanup before terminating voluntarily.

In the terminal, pressing `Ctrl+C` sends `SIGINT` to the active process, which by default terminates it. A signal can
also be sent through the OS interface, for example with the `kill` command:

```sh
kill -<signal id> <PID>
```

Example:

```sh
kill -SIGTERM 2351
```

The `$` prompt character often shown in shell examples is not part of the command.

For most signals, a program can define its own behavior. If it does not, the default behavior is used. In many cases
that means stopping the process, as with `SIGINT` and `SIGTERM`.

The program defines its behavior for signals through the OS interface by masking a signal and associating it with a
handler function. Several interfaces exist for this, such as the older `signal` and `sigset`, and the newer `sigaction`,
which is used in this lab.

In the next example, three signals are masked: `SIGUSR1`, `SIGTERM`, and `SIGINT`.

- `SIGUSR1` is a user-defined signal with no special predefined purpose. Here it is used to simulate an event that
  requires some action.
- The handlers for `SIGTERM` and `SIGINT` print a message and stop the process.
- In the `SIGTERM` handler, the program only announces that execution should stop by modifying a global variable `run`.
- When control returns from that handler, the process notices the updated state and exits its infinite loop.

Example programs:

- [example_signals.c](lab01/example_signals.c)
- [example_sleep.c](lab01/example_sleep.c)

Problem solution:

- [signals.c](lab01/signals.c)

---

## Lab 2

### Problem

In a UNIX/Linux-like environment, using the C or C++ programming language, implement the following basic shell
functionalities:

- program execution in the foreground
- multiple programs executing in the background
- shell command history using the prompt command `history`

While programs are running in the background, the shell should allow:

- printing currently running processes
- terminating a process using `SIGKILL` or `SIGTERM`

It is allowed to use all functions provided by the standard C library (for example `glibc` or `musl`), except `system`.

### Detailed Description

The shell is a program that provides a text interface for running other programs. Commands are entered after the prompt
text printed by the shell.

The input format is:

```txt
<program_name> [argument1 ...]
```

For example, the command to copy one file into another is:

```sh
~/example1/$ cp example.c example-copy.c
```

Here:

- `~/example1/$` is the prompt, usually showing the active directory
- `cp` is the command
- `example.c` and `example-copy.c` are the arguments

Within this exercise, the shell must support:

- built-in commands: `cd`, `exit`, `ps`, `kill`, and `history`
- launching other programs

### Built-in Commands

The `cd` command changes the active directory. The initial directory is the one in which the shell starts. This is most
simply done with `chdir()`. It is not necessary to separately remember the current directory because it can always be
obtained with `getcwd()`.

The `exit` command ends the shell. Before exiting, all running programs should be stopped, for example by sending
`SIGKILL (9)`.

The `ps` command should print all running processes that have not yet completed. For example:

```txt
PID name
72146 prog
72138 something
72119 test
```

The `kill` command should allow sending a signal to one of the running processes listed by `ps`. For example:

```txt
kill 72138 2
```

This command sends signal number `2` (`SIGINT`) to the process with PID `72138`.

Make sure to verify that the specified process was started by this shell. The shell must not allow sending signals to
unrelated processes.

The `history` command should print all previously entered shell commands, including a history number. That number can
later be used to rerun the same command in the format `!<ordinal-number>`.

Example:

```sh
student:~$ pwd
/home/student
student:~$ date
Fri Feb 14 13:09:24 CET 2025
student:~$ echo "Hello World!"
Hello World!
student:~$ history
    1 pwd
    2 date
    3 echo "Hello World!"
    4 history
student:~$ !3
echo "Hello World!"
Hello World!
student:~$
```

### Starting a Program

Programs should be able to run in both the foreground and the background.

If the last argument is `&`, the program starts in the background. Otherwise, it runs in the foreground and the shell
must wait for it to finish.

Examples:

```sh
./prog 1 2 3
```

This launches the program in the foreground.

```sh
./something a b c &
```

This launches the program in the background.

The shell must keep records of all started processes so they can later be listed with `ps` and signaled with `kill`.

The intended logic can be summarized with the following pseudocode:

```txt
repeat {
    read a new user request - a line of text from standard input
    parse request (command and arguments)

    if the command is one of the built-in commands then
        perform this built-in command
    otherwise
        // assume it is a program name
        create a new process and in it:
            separate the process into a separate group*
            run the program with one of the exec functions

        // continuation of the parent process, the shell
        add process to the list of running processes
        if the startup is carried out in the foreground then
            wait for the completion of the process
}
while command is not exit

// the command is exit
send SIGKILL to all remaining processes
finish work
```

Separating the new process into a new process group is necessary for signal management and input control. If a signal is
sent to the shell, for example with `Ctrl+C`, then without separation the shell and all processes created by it would
receive `SIGINT`. Creating a new process group prevents that. One of the functions that can be used for this is
`setpgid()`.

However, once the process is moved into a separate group, it will no longer receive signals typed by the user at the
keyboard, such as `Ctrl+C`. Therefore, when the shell launches a foreground program, it should detect signals sent to
the shell itself (monitoring `SIGINT` is sufficient) and forward them to the foreground process.

Among the OS interfaces needed to implement the shell, the most important are `fork`, `exec*`, `kill`, and `waitpid`.

More detailed information about a particular interface can be obtained with:

```sh
man <interface-name>
```

If the result is not what you want, for example `man kill` describing the command instead of the function, specify the
section number:

```sh
man 2 kill
```

If that still does not help, try another section such as:

```sh
man 3 printf
```

### Running Interactive Programs

All running programs can print messages to the terminal. But when the user types input, which process should receive it?
Initially, input belongs to the shell because the user is giving commands to it.

To precisely control who receives terminal input, separate process groups are required. A specific group is granted
access to terminal input. Since each group here contains only one process, that process becomes the terminal owner.

Initially, terminal input is assigned to the group containing the shell. When the shell creates a new process, that
process should request separation into a new group with `setpgid()`. If terminal input should then be transferred to
that process group, the shell can do it with:

```c
tcsetpgrp(STDIN_FILENO, getpgid(pid_of_new_process));
```

This can also be done by the child process itself immediately after `fork()` and before `exec*()`.

After the foreground process completes, control should be returned to the shell. The shell can reclaim terminal input
with:

```c
tcsetpgrp(STDIN_FILENO, getpgid(0));
```

Because the shell does not own terminal input at that moment, the system may send it `SIGTTOU`, which according to the
default behavior temporarily stops the process. Since that is not desirable, the shell can simply ignore this signal.

The foreground process may also change terminal settings, but those settings should not remain changed after the process
exits. Therefore, the shell can save and later restore them using:

```c
tcgetattr(STDIN_FILENO, &shell_term_settings);
```

and:

```c
tcsetattr(STDIN_FILENO, 0, &shell_term_settings);
```

Example programs:

- [lab2-sucelja.c](lab02/lab2-sucelja.c)

Problem solution:

- [shell.c](lab02/shell.c)

---

## Lab 3

### Problem (semaphores)

Producer–Consumer Problem

Task

Implement communication between producer and consumer processes using a bounded buffer of limited length. Based on the
number of command-line parameters, the initial process creates the corresponding number of producer processes and one
consumer. Each producer takes one string provided via the command line and sends it to the consumer through the buffer
character by character (see the example execution at the bottom of the page). The consumer receives characters one by
one, and when it has received all characters (including the string terminator character `'\0'` sent by producers), it
prints all received characters and terminates.

Suggested Solution Structure

Data shared by producers and the consumer (place them in a shared memory segment that must be created separately):

- integer variables IN and OUT
- buffer M, an array of 5 slots
- semaphores WRITE, MESSAGES, SLOTS

Structure of the producer process

```
producer process
    read a string from the keyboard into array s[]
    i = 0
    do
        wait_SLOTS
        wait_WRITE
        M[IN] = s[i]
        IN = (IN + 1) mod 5
        signal_WRITE
        signal_MESSAGES
        i = i + 1
    until end of string
end.
```

Structure of the consumer process

```
consumer process
    i = 0
    do
        wait_MESSAGES
        s[i] = M[OUT]
        OUT = (OUT + 1) mod 5
        signal_SLOTS
        i = i + 1
    until the end of all strings
    print the string from array s[] to the screen
end.
```

Notes

The end-of-string marker must also be transmitted using the buffer so the consumer can know when a string ends. The
consumer should print received characters and terminate only after it has received all end-of-string markers. “Slow
down” the producer process by calling sleep(1) once inside the loop.

Example output and program execution

(Two strings are provided: 12345678 and abcdef, so two producer processes are created.)

```
# ./a.out 12345678 abcdef
PRODUCER1 -> 1
PRODUCER2 -> a
CONSUMER <- 1
PRODUCER1 -> 2
CONSUMER <- a
PRODUCER2 -> b
CONSUMER <- 2
PRODUCER1 -> 3
CONSUMER <- b
PRODUCER2 -> c
CONSUMER <- 3
PRODUCER1 -> 4
CONSUMER <- c
PRODUCER1 -> 5
CONSUMER <- 4
PRODUCER2 -> d
CONSUMER <- 5
PRODUCER1 -> 6
CONSUMER <- d
PRODUCER2 -> e
CONSUMER <- 6
PRODUCER2 -> f
CONSUMER <- e
PRODUCER1 -> 7
CONSUMER <- f
PRODUCER2 ->
CONSUMER <- 7
PRODUCER1 -> 8
CONSUMER <-
PRODUCER1 ->
CONSUMER <- 8
CONSUMER <-

Received: 1a2b3c45d6ef78
#
```

Problem solution:

- [semaphores.c](lab03/semaphores.c)

### Problem (monitors)

Race

In a car race, two teams participate, each with 3 drivers. Each team has its own pit, 4 mechanics, and a flag person (
lollipop man). Each driver enters their team’s pit twice during the race to change tires. If the pit is occupied, the
driver waits in front of the pit. After the driver stops at their position in the pit, the flag person lowers the flag
and the four mechanics begin changing the wheels (of course, mechanics in both pits may be changing tires
simultaneously). When a mechanic finishes their task, they notify the flag person. After all four mechanics have changed
their respective wheels, the flag person raises the flag and releases the driver back into the race. It is assumed that
the mechanics and the flag person must be ready to change tires throughout the entire race.

Task

Simulate a race of two teams, each with: 3 driver threads, 4 mechanic threads, 1 flag person thread. All of the above
threads are created by the main program thread. Synchronize threads using monitors.

Anything not explicitly specified in the task may be solved in an arbitrary way.

```
main thread(){
    create 6 driver threads, 8 mechanic threads, and 2 lollipop man threads;
    mark the start of the race;
}

driver thread(){
    wait for race start;
    drive x milliseconds, where x is a random number between 2000 and 5000 ms;
    go to pit to change tires;
    drive (7000 - x) ms;
    go to pit to change tires;
    drive y milliseconds, where y is a random number between 2000 and 3000 ms;
    print finished race;
}

mechanic thread(){
    while the race is ongoing{
        wait until the flag person gives the signal that tires can be changed;
        change_wheel; // simulate by waiting z milliseconds, where z is random between 2000 and 4000 ms
        notify the lollipop man that the wheel has been changed;
    }
}

flag_person thread(){ // lollipop man
    while the race is ongoing{
        signal with the flag that the driver may enter to change tires;
        wait until the driver stops;
        lower the flag;
        wait until the mechanics change the wheels;
        raise the flag;
    }
}
```

"Sleeping" for a given number of milliseconds can be achieved using the function usleep(ms*1000), which delays program
execution for the specified number of microseconds.

Problem solution:

- [monitors.c](lab03/monitors.c)

---

## Lab 4

### Problem

In an arbitrarily chosen programming language simulate on-demand paging for a computer with a
random number of available frames and a random program size using the clock algorithm for page
replacement. After each request for RAM access, print the contents of all algorithm data
structures.

Output example:
```
Frame print format: "frames: frame-num:[process-num:page] ..."
Page table format: "pt[process]: page:Fframe | D | NA ..."
Clock algorithm data format: "clock: frame0-A - frame1-A ..."
In the following example offset is 8-bit wide (last 2 hex digit of address).

process 3 READ LA=0x0234
frames: 0:[2:0] 1:[1:2] 2:[1:3] 3:[3:0] 4:[2:3] 5:[3:1] 6:[3:2] 7:[1:0]
pt[1]: 0:F7 1:D 2:F1 3:F2 4:NA 5:NA 6:NA 7:NA
pt[2]: 0:F0 1:D 2:D 3:F4 4:NA 5:NA 6:NA 7:NA
pt[3]: 0:F3 1:F5 2:F6 3:NA 4:NA 5:NA 6:NA 7:NA
HIT: frame 6
paging: process 3, page=2 => frame=6, 0x0234 => 0x0634
read value at 0x0634 => 0x42       (0x42 must be written there before!)
clock: 0-0-0-1-0-0-1-1
hand:             ^

process 2 WRITE(0x73) LA=0x0321
frames: 0:[2:0] 1:[1:2] 2:[1:3] 3:[3:0] 4:[2:3] 5:[3:1] 6:[3:2] 7:[1:0]
pt[1]: 0:F7 1:D 2:F1 3:F2 4:NA 5:NA 6:NA 7:NA
pt[2]: 0:F0 1:D 2:D 3:F4 4:NA 5:NA 6:NA 7:NA
pt[3]: 0:F3 1:F5 2:F6 3:NA 4:NA 5:NA 6:NA 7:NA
HIT: frame 4
paging: process 2, page=3 => frame=4, 0x0321 => 0x0421
save (0x73) at 0x0421
clock: 0-0-0-1-1-0-1-1
hand:             ^

process 1 WRITE(0x37) LA=0x0101
frames: 0:[2:0] 1:[1:2] 2:[1:3] 3:[3:0] 4:[2:3] 5:[3:1] 6:[3:2] 7:[1:0]
pt[1]: 0:F7 1:D 2:F1 3:F2 4:NA 5:NA 6:NA 7:NA
pt[2]: 0:F0 1:D 2:D 3:F4 4:NA 5:NA 6:NA 7:NA
pt[3]: 0:F3 1:F5 2:F6 3:NA 4:NA 5:NA 6:NA 7:NA
MISS (page 1 not in memory)
clock before: 0-0-0-1-1-0-1-1
hand before:        ^
clock after:  0-0-0-0-0-0-1-1
hand after:             ^
use frame 5:
- save to disk:   process 3, page 1
- load from disk: process 1, page 1
frames: 0:[2:0] 1:[1:2] 2:[1:3] 3:[3:0] 4:[2:3] 5:[1:1] 6:[3:2] 7:[1:0]
pt[1]: 0:F7 1:F5 2:F1 3:F2 4:NA 5:NA 6:NA 7:NA
pt[2]: 0:F0 1:D 2:D 3:F4 4:NA 5:NA 6:NA 7:NA
pt[3]: 0:F3 1:D 2:F6 3:NA 4:NA 5:NA 6:NA 7:NA
restarting instruction:
process 1 WRITE(0x37) LA=0x0101
frames: 0:[2:0] 1:[1:2] 2:[1:3] 3:[3:0] 4:[2:3] 5:[1:1] 6:[3:2] 7:[1:0]
pt[1]: 0:F7 1:F5 2:F1 3:F2 4:NA 5:NA 6:NA 7:NA
pt[2]: 0:F0 1:D 2:D 3:F4 4:NA 5:NA 6:NA 7:NA
pt[3]: 0:F3 1:D 2:F6 3:NA 4:NA 5:NA 6:NA 7:NA
HIT: frame 5
paging: process 1, page=1 => frame=5, 0x0101 => 0x0501
save (0x37) at 0x0501
clock:  0-0-0-0-0-1-1-1
hand:             ^

process 1 WRITE(0x37) LA=0x0701
MEMORY FAULT: page 7 not allocated, terminating process 1
```

Problem solution:

- [paging.c](lab04/paging.c)
