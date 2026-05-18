#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PAGE_SIZE 256
#define OFFSET_BITS 8
#define MAX_VPAGES 8
#define MAX_PROCESSES 5
#define MAX_FRAMES 12

typedef struct {
    bool present;
    int frame;
} PageTableEntry;

typedef struct {
    int pid;
    int allocated_pages;
    bool alive;
    int requests_generated;
    PageTableEntry pages[MAX_VPAGES];
    unsigned char disk[MAX_VPAGES][PAGE_SIZE];
} Process;

typedef struct {
    bool occupied;
    bool dirty;
    int pid;
    int page;
    unsigned char data[PAGE_SIZE];
} Frame;

typedef struct {
    bool write;
    int page;
    int offset;
    unsigned char value;
} Access;

static Process processes[MAX_PROCESSES];
static Frame frames[MAX_FRAMES];
static int clock_bits[MAX_FRAMES];

static int process_count;
static int frame_count;
static int clock_hand;
static unsigned int rng_state;

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [seed] [steps] [processes] [frames]\n", prog);
    fprintf(stderr, "All arguments are optional. Without them the simulation is randomized.\n");
}

static void fail(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

static int parse_arg(const char *text, const char *name, int min_value, int max_value) {
    char *end = NULL;
    long value = strtol(text, &end, 10);

    if (end == text || *end != '\0') {
        fprintf(stderr, "Invalid %s: %s\n", name, text);
        exit(EXIT_FAILURE);
    }
    if (value < min_value || value > max_value) {
        fprintf(stderr, "%s must be in range [%d, %d]\n", name, min_value, max_value);
        exit(EXIT_FAILURE);
    }

    return (int)value;
}

static unsigned int next_rand(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static int rand_between(int min_value, int max_value) {
    unsigned int span = (unsigned int)(max_value - min_value + 1);
    return min_value + (int)(next_rand() % span);
}

static bool chance(int percent) {
    return rand_between(1, 100) <= percent;
}

static unsigned char initial_value(int pid, int page, int offset) {
    unsigned int value = (unsigned int)(pid * 53 + page * 31 + offset * 17 + 0x42);
    return (unsigned char)(value & 0xffu);
}

static int alive_processes(void) {
    int count = 0;
    int i;

    for (i = 0; i < process_count; i++) {
        if (processes[i].alive) {
            count++;
        }
    }

    return count;
}

static Process *pick_process(void) {
    int alive = alive_processes();
    int target;
    int i;

    if (alive == 0) {
        return NULL;
    }

    target = rand_between(1, alive);
    for (i = 0; i < process_count; i++) {
        if (!processes[i].alive) {
            continue;
        }
        target--;
        if (target == 0) {
            return &processes[i];
        }
    }

    return NULL;
}

static void print_frames(void) {
    int i;

    printf("frames:");
    for (i = 0; i < frame_count; i++) {
        if (frames[i].occupied) {
            printf(" %d:[%d:%d]", i, frames[i].pid, frames[i].page);
        } else {
            printf(" %d:[-:-]", i);
        }
    }
    printf("\n");
}

static void print_clock(const char *label) {
    int i;

    printf("%s", label);
    for (i = 0; i < frame_count; i++) {
        if (i > 0) {
            printf("-");
        }
        printf("%d", clock_bits[i]);
    }
    printf("\n");
}

static void print_hand(const char *label) {
    int i;

    printf("%s", label);
    for (i = 0; i < clock_hand * 2; i++) {
        printf(" ");
    }
    printf("^\n");
}

static void print_page_tables(void) {
    int i;
    int page;

    for (i = 0; i < process_count; i++) {
        Process *proc = &processes[i];

        printf("pt[%d]:", proc->pid);
        if (!proc->alive) {
            printf(" terminated");
        }
        for (page = 0; page < MAX_VPAGES; page++) {
            if (page >= proc->allocated_pages) {
                printf(" %d:NA", page);
            } else if (proc->pages[page].present) {
                printf(" %d:F%d", page, proc->pages[page].frame);
            } else {
                printf(" %d:D", page);
            }
        }
        printf("\n");
    }
}

static void print_state(void) {
    print_frames();
    print_page_tables();
}

static void release_process_frames(Process *proc) {
    int i;

    for (i = 0; i < frame_count; i++) {
        if (!frames[i].occupied || frames[i].pid != proc->pid) {
            continue;
        }

        if (frames[i].dirty) {
            memcpy(proc->disk[frames[i].page], frames[i].data, PAGE_SIZE);
        }

        frames[i].occupied = false;
        frames[i].dirty = false;
        frames[i].pid = 0;
        frames[i].page = 0;
        memset(frames[i].data, 0, sizeof(frames[i].data));
        clock_bits[i] = 0;
    }

    for (i = 0; i < proc->allocated_pages; i++) {
        proc->pages[i].present = false;
        proc->pages[i].frame = -1;
    }
}

static int select_victim_frame(void) {
    while (1) {
        if (!frames[clock_hand].occupied || clock_bits[clock_hand] == 0) {
            int victim = clock_hand;

            clock_hand = (clock_hand + 1) % frame_count;
            return victim;
        }

        clock_bits[clock_hand] = 0;
        clock_hand = (clock_hand + 1) % frame_count;
    }
}

static void load_page_into_frame(Process *proc, int page, int frame_index) {
    if (frames[frame_index].occupied) {
        Process *old_proc = &processes[frames[frame_index].pid - 1];
        int old_page = frames[frame_index].page;

        if (frames[frame_index].dirty) {
            memcpy(old_proc->disk[old_page], frames[frame_index].data, PAGE_SIZE);
        }
        old_proc->pages[old_page].present = false;
        old_proc->pages[old_page].frame = -1;
    }

    frames[frame_index].occupied = true;
    frames[frame_index].dirty = false;
    frames[frame_index].pid = proc->pid;
    frames[frame_index].page = page;
    memcpy(frames[frame_index].data, proc->disk[page], PAGE_SIZE);

    proc->pages[page].present = true;
    proc->pages[page].frame = frame_index;
    clock_bits[frame_index] = 1;
}

static void print_access(const Process *proc, const Access *access) {
    if (access->write) {
        printf("process %d WRITE(0x%02X) LA=0x%02X%02X\n",
               proc->pid, access->value, access->page, access->offset);
    } else {
        printf("process %d READ LA=0x%02X%02X\n",
               proc->pid, access->page, access->offset);
    }
}

static void simulate_access(Process *proc, Access access) {
    int physical_address;
    int frame_index;

    print_access(proc, &access);

    if (access.page >= proc->allocated_pages) {
        printf("MEMORY FAULT: page %d not allocated, terminating process %d\n",
               access.page, proc->pid);
        proc->alive = false;
        release_process_frames(proc);
        print_state();
        printf("\n");
        return;
    }

    print_state();

    if (!proc->pages[access.page].present) {
        int victim;
        bool replacing;
        int old_pid = 0;
        int old_page = 0;
        bool old_dirty = false;

        printf("MISS (page %d not in memory)\n", access.page);
        print_clock("clock before: ");
        print_hand("hand before:  ");

        victim = select_victim_frame();
        replacing = frames[victim].occupied;
        if (replacing) {
            old_pid = frames[victim].pid;
            old_page = frames[victim].page;
            old_dirty = frames[victim].dirty;
        }

        print_clock("clock after:  ");
        print_hand("hand after:   ");

        if (replacing) {
            printf("use frame %d:\n", victim);
            if (old_dirty) {
                printf("- save to disk:   process %d, page %d\n", old_pid, old_page);
            } else {
                printf("- discard clean:  process %d, page %d\n", old_pid, old_page);
            }
            printf("- load from disk: process %d, page %d\n", proc->pid, access.page);
        } else {
            printf("use free frame %d:\n", victim);
            printf("- load from disk: process %d, page %d\n", proc->pid, access.page);
        }

        load_page_into_frame(proc, access.page, victim);
        print_state();
        printf("restarting instruction:\n");
        simulate_access(proc, access);
        return;
    }

    frame_index = proc->pages[access.page].frame;
    clock_bits[frame_index] = 1;

    printf("HIT: frame %d\n", frame_index);
    physical_address = (frame_index << OFFSET_BITS) | access.offset;
    printf("paging: process %d, page=%d => frame=%d, 0x%02X%02X => 0x%04X\n",
           proc->pid, access.page, frame_index, access.page, access.offset, physical_address);

    if (access.write) {
        frames[frame_index].data[access.offset] = access.value;
        frames[frame_index].dirty = true;
        printf("save (0x%02X) at 0x%04X\n", access.value, physical_address);
    } else {
        printf("read value at 0x%04X => 0x%02X\n",
               physical_address, frames[frame_index].data[access.offset]);
    }

    print_clock("clock: ");
    print_hand("hand:  ");
    printf("\n");
}

static Access make_access(Process *proc) {
    Access access;
    bool invalid = false;

    if (proc->requests_generated >= 3 && proc->allocated_pages < MAX_VPAGES) {
        invalid = chance(15);
    }

    access.write = chance(50);
    access.offset = rand_between(0, PAGE_SIZE - 1);
    access.value = (unsigned char)rand_between(0, 255);

    if (invalid) {
        access.page = rand_between(proc->allocated_pages, MAX_VPAGES - 1);
    } else {
        access.page = rand_between(0, proc->allocated_pages - 1);
    }

    proc->requests_generated++;
    return access;
}

static void init_simulation(void) {
    int i;
    int page;
    int offset;

    memset(processes, 0, sizeof(processes));
    memset(frames, 0, sizeof(frames));
    memset(clock_bits, 0, sizeof(clock_bits));
    clock_hand = 0;

    for (i = 0; i < process_count; i++) {
        processes[i].pid = i + 1;
        processes[i].allocated_pages = rand_between(2, MAX_VPAGES - 1);
        processes[i].alive = true;
        processes[i].requests_generated = 0;

        for (page = 0; page < MAX_VPAGES; page++) {
            processes[i].pages[page].present = false;
            processes[i].pages[page].frame = -1;
            for (offset = 0; offset < PAGE_SIZE; offset++) {
                processes[i].disk[page][offset] = initial_value(processes[i].pid, page, offset);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int steps = 20;
    int step;
    unsigned int default_seed = (unsigned int)time(NULL);
    unsigned int initial_seed;

    if (argc > 5) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    rng_state = default_seed;
    if (argc >= 2) {
        rng_state = (unsigned int)parse_arg(argv[1], "seed", 0, 2147483647);
    }
    initial_seed = rng_state;

    if (argc >= 3) {
        steps = parse_arg(argv[2], "steps", 1, 200);
    }

    process_count = (argc >= 4) ? parse_arg(argv[3], "processes", 1, MAX_PROCESSES) : rand_between(3, MAX_PROCESSES);
    frame_count = (argc >= 5) ? parse_arg(argv[4], "frames", 2, MAX_FRAMES) : rand_between(4, 8);

    if (frame_count <= 0 || process_count <= 0) {
        fail("Simulation configuration is invalid.");
    }

    init_simulation();

    printf("seed=%u steps=%d processes=%d frames=%d page-size=%dB\n",
           initial_seed, steps, process_count, frame_count, PAGE_SIZE);
    for (step = 0; step < process_count; step++) {
        printf("process %d allocated pages: %d\n",
               processes[step].pid, processes[step].allocated_pages);
    }
    printf("\n");

    for (step = 0; step < steps; step++) {
        Process *proc = pick_process();

        if (proc == NULL) {
            printf("All processes terminated.\n");
            break;
        }

        simulate_access(proc, make_access(proc));
    }

    return EXIT_SUCCESS;
}
