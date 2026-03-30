# Lab Questions and Answers

## Lab 1

### In General

#### How to send a signal to a process

`a)` Via the keyboard:

- `Ctrl+C` usually sends `SIGINT` to the foreground process group.
- `Ctrl+\` usually sends `SIGQUIT`.
- `Ctrl+Z` usually sends `SIGTSTP`.

`b)` From the shell:

- Use the `kill` command, for example:

```sh
kill -SIGTERM 1234
kill -2 1234
kill -SIGKILL 1234
```

#### List several signals and what they are used for

- `SIGINT`: interrupt from keyboard, usually terminates the process.
- `SIGTERM`: polite termination request.
- `SIGKILL`: immediate forced termination; it cannot be caught or ignored.
- `SIGUSR1`, `SIGUSR2`: user-defined signals for application-specific purposes.
- `SIGCHLD`: sent to a parent when a child process changes state.
- `SIGSTOP`: stops a process; it cannot be caught or ignored.
- `SIGCONT`: continues a stopped process.
- `SIGQUIT`: quits the process and often creates a core dump.

#### What does the `sigaction` function do?

`sigaction` sets or reads the action associated with a signal.

#### What can be achieved with `sigaction`?

With `sigaction`, you can:

- set a handler function with `sa_handler`
- use `sa_sigaction` for an extended handler form
- block additional signals during handler execution with `sa_mask`
- change behavior with `sa_flags`, for example `SA_RESTART` or `SA_NODEFER`
- retrieve the previous signal action through the third argument

#### Describe the signal reception procedure

The usual sequence is:

1. A signal is generated.
2. The kernel marks it as pending for the process or thread.
3. If it is not blocked, the kernel delivers it at a safe point.
4. The current execution context is saved.
5. The handler runs, or the default action is performed.
6. After the handler finishes, execution resumes unless the process was terminated or stopped.

#### When will the signal not be accepted?

A signal will not be accepted immediately if:

- it is currently blocked in the signal mask
- the same standard, non-realtime signal is already pending
- the process has already terminated

Note:

- `SIGKILL` and `SIGSTOP` are always delivered by the kernel, but you cannot install handlers for them.

#### If a program is sent a `SIGINT` while waiting in `sleep(10)`, what happens afterward?

The sleep is interrupted by the signal. After the signal handler returns:

- the program may continue immediately if the sleep call is not restarted
- or it may continue sleeping the remaining time if the code explicitly handles interruption and retries

In [example_sleep.c](/C:/Users/borna/CLionProjects/OS/lab01/example_sleep.c), the function `good_sleep()` uses `nanosleep()` in a loop and retries with the remaining unslept time. Because of that, the program does sleep the remaining time.

#### A program is currently processing `SIGINT`. What happens if it gets another `SIGINT`? What if it gets `SIGTERM`?

If the program is already handling `SIGINT`:

- by default, that same signal is blocked while its handler runs
- one more `SIGINT` may remain pending and be delivered later
- multiple identical standard signals are not counted separately, so repeated arrivals can collapse into one pending signal

If the program then receives `SIGTERM`:

- if `SIGTERM` is not blocked, its handler may interrupt the current handler
- if it is blocked by `sa_mask`, it stays pending until the current handler finishes

In [example_signals.c](/C:/Users/borna/CLionProjects/OS/lab01/example_signals.c), `SIGTERM` is explicitly blocked during the `SIGUSR1` handler.

#### What if no behavior is specified for `SIGINT` with `sigaction`?

Then the default behavior applies. For `SIGINT`, that usually means the process terminates.

### Regarding Exercises

#### Task 1

##### Check if the program works

Yes. The Lab 1 files in the repo are:

- [example_signals.c](/C:/Users/borna/CLionProjects/OS/lab01/example_signals.c)
- [example_sleep.c](/C:/Users/borna/CLionProjects/OS/lab01/example_sleep.c)
- [signals.c](/C:/Users/borna/CLionProjects/OS/lab01/signals.c)

The main Lab 1 solution is [signals.c](/C:/Users/borna/CLionProjects/OS/lab01/signals.c), and it compiles and runs as the interrupt simulator.

##### What would happen if the `sigaction` call were removed from the program?

That signal would use its default action instead of the custom handler.

Examples:

- without `sigaction(SIGINT, ...)`, `SIGINT` would usually terminate the process
- without `sigaction(SIGTERM, ...)`, `SIGTERM` would usually terminate the process
- without `sigaction(SIGUSR1, ...)`, `SIGUSR1` would usually terminate the process

##### Why does `Ctrl+\` interrupt execution?

Because the terminal sends `SIGQUIT` to the foreground process group when `Ctrl+\` is pressed.

##### Send a `SIGKILL` signal to the program using another terminal

Use:

```sh
kill -SIGKILL <PID>
```

The process will terminate immediately. No signal handler runs and no cleanup code executes.

##### While a signal is already waiting to be processed, send the same signal again. What happens? Why?

For standard signals such as `SIGUSR1`, if the same signal is already pending and you send it again:

- it usually does not create another separately queued pending instance
- the signal remains just “pending”

Why:

- standard signals are not queued by multiplicity
- the kernel usually remembers only that at least one such signal is pending

## Lab 2

### About Threads

#### What do `pthread_create` and `pthread_join` do?

- `pthread_create` creates a new thread and starts executing the given start routine in that thread.
- `pthread_join` waits for a specific thread to finish and can collect its return value.

#### What is the first argument to `pthread_create` for?

It is a pointer where the ID of the new thread is stored.

#### What is the third argument to `pthread_create` for?

It is the pointer to the thread start function.

#### What is the fourth argument to `pthread_create` for?

It is the argument passed to the thread start function.

#### Why is the last argument passed to the initial function a pointer?

Because `pthread_create` passes one generic `void *` argument. A pointer lets you pass:

- a single variable
- a structure
- an array
- any custom data block

#### What happens to the thread that calls `pthread_join`?

It blocks and waits.

#### When will the thread that called `pthread_join` resume?

It resumes when the target thread terminates.

#### What would happen if the main thread created new threads and then finished without a `pthread_join` loop?

Usually the whole process would terminate when `main` returns, unless the process stays alive through another mechanism such as `pthread_exit()`. That means the created threads may be terminated before completing their work.

### About Processes

#### What does `fork()` do?

`fork()` creates a new child process by duplicating the calling process.

#### What does `fork()` return?

- `0` in the child process
- child PID in the parent process
- `-1` on error

#### What does `wait()` do? Why is it important?

`wait()` waits for a child to finish or change state and reaps it. It is important because otherwise terminated children can remain as zombie processes.

#### What if the parent does not call `wait` for each child?

The finished child processes may remain zombies until the parent exits or until some other wait-family call reaps them.

#### If we use `wait(&variable)` instead of `wait(NULL)`, what value does `variable` have afterward?

It contains the encoded child exit status information. It is not just the plain exit code. You interpret it with macros such as:

- `WIFEXITED(variable)`
- `WEXITSTATUS(variable)`
- `WIFSIGNALED(variable)`

#### What is shared memory?

Shared memory is a memory region that multiple processes can map into their address spaces so they all access the same underlying bytes.

#### Why use shared memory for communication between parent and child processes or between children?

Because after `fork()`, processes have separate address spaces. Ordinary global or local variables are copied, not truly shared. Shared memory gives all participating processes access to the same data.

#### Describe `shmget` and `shmat`

- `shmget` creates or obtains a shared memory segment and returns its segment ID.
- `shmat` attaches that segment to the process address space and returns a pointer to it.

#### What do `shmdt` and `shmctl` do?

- `shmdt` detaches shared memory from the current process.
- `shmctl` performs control operations on the segment, commonly `IPC_RMID` to mark it for deletion.

Typical cleanup pattern:

```c
shmdt(ptr);
shmctl(shmid, IPC_RMID, NULL);
```

#### Check that `shmget` and `shmat` are used correctly

The important idea is:

- `shmget` should create the needed shared region once per logical segment
- `shmat` should attach that segment once per process that needs access

It is valid to allocate multiple separate segments, but the cleanest solution is usually to allocate one larger shared block and place all shared fields inside it.

### Dekker / Lamport Bakery Algorithm

#### For what maximum number of threads can the Dekker/Lamport algorithm be used?

- Dekker’s algorithm is for 2 threads.
- Lamport’s bakery algorithm works for arbitrary `N` threads.

#### Can a thread enter the critical section twice in a row according to Dekker’s algorithm?

Yes. That can happen if:

- it exits the critical section
- no other thread is currently competing
- or the other thread withdraws or does not request entry in time

Then the same thread may enter again.

#### What is the purpose of `NUMBER` and `INPUT`?

- `NUMBER[i]` is the ticket number of thread `i`.
- `INPUT[i]` or `CHOOSING[i]` indicates that thread `i` is currently choosing its ticket number.

This prevents another thread from reading an unfinished ticket choice.

#### If threads 1, 2, and 3 want to enter the critical section and `NUMBER[] = 5 4 9`, which enters first?

Thread 2 enters first because it has the smallest ticket number: `4`.

#### Will the algorithm work if we only have one thread?

Yes. It works trivially because there is no competition.

#### What is the purpose of `while INPUT[j] == 1`?

It waits until thread `j` finishes choosing its ticket number, so the observing thread sees a stable `NUMBER[j]` value.

#### What happens when `j == i` in a `while` loop? Why does the thread not wait for itself?

The algorithm is written so the thread ignores itself, usually with a condition like `j != i`. That is why it does not wait for itself.

#### In the worst case, how many other threads must one thread wait for if there are `N` total?

In the worst case, it may wait for `N - 1` other threads.

#### What if a thread gets stuck in the non-critical section? Does that affect the critical section of others?

No. If a thread is stuck in the non-critical section and is not competing for entry, it does not block other threads from entering the critical section.

### 2a / 2b

#### If we remove synchronization, why can variable `A` be less than `N * M` after the program ends?

Because increments such as `A++` are not atomic. Multiple threads or processes can interleave:

1. read `A`
2. compute `A + 1`
3. write back

This causes lost updates, so the final result can be smaller than expected.

#### What if in 2a we do not use shared memory for `A`, but define it as a global variable like `int A = 0;`?

If the solution uses processes, that will not work for shared counting. After `fork()`, each process has its own copy of the global variable. Changes made by one process are not automatically visible to the others, so the final result in the parent will not reflect the combined increments correctly.
