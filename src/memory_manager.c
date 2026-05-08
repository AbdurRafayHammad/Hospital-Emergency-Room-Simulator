/* File: src/memory_manager.c */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <unistd.h>

#include "common.h"
#include "memory_manager.h"
#include "utils.h"

#define WARD_UNITS 96
#define PAGE_SIZE_UNITS 8
#define FRAME_COUNT 6
#define MAX_REQUESTS 200

typedef struct WardBlock {
    int start;
    int size;
    int is_free;
    PatientJob owner;
    struct WardBlock *next;
} WardBlock;

typedef struct {
    int attempts;
    int successes;
    int failures;
    int frees;
    int coalesces;
    int peak_external_frag;
    int page_faults;
    int page_hits;
} MemoryStats;

typedef struct {
    int used;
    char patient_id[PATIENT_ID_LEN];
    int page_no;
    unsigned long last_touch;
} PageFrame;

typedef struct {
    PageFrame frames[FRAME_COUNT];
    unsigned long clock_tick;
} PagingDesk;

const char *allocation_strategy_name(AllocationStrategy strategy)
{
    if (strategy == ALLOC_FIRST_FIT) {
        return "FIRST_FIT";
    }

    if (strategy == ALLOC_BEST_FIT) {
        return "BEST_FIT";
    }

    if (strategy == ALLOC_WORST_FIT) {
        return "WORST_FIT";
    }

    return "UNKNOWN";
}

int parse_allocation_strategy(const char *text, AllocationStrategy *out_strategy)
{
    if (text == NULL || out_strategy == NULL) {
        return -1;
    }

    if (strcasecmp(text, "first") == 0 || strcasecmp(text, "first-fit") == 0) {
        *out_strategy = ALLOC_FIRST_FIT;
        return 0;
    }

    if (strcasecmp(text, "best") == 0 || strcasecmp(text, "best-fit") == 0) {
        *out_strategy = ALLOC_BEST_FIT;
        return 0;
    }

    if (strcasecmp(text, "worst") == 0 || strcasecmp(text, "worst-fit") == 0) {
        *out_strategy = ALLOC_WORST_FIT;
        return 0;
    }

    return -1;
}

static void memory_log(const char *tag, const char *message)
{
    if (append_log_line("logs/memory_log.txt", tag, message) != 0) {
        fprintf(stderr, "memory log write failed\n");
    }
}

static WardBlock *make_block(int start, int size, int is_free)
{
    WardBlock *block = malloc(sizeof(*block));

    if (block == NULL) {
        return NULL;
    }

    memset(block, 0, sizeof(*block));
    block->start = start;
    block->size = size;
    block->is_free = is_free;
    block->next = NULL;

    return block;
}

static void destroy_blocks(WardBlock *head)
{
    while (head != NULL) {
        WardBlock *next = head->next;
        free(head);
        head = next;
    }
}

static PatientJob make_memory_patient(int number)
{
    PatientJob job;

    memset(&job, 0, sizeof(job));

    job.patient_no = number;
    snprintf(job.patient_id, sizeof(job.patient_id), "MEM-%04d", number);
    snprintf(job.name, sizeof(job.name), "MemoryPatient_%02d", number);
    job.age = 22 + ((number * 7) % 55);
    job.severity = 1 + ((number * 3) % 5);

    if (number % 5 == 0) {
        job.bed_type = BED_ICU;
    } else if (number % 3 == 0) {
        job.bed_type = BED_ISOLATION;
    } else {
        job.bed_type = BED_GENERAL;
    }

    job.care_units = 6 + ((number * 5) % 23);
    return job;
}

static WardBlock *choose_first_fit(WardBlock *head, int units)
{
    WardBlock *walk = head;

    while (walk != NULL) {
        if (walk->is_free && walk->size >= units) {
            return walk;
        }

        walk = walk->next;
    }

    return NULL;
}

static WardBlock *choose_best_fit(WardBlock *head, int units)
{
    WardBlock *walk = head;
    WardBlock *best = NULL;
    int best_waste = INT_MAX;

    while (walk != NULL) {
        if (walk->is_free && walk->size >= units) {
            int waste = walk->size - units;

            if (waste < best_waste) {
                best = walk;
                best_waste = waste;
            }
        }

        walk = walk->next;
    }

    return best;
}

static WardBlock *choose_worst_fit(WardBlock *head, int units)
{
    WardBlock *walk = head;
    WardBlock *worst = NULL;
    int worst_size = -1;

    while (walk != NULL) {
        if (walk->is_free && walk->size >= units && walk->size > worst_size) {
            worst = walk;
            worst_size = walk->size;
        }

        walk = walk->next;
    }

    return worst;
}

static WardBlock *choose_block(WardBlock *head, int units,
                               AllocationStrategy strategy)
{
    if (strategy == ALLOC_FIRST_FIT) {
        return choose_first_fit(head, units);
    }

    if (strategy == ALLOC_BEST_FIT) {
        return choose_best_fit(head, units);
    }

    return choose_worst_fit(head, units);
}

static int external_fragmentation(WardBlock *head)
{
    int total_free = 0;
    int largest_free = 0;
    WardBlock *walk = head;

    while (walk != NULL) {
        if (walk->is_free) {
            total_free += walk->size;

            if (walk->size > largest_free) {
                largest_free = walk->size;
            }
        }

        walk = walk->next;
    }

    if (total_free == 0) {
        return 0;
    }

    return total_free - largest_free;
}

static void update_peak_fragmentation(WardBlock *head, MemoryStats *stats)
{
    int current = external_fragmentation(head);

    if (current > stats->peak_external_frag) {
        stats->peak_external_frag = current;
    }
}

static void print_blocks(WardBlock *head)
{
    WardBlock *walk = head;

    printf("  blocks:");

    while (walk != NULL) {
        if (walk->is_free) {
            printf(" [FREE start=%d size=%d]", walk->start, walk->size);
        } else {
            printf(" [USED %s start=%d size=%d]",
                   walk->owner.patient_id,
                   walk->start,
                   walk->size);
        }

        walk = walk->next;
    }

    putchar('\n');
}

static int allocate_patient(WardBlock *head, const PatientJob *job,
                            AllocationStrategy strategy, MemoryStats *stats)
{
    WardBlock *chosen;
    char message[LOG_LINE_LEN];

    stats->attempts++;
    chosen = choose_block(head, job->care_units, strategy);

    if (chosen == NULL) {
        stats->failures++;

        snprintf(message, sizeof(message),
                 "%s failed patient=%s units=%d external_frag=%d",
                 allocation_strategy_name(strategy),
                 job->patient_id,
                 job->care_units,
                 external_fragmentation(head));

        memory_log("ALLOC-FAIL", message);
        return -1;
    }

    if (chosen->size > job->care_units) {
        WardBlock *remainder = make_block(chosen->start + job->care_units,
                                          chosen->size - job->care_units, 1);

        if (remainder == NULL) {
            stats->failures++;
            return -1;
        }

        remainder->next = chosen->next;
        chosen->next = remainder;
        chosen->size = job->care_units;
    }

    chosen->is_free = 0;
    memcpy(&chosen->owner, job, sizeof(chosen->owner));
    stats->successes++;

    snprintf(message, sizeof(message),
             "%s allocated patient=%s units=%d start=%d",
             allocation_strategy_name(strategy),
             job->patient_id,
             job->care_units,
             chosen->start);

    memory_log("ALLOC", message);
    update_peak_fragmentation(head, stats);

    return 0;
}

static void coalesce_blocks(WardBlock *head, MemoryStats *stats)
{
    WardBlock *walk = head;

    while (walk != NULL && walk->next != NULL) {
        if (walk->is_free && walk->next->is_free) {
            WardBlock *old_next = walk->next;

            walk->size += old_next->size;
            walk->next = old_next->next;
            free(old_next);
            stats->coalesces++;
        } else {
            walk = walk->next;
        }
    }
}

static int free_patient(WardBlock *head, const char *patient_id,
                        MemoryStats *stats)
{
    WardBlock *walk = head;
    char message[LOG_LINE_LEN];

    while (walk != NULL) {
        if (!walk->is_free &&
            strncmp(walk->owner.patient_id, patient_id, PATIENT_ID_LEN) == 0) {
            walk->is_free = 1;
            memset(&walk->owner, 0, sizeof(walk->owner));
            stats->frees++;

            snprintf(message, sizeof(message), "released patient=%s", patient_id);
            memory_log("FREE", message);

            coalesce_blocks(head, stats);
            update_peak_fragmentation(head, stats);
            return 0;
        }

        walk = walk->next;
    }

    return -1;
}

static void remove_active_id(char active_ids[][PATIENT_ID_LEN],
                             int *active_count, int index)
{
    int i;

    for (i = index; i < *active_count - 1; i++) {
        memcpy(active_ids[i], active_ids[i + 1], PATIENT_ID_LEN);
    }

    *active_count = *active_count - 1;
}

static int find_page(PagingDesk *paging, const char *patient_id, int page_no)
{
    int i;

    for (i = 0; i < FRAME_COUNT; i++) {
        if (paging->frames[i].used &&
            paging->frames[i].page_no == page_no &&
            strncmp(paging->frames[i].patient_id, patient_id,
                    PATIENT_ID_LEN) == 0) {
            return i;
        }
    }

    return -1;
}

static int choose_frame(PagingDesk *paging)
{
    int i;
    int oldest = 0;

    for (i = 0; i < FRAME_COUNT; i++) {
        if (!paging->frames[i].used) {
            return i;
        }

        if (paging->frames[i].last_touch < paging->frames[oldest].last_touch) {
            oldest = i;
        }
    }

    return oldest;
}

static void access_page(PagingDesk *paging, const PatientJob *job,
                        int page_no, MemoryStats *stats)
{
    int frame = find_page(paging, job->patient_id, page_no);
    char message[LOG_LINE_LEN];

    paging->clock_tick++;

    if (frame >= 0) {
        stats->page_hits++;
        paging->frames[frame].last_touch = paging->clock_tick;

        snprintf(message, sizeof(message),
                 "hit patient=%s page=%d frame=%d",
                 job->patient_id, page_no, frame);

        memory_log("PAGE-HIT", message);
        return;
    }

    frame = choose_frame(paging);
    stats->page_faults++;

    paging->frames[frame].used = 1;
    paging->frames[frame].page_no = page_no;
    paging->frames[frame].last_touch = paging->clock_tick;
    snprintf(paging->frames[frame].patient_id,
             sizeof(paging->frames[frame].patient_id),
             "%s", job->patient_id);

    snprintf(message, sizeof(message),
             "fault patient=%s page=%d loaded_frame=%d",
             job->patient_id, page_no, frame);

    memory_log("PAGE-FAULT", message);
}

static void simulate_paging(PagingDesk *paging, const PatientJob *job,
                            MemoryStats *stats)
{
    int pages = (job->care_units + PAGE_SIZE_UNITS - 1) / PAGE_SIZE_UNITS;
    int page;

    printf("  paging patient=%s units=%d pages=%d\n",
           job->patient_id, job->care_units, pages);

    for (page = 0; page < pages; page++) {
        access_page(paging, job, page, stats);
    }

    if (pages > 1) {
        access_page(paging, job, 0, stats);
        access_page(paging, job, pages - 1, stats);
    }
}

static void run_mmap_bonus(void)
{
    long page_size = sysconf(_SC_PAGESIZE);
    int fd;
    void *mapped;

    if (page_size <= 0) {
        return;
    }

    fd = open("/dev/zero", O_RDWR);

    if (fd < 0) {
        memory_log("MMAP", "open /dev/zero failed");
        return;
    }

    mapped = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE, fd, 0);
    close(fd);

    if (mapped == MAP_FAILED) {
        memory_log("MMAP", "mmap failed");
        return;
    }

    snprintf((char *)mapped, (size_t)page_size,
             "temporary mmap ward board, page_size=%ld", page_size);

    printf("  mmap bonus mapped %ld bytes: %s\n",
           page_size, (char *)mapped);

    memory_log("MMAP", "temporary mmap ward board created and released");
    munmap(mapped, (size_t)page_size);
}

static void print_summary(const MemoryStats *stats, WardBlock *head)
{
    printf("\nMemory demo summary:\n");
    printf("  attempts=%d successes=%d failures=%d frees=%d coalesces=%d\n",
           stats->attempts,
           stats->successes,
           stats->failures,
           stats->frees,
           stats->coalesces);
    printf("  current_external_fragmentation=%d peak_external_fragmentation=%d\n",
           external_fragmentation(head),
           stats->peak_external_frag);
    printf("  page_faults=%d page_hits=%d\n",
           stats->page_faults,
           stats->page_hits);
}

int run_memory_demo(AllocationStrategy strategy, int request_count)
{
    WardBlock *head;
    MemoryStats stats;
    PagingDesk paging;
    char (*active_ids)[PATIENT_ID_LEN];
    int active_count = 0;
    int i;

    if (request_count <= 0 || request_count > MAX_REQUESTS) {
        puts("Error: memory demo request count must be from 1 to 200.");
        return 1;
    }

    memset(&stats, 0, sizeof(stats));
    memset(&paging, 0, sizeof(paging));

    active_ids = calloc((size_t)request_count, sizeof(*active_ids));

    if (active_ids == NULL) {
        perror("calloc active ids");
        return 1;
    }

    head = make_block(0, WARD_UNITS, 1);

    if (head == NULL) {
        free(active_ids);
        perror("make initial memory block");
        return 1;
    }

    printf("\nMemory allocator strategy: %s\n",
           allocation_strategy_name(strategy));
    printf("Ward memory units: %d, page size: %d units, frames: %d\n",
           WARD_UNITS, PAGE_SIZE_UNITS, FRAME_COUNT);

    memory_log("START", allocation_strategy_name(strategy));
    print_blocks(head);

    for (i = 1; i <= request_count; i++) {
        PatientJob job = make_memory_patient(i);

        printf("\nrequest patient=%s severity=%d bed=%s units=%d\n",
               job.patient_id,
               job.severity,
               bed_type_to_text(job.bed_type),
               job.care_units);

        simulate_paging(&paging, &job, &stats);

        if (allocate_patient(head, &job, strategy, &stats) == 0) {
            snprintf(active_ids[active_count], PATIENT_ID_LEN,
                     "%s", job.patient_id);
            active_count++;
        } else {
            printf("  allocation failed for %s\n", job.patient_id);
        }

        if (i % 4 == 0 && active_count > 1) {
            printf("  discharge creates hole: %s\n", active_ids[0]);
            free_patient(head, active_ids[0], &stats);
            remove_active_id(active_ids, &active_count, 0);
        }

        print_blocks(head);
        printf("  external fragmentation now=%d\n",
               external_fragmentation(head));
    }

    while (active_count > 0) {
        free_patient(head, active_ids[active_count - 1], &stats);
        active_count--;
    }

    coalesce_blocks(head, &stats);
    print_blocks(head);
    print_summary(&stats, head);
    run_mmap_bonus();

    memory_log("END", allocation_strategy_name(strategy));

    destroy_blocks(head);
    free(active_ids);
    return 0;
}

int run_memory_demo_all(int request_count)
{
    int result = 0;

    result |= run_memory_demo(ALLOC_FIRST_FIT, request_count);
    result |= run_memory_demo(ALLOC_BEST_FIT, request_count);
    result |= run_memory_demo(ALLOC_WORST_FIT, request_count);

    return result;
}
