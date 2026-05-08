/* File: src/scheduler.c */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "ipc_manager.h"
#include "queue.h"
#include "scheduler.h"
#include "utils.h"

static int parse_fd_number(const char *text, int *out_fd)
{
    char *end = NULL;
    long value;

    errno = 0;
    value = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    if (value < 0 || value > INT_MAX) {
        return -1;
    }

    *out_fd = (int)value;
    return 0;
}

static HospitalBoard *attach_scheduler_board(void)
{
    int shmid = -1;
    HospitalBoard *board = NULL;

    if (hospital_shm_attach(&shmid, &board) != 0) {
        perror("scheduler shared memory attach");
        append_log_line("logs/schedule_log.txt", "SHM-CHILD",
                        "scheduler could not attach shared memory");
        return NULL;
    }

    if (board->magic != HOSPITAL_SHM_MAGIC) {
        puts("scheduler found invalid shared memory board");
        hospital_board_detach(board);
        return NULL;
    }

    board->scheduler_pid = getpid();
    snprintf(board->last_status, sizeof(board->last_status),
             "scheduler child attached");

    printf("scheduler child attached shared memory shmid=%d\n", shmid);
    append_log_line("logs/schedule_log.txt", "SHM-CHILD",
                    "scheduler attached shared memory");

    return board;
}

static void log_scheduled_patient(const PatientJob *job, int slot)
{
    char message[LOG_LINE_LEN];

    snprintf(message, sizeof(message),
             "slot=%d patient=%s severity=%d bed=%s order=%lu",
             slot,
             job->patient_id,
             job->severity,
             bed_type_to_text(job->bed_type),
             job->arrival_order);

    append_log_line("logs/schedule_log.txt", "SCHEDULE", message);
}

static int read_pipe_records(int read_fd, PatientQueue *queue, int *accepted)
{
    FILE *stream;
    char line[LOG_LINE_LEN];

    stream = fdopen(read_fd, "r");

    if (stream == NULL) {
        perror("scheduler fdopen");
        close(read_fd);
        return -1;
    }

    while (fgets(line, sizeof(line), stream) != NULL) {
        PatientJob job;

        if (parse_patient_record(line, &job) != 0) {
            append_log_line("logs/schedule_log.txt", "PIPE-BAD",
                            "scheduler rejected malformed pipe record");
            continue;
        }

        if (patient_queue_push_priority(queue, &job) != 0) {
            fclose(stream);
            return -1;
        }

        *accepted = *accepted + 1;

        printf("scheduler child queued patient=%s severity=%d bed=%s\n",
               job.patient_id,
               job.severity,
               bed_type_to_text(job.bed_type));
    }

    fclose(stream);
    return 0;
}

int scheduler_worker_main_from_fd(int read_fd)
{
    PatientQueue queue;
    PatientJob job;
    HospitalBoard *board;
    int accepted = 0;
    int slot = 1;

    setvbuf(stdout, NULL, _IOLBF, 0);

    patient_queue_init(&queue);
    board = attach_scheduler_board();

    append_log_line("logs/schedule_log.txt", "CHILD", "scheduler worker started");

    if (read_pipe_records(read_fd, &queue, &accepted) != 0) {
        patient_queue_destroy(&queue);

        if (board != NULL) {
            hospital_board_detach(board);
        }

        append_log_line("logs/schedule_log.txt", "CHILD", "scheduler worker failed");
        return 1;
    }

    printf("\nScheduler child received %d patient records\n", accepted);
    puts("Priority scheduling order inside execv child:");

    while (patient_queue_pop(&queue, &job)) {
        printf("  slot=%d patient=%s name=%s severity=%d bed=%s units=%d order=%lu\n",
               slot,
               job.patient_id,
               job.name,
               job.severity,
               bed_type_to_text(job.bed_type),
               job.care_units,
               job.arrival_order);

        hospital_board_note_scheduled(board, &job);
        log_scheduled_patient(&job, slot);
        slot++;
    }

    if (board != NULL) {
        snprintf(board->last_status, sizeof(board->last_status),
                 "scheduler child completed");
        hospital_board_detach(board);
    }

    append_log_line("logs/schedule_log.txt", "CHILD", "scheduler worker completed");
    return 0;
}

int main(int argc, char **argv)
{
    int read_fd;

    if (argc != 2) {
        puts("Usage: ./scheduler_worker PIPE_READ_FD");
        return 1;
    }

    if (parse_fd_number(argv[1], &read_fd) != 0) {
        puts("Error: invalid pipe read fd");
        return 1;
    }

    return scheduler_worker_main_from_fd(read_fd);
}
