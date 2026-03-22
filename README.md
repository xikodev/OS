# Operating Systems

Faculty of Electrical Engineering and Computing

---

## Lab 1

### Problem

In an arbitrarily chosen programming language, simulate:

- interrupt system with software support for priority interrupting (students whose JMBAG
  ends with digits 0, 1, 2, 3 or 4)
- interrupt system with hardware support (students whose JMBAG ends with the digits 5, 6,
  7, 8 or 9)

with four interrupt levels (four priorities), where all interrupts are simulated by a single signal
(e.g. SIGINT), after which the interrupt priority is entered interactively.
The simulator should print the state of the system (whether the main program or an interrupt
subroutine is being executed and the values of all relevant data structures) every time a change
occurs in the system.

### Detailed description

The signaling mechanism at the operating system level enables the processing of events that
occur in parallel with the normal operation of the program, i.e. the process, i.e. its threads.

In this respect, the signal is similar to the processor-level interrupt mechanism: the processor
executes a thread that can be interrupted by a device interrupt. The processor then suspends
execution of the thread, saves its context, and jumps to the interrupt handler. After the interrupt
handler completes, it returns and resumes the thread (restores its context). Similarly, the
signals interrupt the execution of a thread, the signal processing function is called (the default
function or a function defined in the program) and after its completion the processor returns to
the thread and resumes its operation.

Let us consider the signals SIGINT (signal interrupt) and SIGTERM (terminate). The usual use
of the signal SIGINT is to stop a process. Usually this is a "forced" interrupt due to an error in
the execution of the process. On the other hand, SIGTERM is also used to interrupt the
process, but for other reasons and not because of program errors. For example, when the
system is shutting down, all processes must be stopped, but in a pleasant way. They are
notified to stop with this signal. Then a corresponding function (SIGTERM handler) is called
within a process, which can perform additional "housekeeping" before the process stops
voluntarily.

In the terminal, we send SIGINT to the active process by pressing Ctrl + C and the process is
terminated (default behavior). The signal can also be sent with special shell commands or
other programs through the OS interface. With the kill command we can send a signal to a
process whose identification number (PID) we know with:

```
$ kill -<signal id> <PID>
```

Signal SIGTERM can be sent to process with PID 2351 with command:

```
$ kill -SIGTERM 2351
```

Character `$` is command shell prefix, not part of the command.

For most signals, the program can specify what to do with them. If the program does not do
this, the default behavior is used. In many cases this means that the process is stopped, like
with the SIGINT and SIGTERM signals.

The program defines its behavior for signals through OS interface - it masks a signal, usually
with a signal handler function (function defined in the program). There are several interfaces
for this, such as the older signal and sigset functions and the newer sigaction, which is used
in this lab.

In the next example, three signals are masked, SIGUSR1, SIGTERM, and SIGINT. The
SIGUSR1 signal is a "user" signal that serves no particular purpose. Here SIGUSR1 is used
to simulate an event where an action must be performed. The signal handler for SIGTERM
and SIGINT, on the other hand, prints a message and stops the process. However, in the
handler for SIGTERM, we only announce that programs need to be stopped by using a global
variable run. When the process returns from this signal handler, it will recognize this change
and exit the infinite loop.

Example programs: [example_signals.c](lab01/example_signals.c), [example_sleep.c](lab01/example_sleep.c)

Problem solution: [signals.c](lab01/signals.c)
