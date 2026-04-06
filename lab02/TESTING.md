yy# Lab 2 Testing Guide

This file explains how to build and test the Lab 2 shell implementation in [shell.c](/C:/Users/borna/CLionProjects/OS/lab02/shell.c).

## Which File To Test

Test [shell.c](/C:/Users/borna/CLionProjects/OS/lab02/shell.c).

There is also a file named [lab2-sucelja.c](/C:/Users/borna/CLionProjects/OS/lab02/lab2-sucelja.c), but that is not the completed solution that was added for the assignment.

## What To Test

The shell should support:

- external program execution
- foreground execution
- background execution with `&`
- built-in `cd`
- built-in `exit`
- built-in `ps`
- built-in `kill`
- built-in `history`
- history replay with `!n`
- process tracking for shell-started programs only
- cleanup of remaining jobs on exit

## Build

From the repository root:

```sh
gcc -Wall -Wextra -Werror -std=c11 -pedantic lab02/shell.c -o lab02/shell
```

This build command was verified in the current repo.

## Run

Start the shell:

```sh
./lab02/shell
```

You should see the prompt:

```txt
osh$
```

## Test 1: Simple Foreground Command

Inside the shell, run:

```txt
pwd
```

Verify that the current directory is printed and the shell returns to the prompt afterward.

You can also try:

```txt
echo hello
```

Expected result:

- the command executes immediately
- output is shown
- the shell prompt returns after the command exits

## Test 2: `history`

Run a few commands:

```txt
pwd
echo test
history
```

Verify that:

- previous commands are listed
- each entry has an index number
- `history` itself also appears in the list

Expected structure:

```txt
    1 pwd
    2 echo test
    3 history
```

## Test 3: History Replay With `!n`

After creating a history list, run:

```txt
!1
```

Verify that:

- the shell prints the replayed command
- the command is executed again

Example:

```txt
!1
pwd
/some/path
```

## Test 4: `cd`

Run:

```txt
pwd
cd ..
pwd
```

Verify that:

- the second `pwd` prints the parent directory
- the current shell process changes directory

Also test an invalid directory:

```txt
cd does-not-exist
```

Verify that an error message is printed.

## Test 5: Background Execution

Run:

```txt
sleep 10 &
```

Verify that:

- the shell immediately returns to the prompt
- it prints the background PID

Expected structure:

```txt
[background pid 1234]
```

## Test 6: `ps`

After starting one or more background jobs, run:

```txt
ps
```

Verify that:

- running shell-managed processes are listed
- the output includes PID and command

Expected structure:

```txt
PID      command
1234     sleep 10 &
```

If no jobs are running, it should print:

```txt
(no running processes)
```

## Test 7: `kill`

Start a background process:

```txt
sleep 30 &
ps
```

Take the PID shown by `ps`, then run:

```txt
kill <PID> 15
```

Verify that:

- the shell accepts the command
- the background process disappears from `ps` soon after

Also test invalid cases:

```txt
kill 999999 15
kill abc 15
kill <PID>
```

Verify that:

- unmanaged PIDs are rejected
- invalid arguments are rejected
- incorrect usage prints an error

## Test 8: Foreground Wait Behavior

Run:

```txt
sleep 3
```

Verify that:

- the shell does not return to the prompt immediately
- the prompt returns only after about 3 seconds

This confirms foreground waiting behavior.

## Test 9: Exit Cleanup

Start one or more background jobs:

```txt
sleep 30 &
sleep 30 &
exit
```

Verify that:

- the shell exits
- the jobs it started do not continue running afterward

You can confirm from another terminal with:

```sh
ps -ef | grep sleep
```

or:

```sh
pgrep -a sleep
```

The exact command depends on your environment.

## Test 10: Signal Forwarding To Foreground Program

This is best tested interactively in a real terminal.

1. Start the shell:

```sh
./lab02/shell
```

2. Run a foreground command that normally reacts to `Ctrl+C`:

```txt
sleep 30
```

3. Press `Ctrl+C`.

Verify that:

- the foreground program stops
- the shell itself does not exit
- control returns to the prompt

This checks `SIGINT` forwarding to the foreground process group.

## Suggested Full Manual Test Session

Run this sequence inside the shell:

```txt
pwd
echo hello
history
!1
sleep 10 &
ps
kill <PID> 15
ps
cd ..
pwd
sleep 3
exit
```

This covers most of the required functionality in one pass.

## Common Problems

- If `sleep` is not found, you are probably not running in a UNIX-like environment.
- If `Ctrl+C` kills the shell itself, test again in a terminal with proper job-control support.
- If `ps -ef` or `pgrep` are unavailable, use the process-listing tool provided by your environment.
- On Windows, use MSYS2 or another POSIX-like environment for realistic testing.
