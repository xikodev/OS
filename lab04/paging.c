#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PAGE_SIZE 256
#define OFFSET_BITS 8
#define MAX_PAGES 8
#define MAX_PROCESSES 5
#define MAX_FRAMES 12

typedef struct {
    bool present;
    int frame;
} PageEntry;

typedef struct {
    int pid;
    int num_pages;
    bool alive;
    int requests;
    PageEntry page_table[MAX_PAGES];
    unsigned char disk[MAX_PAGES][PAGE_SIZE];
} Process;

typedef struct {
    bool used;
    bool dirty;
    int pid;
    int page;
    unsigned char bytes[PAGE_SIZE];
} Frame;

typedef struct {
    bool write;
    int page;
    int offset;
    unsigned char value;
} Request;

Process processes[MAX_PROCESSES];
Frame memory_frames[MAX_FRAMES];
int clock_ref[MAX_FRAMES];

int n = 0;
int m = 0;
int hand = 0;

static int random_between(int a, int b) {
    /* obican random broj u intervalu */
    return a+rand()%(b-a+1);
}

static bool random_percent(int p) {
    return random_between(1, 100) <= p;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [seed] [steps] [processes] [frames]\n", prog);
}

static int read_number(const char *text, int min, int max, const char *what) {
    char *end = NULL;
    long x = strtol(text, &end, 10);

    /* provjera argumenta iz komandne linije */
    if (end == text || *end != '\0') {
        fprintf(stderr, "Bad %s: %s\n", what, text);
        exit(1);
    }
    if (x < min || x > max) {
        fprintf(stderr, "%s must be from %d to %d\n", what, min, max);
        exit(1);
    }
    return (int)x;
}

static unsigned char start_value(int pid, int page, int offset) {
    /* da svaka stranica ima neki pocetni sadrzaj */
    return (unsigned char)((pid * 53 + page * 31 + offset * 17 + 0x42) & 0xff);
}

static void print_mem(void) {
    int i;
    /* trenutno stanje okvira u RAM-u */
    printf("frames:");
    for (i = 0; i < m; i++) {
        if (memory_frames[i].used) {
            printf(" %d:[%d:%d]", i, memory_frames[i].pid, memory_frames[i].page);
        } else {
            printf(" %d:[-:-]", i);
        }
    }
    printf("\n");
}

static void print_pt(void) {
    int i, j;
    /* tablice stranica za sve procese */
    for (i = 0; i < n; i++) {
        printf("pt[%d]:", processes[i].pid);
        if (!processes[i].alive) {
            printf(" terminated");
        }
        for (j = 0; j < MAX_PAGES; j++) {
            if (j >= processes[i].num_pages) {
                printf(" %d:NA", j);
            } else if (processes[i].page_table[j].present) {
                printf(" %d:F%d", j, processes[i].page_table[j].frame);
            } else {
                printf(" %d:D", j);
            }
        }
        printf("\n");
    }
}

static void print_clock_state(const char *text) {
    int i;
    printf("%s", text);
    for (i = 0; i < m; i++) {
        if (i > 0) {
            printf("-");
        }
        printf("%d", clock_ref[i]);
    }
    printf("\n");
}

static void print_hand(const char *text) {
    int i;
    printf("%s", text);
    for (i = 0; i < hand * 2; i++) {
        printf(" ");
    }
    printf("^\n");
}

static int alive_count(void) {
    int i;
    int cnt = 0;

    /* prebroji procese koji jos rade */
    for (i = 0; i < n; i++) {
        if (processes[i].alive) {
            cnt++;
        }
    }
    return cnt;
}

/* oslobodi sve okvire od procesa */
static void free_process_frames(Process *p) {
    int i;
    for (i = 0; i < m; i++) {
        if (!memory_frames[i].used || memory_frames[i].pid != p->pid) {
            continue;
        }
        if (memory_frames[i].dirty) {
            memcpy(p->disk[memory_frames[i].page], memory_frames[i].bytes, PAGE_SIZE);
        }
        memory_frames[i].used = false;
        memory_frames[i].dirty = false;
        memory_frames[i].pid = 0;
        memory_frames[i].page = 0;
        memset(memory_frames[i].bytes, 0, PAGE_SIZE);
        clock_ref[i] = 0;
    }
    for (i = 0; i < p->num_pages; i++) {
        p->page_table[i].present = false;
        p->page_table[i].frame = -1;
    }
}

/* clock algoritam */
static int nadji_okvir(void) {
    while (1) {
        if (!memory_frames[hand].used || clock_ref[hand] == 0) {
            int x = hand;
            hand = (hand + 1) % m;
            return x;
        }
        clock_ref[hand] = 0;
        hand = (hand + 1) % m;
    }
}

static void ubaci_stranicu(Process *p, int page, int frame_index) {
    /* ako je okvir zauzet moramo izbaciti staru stranicu */
    if (memory_frames[frame_index].used) {
        int old_pid = memory_frames[frame_index].pid;
        int old_page = memory_frames[frame_index].page;
        if (memory_frames[frame_index].dirty) {
            memcpy(processes[old_pid - 1].disk[old_page], memory_frames[frame_index].bytes, PAGE_SIZE);
        }
        processes[old_pid - 1].page_table[old_page].present = false;
        processes[old_pid - 1].page_table[old_page].frame = -1;
    }

    /* ucitaj novu stranicu u okvir */
    memory_frames[frame_index].used = true;
    memory_frames[frame_index].dirty = false;
    memory_frames[frame_index].pid = p->pid;
    memory_frames[frame_index].page = page;
    memcpy(memory_frames[frame_index].bytes, p->disk[page], PAGE_SIZE);
    p->page_table[page].present = true;
    p->page_table[page].frame = frame_index;
    clock_ref[frame_index] = 1;
}

static void print_request(Process *p, Request *r) {
    /* ispis trenutnog zahtjeva */
    if (r->write) {
        printf("process %d WRITE(0x%02X) LA=0x%02X%02X\n",
               p->pid, r->value, r->page, r->offset);
    } else {
        printf("process %d READ LA=0x%02X%02X\n",
               p->pid, r->page, r->offset);
    }
}

static void access_memory(Process *p, Request r) {
    int frame_index;
    int physical;

    /* ispis sto proces trazi */
    print_request(p, &r);
    if (r.page >= p->num_pages) {
        /* proces dira stranicu koja mu nije dodijeljena */
        printf("MEMORY FAULT: page %d not allocated, terminating process %d\n", r.page, p->pid);
        p->alive = false;
        free_process_frames(p);
        print_mem();
        print_pt();
        printf("\n");
        return;
    }
    print_mem();
    print_pt();
    if (!p->page_table[r.page].present) {
        int victim;
        bool replacing;
        int old_pid = 0;
        int old_page = 0;
        bool old_dirty = false;

        printf("MISS (page %d not in memory)\n", r.page);
        print_clock_state("clock before: ");
        print_hand("hand before:  ");

        /* trazi slobodan okvir ili zrtvu */
        victim = nadji_okvir();
        replacing = memory_frames[victim].used;
        if (replacing) {
            old_pid = memory_frames[victim].pid;
            old_page = memory_frames[victim].page;
            old_dirty = memory_frames[victim].dirty;
        }
        print_clock_state("clock after:  ");
        print_hand("hand after:   ");
        if (replacing) {
            printf("use frame %d:\n", victim);
            if (old_dirty) {
                /* dirty znaci da moramo spremiti nazad */
                printf("- save to disk:   process %d, page %d\n", old_pid, old_page);
            } else {
                printf("- discard clean:  process %d, page %d\n", old_pid, old_page);
            }
            printf("- load from disk: process %d, page %d\n", p->pid, r.page);
        } else {
            printf("use free frame %d:\n", victim);
            printf("- load from disk: process %d, page %d\n", p->pid, r.page);
        }

        ubaci_stranicu(p, r.page, victim);
        print_mem();
        print_pt();
        /* nakon ucitavanja stranice instrukcija ide opet */
        printf("restarting instruction:\n");
        access_memory(p, r);
        return;
    }

    /* ako je pogodak samo prevedi adresu */
    frame_index = p->page_table[r.page].frame;
    clock_ref[frame_index] = 1;
    physical = (frame_index << OFFSET_BITS) | r.offset;
    printf("HIT: frame %d\n", frame_index);
    printf("paging: process %d, page=%d => frame=%d, 0x%02X%02X => 0x%04X\n",
           p->pid, r.page, frame_index, r.page, r.offset, physical);
    if (r.write) {
        /* write ide direktno u okvir pa stranica postaje dirty */
        memory_frames[frame_index].bytes[r.offset] = r.value;
        memory_frames[frame_index].dirty = true;
        printf("save (0x%02X) at 0x%04X\n", r.value, physical);
    } else {
        printf("read value at 0x%04X => 0x%02X\n",
               physical, memory_frames[frame_index].bytes[r.offset]);
    }

    print_clock_state("clock: ");
    print_hand("hand:  ");
    printf("\n");
}

static Request make_request(Process *p) {
    Request r;
    bool bad_page = false;

    /* nekad namjerno generiram krivu stranicu da se vidi fault */
    if (p->requests >= 3 && p->num_pages < MAX_PAGES) {
        bad_page = random_percent(15);
    }
    r.write = random_percent(50);
    r.offset = random_between(0, PAGE_SIZE - 1);
    r.value = (unsigned char)random_between(0, 255);
    if (bad_page) {
        r.page = random_between(p->num_pages, MAX_PAGES - 1);
    } else {
        r.page = random_between(0, p->num_pages - 1);
    }
    p->requests++;
    return r;
}

static void setup(void) {
    int i, j, k;

    /* pocetno stanje svega je prazno */
    memset(processes, 0, sizeof(processes));
    memset(memory_frames, 0, sizeof(memory_frames));
    memset(clock_ref, 0, sizeof(clock_ref));
    hand = 0;
    for (i = 0; i < n; i++) {
        processes[i].pid = i + 1;
        processes[i].num_pages = random_between(2, MAX_PAGES - 1);
        processes[i].alive = true;
        processes[i].requests = 0;
        for (j = 0; j < MAX_PAGES; j++) {
            processes[i].page_table[j].present = false;
            processes[i].page_table[j].frame = -1;
            for (k = 0; k < PAGE_SIZE; k++) {
                /* stavi nesto na "disk" */
                processes[i].disk[j][k] = start_value(processes[i].pid, j, k);
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int steps = 20;
    int i, j;
    unsigned int seed;
    Process *p;
    int zivi;
    int trazeni;
    int br;

    if (argc > 5) {
        print_usage(argv[0]);
        return 1;
    }

    /* ako nema argumenta za seed uzmi trenutno vrijeme */
    seed = (unsigned int)time(NULL);
    if (argc >= 2) {
        seed = (unsigned int)read_number(argv[1], 0, 2147483647, "seed");
    }
    srand(seed);
    if (argc >= 3) {
        steps = read_number(argv[2], 1, 200, "steps");
    }
    if (argc >= 4) {
        n = read_number(argv[3], 1, MAX_PROCESSES, "processes");
    } else {
        n = random_between(3, MAX_PROCESSES);
    }
    if (argc >= 5) {
        m = read_number(argv[4], 2, MAX_FRAMES, "frames");
    } else {
        m = random_between(4, 8);
    }
    setup();
    printf("seed=%u steps=%d processes=%d frames=%d page-size=%dB\n",
           seed, steps, n, m, PAGE_SIZE);
    for (i = 0; i < n; i++) {
        printf("process %d allocated pages: %d\n", processes[i].pid, processes[i].num_pages);
    }
    printf("\n");
    for (i = 0; i < steps; i++) {
        zivi = alive_count();
        if (zivi == 0) {
            printf("All processes terminated.\n");
            break;
        }

        /* izaberi neki zivi proces */
        trazeni = random_between(1, zivi);
        p = NULL;
        br = 0;
        for (j = 0; j < n; j++) {
            if (processes[j].alive) {
                br++;
                if (br == trazeni) {
                    p = &processes[j];
                    break;
                }
            }
        }

        /* jedan pristup memoriji */
        access_memory(p, make_request(p));
    }
    return 0;
}
