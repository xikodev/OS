# Lab 3 Semaphores Testing Guide

This file explains how to build and test the producer-consumer semaphore solution in [semaphores.c](/C:/Users/borna/CLionProjects/OS/lab03/semaphores.c).

## What To Test

The program should demonstrate:

- one consumer process
- one producer process per command-line string
- bounded-buffer communication through shared memory
- semaphore synchronization for free slots and available messages
- transmission of the terminating `'\0'` marker for every producer
- final concatenation of all non-terminator characters received by the consumer

## Build

From the repository root:

```sh
gcc -Wall -Wextra -Werror -std=c11 -pedantic lab03/semaphores.c -o lab03/semaphores -pthread
```

This build command was verified in the current repo.

## Run

Start the program with one or more strings:

```sh
./lab03/semaphores 12345678 abcdef
```

## Test 1: Basic Two-Producer Run

Run:

```sh
./lab03/semaphores 12345678 abcdef
```

Verify that:

- producer lines appear for both producers
- consumer lines appear as characters are removed from the buffer
- empty transfers are printed when `'\0'` is sent or received
- the program ends by printing the collected string

Expected structure:

```txt
PRODUCER1 -> 1
PRODUCER2 -> a
CONSUMER <- 1
...
PRODUCER2 ->
CONSUMER <-
...
Received: ...
```

The exact interleaving may differ between runs.

## Test 2: Single Producer

Run:

```sh
./lab03/semaphores hello
```

Verify that:

- only `PRODUCER1` appears
- the consumer receives all characters from `hello`
- the final line is:

```txt
Received: hello
```

## Test 3: Multiple Short Strings

Run:

```sh
./lab03/semaphores A BB CCC DDDD
```

Verify that:

- four producers are created
- each producer eventually prints an empty transfer for its string terminator
- the consumer prints four matching empty receives
- the final received text contains exactly the letters sent by all producers, in the order they were consumed

Because producers run concurrently, the final mixed order depends on scheduling.

## Test 4: Buffer Pressure

Run with longer strings:

```sh
./lab03/semaphores abcdefghijklmnop 1234567890123456
```

Verify that:

- the program continues running without corruption or deadlock
- producers and the consumer keep alternating work
- the program exits normally after all data is consumed

This test increases the chance that the 5-slot bounded buffer fills up and exercises the `SLOTS` semaphore.

## Test 5: Missing Arguments

Run:

```sh
./lab03/semaphores
```

Verify that:

- the program prints a usage message
- it exits with failure instead of hanging

## Suggested Manual Session

```sh
gcc -Wall -Wextra -Werror -std=c11 -pedantic lab03/semaphores.c -o lab03/semaphores -pthread
./lab03/semaphores 12345678 abcdef
./lab03/semaphores hello
./lab03/semaphores A BB CCC DDDD
```

## Common Problems

- If `gcc` is missing, install a compiler first.
- On Windows, build and run this file in a POSIX-capable environment such as MSYS2.
- If the program appears stuck, confirm you are running the compiled `lab03/semaphores` binary from the current source file.
