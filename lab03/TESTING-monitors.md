# Lab 3 Monitors Testing Guide

This file explains how to build and test the race simulation with monitors in [monitors.c](/C:/Users/borna/CLionProjects/OS/lab03/monitors.c).

## What To Test

The program should demonstrate:

- creation of 6 driver threads
- creation of 8 mechanic threads
- creation of 2 flag-person threads
- race start synchronization
- one driver at a time in each team pit
- four mechanics working on each pit stop
- driver release only after all four mechanics finish
- clean shutdown after all drivers finish the race

## Build

From the repository root:

```sh
gcc -Wall -Wextra -Werror -std=c11 -pedantic lab03/monitors.c -o lab03/monitors -pthread
```

## Run

Start the simulation:

```sh
./lab03/monitors
```

## Test 1: Startup

Verify that startup output includes:

- race setup summary
- race start message
- driver activity messages

Expected indicators:

```txt
Race setup complete: 6 drivers, 8 mechanics, 2 flag persons
Race started
```

## Test 2: Pit Entry Synchronization

During the run, watch one team’s output.

Verify that:

- drivers may wait for the pit
- the flag person raises the flag before a driver enters
- only one driver from the same team is in the pit at a time

Expected structure:

```txt
TEAM 1 DRIVER 2 waiting for pit stop
TEAM 1 FLAG: raised, driver may enter pit
TEAM 1 DRIVER 2 entered pit
```

Another team may be using its own pit at the same time. That is valid.

## Test 3: Mechanics Coordination

For each pit stop, verify that:

- the flag person lowers the flag after the driver enters
- exactly four mechanic completion messages appear for that team and pit stop
- the remaining counter reaches `0`

Expected structure:

```txt
TEAM 1 FLAG: lowered, mechanics start
TEAM 1 MECHANIC 1 finished wheel, remaining 3
TEAM 1 MECHANIC 4 finished wheel, remaining 2
TEAM 1 MECHANIC 2 finished wheel, remaining 1
TEAM 1 MECHANIC 3 finished wheel, remaining 0
```

The order of mechanics will vary.

## Test 4: Driver Release

After the fourth mechanic finishes, verify that:

- the flag person raises the flag again
- the current driver leaves the pit
- a later driver can eventually use the same pit

Expected structure:

```txt
TEAM 2 FLAG: raised, driver released
TEAM 2 DRIVER 1 leaving pit
```

## Test 5: Race Completion

Let the simulation run to the end.

Verify that:

- all six drivers print `finished race`
- flag-person threads print stopping messages
- mechanic threads print stopping messages
- the main thread prints `Race finished`

## Suggested Manual Session

```sh
gcc -Wall -Wextra -Werror -std=c11 -pedantic lab03/monitors.c -o lab03/monitors -pthread
./lab03/monitors
```

Because random sleep times are used, every run will interleave differently. The important property is correct synchronization, not a fixed order of lines.

## Common Problems

- If compilation fails, make sure pthread support is enabled with `-pthread`.
- On Windows, use a POSIX-capable environment such as MSYS2.
- If output looks different from a previous run, that alone is not a bug because timing is randomized.
