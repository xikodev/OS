# Lab 4 Testing Guide

This file explains how to build and test the Lab 4 paging simulator in [paging.c](/C:/Users/borna/CLionProjects/OS/lab04/paging.c).

## What To Test

The Lab 4 program should demonstrate:

- random or seeded simulation startup
- per-process allocated virtual pages
- RAM frame state printing
- page table state printing
- logical-to-physical address translation on a hit
- page fault handling on a miss
- page loading from disk into RAM
- page replacement using the clock algorithm
- dirty-page writeback to disk on replacement
- process termination on invalid page access

## Build

From the repository root:

```sh
gcc -Wall -Wextra -Werror -std=c11 -pedantic lab04/paging.c -o lab04/paging
```

On this Windows workspace, the verified equivalent was:

```sh
gcc -Wall -Wextra -Werror -std=c11 -pedantic lab04/paging.c -o lab04/paging.exe
```

## Run

The program accepts optional arguments:

```txt
./lab04/paging [seed] [steps] [processes] [frames]
```

Examples:

```sh
./lab04/paging
./lab04/paging 12345 16 3 2
```

The seeded form is better for repeatable testing.

## Expected Output Structure

Each memory access should print:

- the process number
- the operation type, `READ` or `WRITE`
- the logical address
- the current frame contents
- the current page tables
- whether the access was a `HIT`, `MISS`, or `MEMORY FAULT`

For misses, it should also print:

- clock bits before replacement
- clock hand position before replacement
- clock bits after scanning
- clock hand position after scanning
- which frame was used
- whether the old page was saved to disk or discarded
- which new page was loaded
- a restarted instruction

## Test 1: Startup

Run:

```sh
./lab04/paging 12345 5 3 2
```

Verify that:

- the seed is printed
- the number of steps is printed
- the number of processes and frames is printed
- each process shows how many pages were allocated

Expected indicators:

- `seed=12345`
- `steps=5`
- `process 1 allocated pages:`

## Test 2: Page Miss Followed By Restarted Hit

Run:

```sh
./lab04/paging 12345 6 3 2
```

Verify that at least one request shows:

- `MISS (page ... not in memory)`
- `use free frame ...` or `use frame ...`
- `restarting instruction:`
- the same logical access repeated
- `HIT: frame ...`

This confirms that a page fault loads the page and the instruction is retried.

## Test 3: Address Translation On Hit

Using the same seeded run:

```sh
./lab04/paging 12345 6 3 2
```

Find a `HIT` and verify that:

- the reported page maps to the reported frame
- the physical address uses the frame number as the high byte
- the logical offset is preserved

Example pattern:

```txt
paging: process 2, page=1 => frame=0, 0x01B7 => 0x00B7
```

That means:

- page `0x01`
- offset `0xB7`
- frame `0x00`
- physical address `0x00B7`

## Test 4: Clock Replacement

Run with a small number of frames to force replacement:

```sh
./lab04/paging 12345 16 3 2
```

Verify that after RAM fills up, a miss shows:

- `clock before:`
- `hand before:`
- `clock after:`
- `hand after:`
- `use frame ...`

This confirms the clock algorithm is scanning reference bits and selecting a victim frame.

## Test 5: Dirty Page Writeback

Run:

```sh
./lab04/paging 12345 16 3 2
```

Look for a sequence where:

- a page is written with `WRITE(...)`
- later that page is replaced
- replacement prints `save to disk`

Expected indicator:

```txt
- save to disk:   process X, page Y
```

This confirms dirty pages are written back before eviction.

## Test 6: Clean Page Discard

In the same or another seeded run:

```sh
./lab04/paging 12345 16 3 2
```

Verify that some evictions print:

```txt
- discard clean:  process X, page Y
```

This confirms clean pages are not unnecessarily written back.

## Test 7: Invalid Page Access

Run enough steps to allow an invalid page access to appear:

```sh
./lab04/paging 12345 30 3 2
```

Verify that at least one process eventually prints:

```txt
MEMORY FAULT: page ... not allocated, terminating process ...
```

Also verify that after termination:

- that process page table is marked as terminated
- its pages are no longer shown as present in frames

## Suggested Manual Test Session

Use these three runs:

```sh
./lab04/paging 12345 6 3 2
./lab04/paging 12345 16 3 2
./lab04/paging 12345 30 3 2
```

These are enough to cover:

- startup
- hit/miss behavior
- restarted instructions
- clock replacement
- dirty and clean eviction
- invalid-page termination

## Common Problems

- If the output changes between runs, use an explicit seed.
- If you do not see replacement, reduce the frame count.
- If you do not see a memory fault, increase the step count.
- If `gcc` is missing, install a C compiler first.
- On Windows, the executable name may be `paging.exe` instead of `paging`.
