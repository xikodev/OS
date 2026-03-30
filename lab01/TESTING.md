# Lab 1 Testing Guide

This file explains how to build and test the Lab 1 interrupt simulator in [signals.c](/C:/Users/borna/CLionProjects/OS/lab01/signals.c).

## What To Test

The Lab 1 program should demonstrate:

- startup and initial state printing
- registration of interrupt requests through `SIGINT`
- interactive priority entry from `1` to `4`
- execution of the correct ISR
- nested handling of higher-priority interrupts
- restoration of the previous context after ISR completion
- graceful termination through `SIGTERM`

## Build

From the repository root:

```sh
gcc -Wall -Wextra -Werror -std=c11 -pedantic lab01/signals.c -o lab01/signals
```

This build command was verified in the current repo.

## Run

Start the simulator:

```sh
./lab01/signals
```

The program prints its PID on startup. You will need that PID to send signals from another terminal.

## Test 1: Startup

After launch, verify that:

- the program prints a startup message
- the PID is shown
- the initial system state is printed
- the main program starts printing iterations

Expected indicators:

- `Hardware interrupt simulator started`
- `PID = ...`
- `STATE CHANGE: Initial state`
- `Running:MAIN PROGRAM`

## Test 2: Single Interrupt

In another terminal, send one interrupt request:

```sh
kill -SIGINT <PID>
```

Go back to the simulator terminal and enter a priority such as:

```txt
2
```

Verify that:

- the program reports `SIGINT` reception
- it asks for an interrupt priority
- it marks that priority as pending
- it starts ISR level 2
- it executes 5 ISR steps
- it restores the previous context and returns to the main program

## Test 3: Invalid Priority Input

Send `SIGINT` again:

```sh
kill -SIGINT <PID>
```

When prompted, enter invalid input such as:

```txt
0
abc
5
```

Then enter a valid value:

```txt
3
```

Verify that:

- invalid input is rejected
- the program keeps prompting until a valid priority is entered
- after valid input, ISR level 3 runs normally

## Test 4: Higher-Priority Preemption

This test checks nested interrupts.

1. Send one interrupt:

```sh
kill -SIGINT <PID>
```

2. Enter a lower priority, for example:

```txt
2
```

3. While ISR level 2 is still running, send another interrupt from the second terminal:

```sh
kill -SIGINT <PID>
```

4. In the simulator terminal, enter a higher priority:

```txt
4
```

Verify that:

- the second interrupt is registered while ISR 2 is active
- ISR level 4 is accepted before returning to the main program
- after ISR 4 completes, the previous context is restored
- execution eventually returns to the correct earlier context

## Test 5: Lower-Priority Pending Request

This test checks that lower-priority interrupts do not preempt a higher-priority ISR immediately.

1. Trigger a high-priority interrupt first:

```sh
kill -SIGINT <PID>
```

Enter:

```txt
4
```

2. While ISR 4 is running, send another interrupt:

```sh
kill -SIGINT <PID>
```

Enter:

```txt
1
```

Verify that:

- the lower-priority request is registered as pending
- it does not preempt ISR 4 immediately
- it runs only after the current higher-priority work allows it

## Test 6: Graceful Termination

From another terminal:

```sh
kill -SIGTERM <PID>
```

Verify that:

- the program reports `SIGTERM`
- termination is registered in the state output
- the simulator exits cleanly

Expected indicators:

- `SIGTERM received -> simulator stopping`
- `STATE CHANGE: Termination request registered`
- `Simulator finished`

## Suggested Manual Test Session

Use one terminal for the program and another for signals.

Terminal 1:

```sh
./lab01/signals
```

Terminal 2:

```sh
kill -SIGINT <PID>
kill -SIGINT <PID>
kill -SIGTERM <PID>
```

Terminal 1 input sequence:

```txt
2
4
```

This is a quick way to see normal execution, interrupt nesting, and shutdown.

## Common Problems

- If `kill` says the process does not exist, check the PID again.
- If the program seems stuck, make sure you entered the interrupt priority in the simulator terminal, not the second terminal.
- If `gcc` is missing, install a C compiler first.
- On Windows, run the program in an environment that supports POSIX signals, such as MSYS2.
