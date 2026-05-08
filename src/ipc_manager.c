/* File: src/ipc_manager.c */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ipc_manager.h"
#include "utils.h"

static int parse_int_field(const char *text, int *out_value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    if (parsed < INT_MIN || parsed > INT_MAX) {
        return -1;
    }

    *out_value = (int)parsed;
    return 0;
}

int hospital_fifo_create(const char *path)
{
    struct stat info;

    if (mkfifo(path, 0666) == 0) {
        return 0;
    }

    if (errno != EEXIST) {
        return -1;
    }

    if (stat(path, &info) != 0) {
        return -1;
    }

    if (!S_ISFIFO(info.st_mode)) {
        errno = EEXIST;
        return -1;
    }

    return 0;
}

int hospital_fifo_remove(const char *path)
{
    if (unlink(path) == 0) {
        return 0;
    }

    if (errno == ENOENT) {
        return 0;
    }

    return -1;
}

int hospital_fifo_open_read(const char *path)
{
    return open(path, O_RDONLY);
}

int hospital_fifo_open_write(const char *path)
{
    return open(path, O_WRONLY);
}

int format_patient_record(const PatientJob *job, char *buffer, size_t buffer_size)
{
    char stamp[32];
    int written;

    if (job == NULL || buffer == NULL) {
        return -1;
    }

    if (make_timestamp(stamp, sizeof(stamp)) != 0) {
        return -1;
    }

    written = snprintf(buffer, buffer_size, "%s|%s|%s|%d|%d|%s|%d\n",
                       stamp,
                       job->patient_id,
                       job->name,
                       job->age,
                       job->severity,
                       bed_type_to_text(job->bed_type),
                       job->care_units);

    if (written < 0 || (size_t)written >= buffer_size) {
        return -1;
    }

    return 0;
}

int parse_patient_record(const char *line, PatientJob *job)
{
    char copy[LOG_LINE_LEN];
    char *fields[7];
    char *token;
    char *saveptr = NULL;
    size_t field_count = 0;

    if (line == NULL || job == NULL) {
        return -1;
    }

    snprintf(copy, sizeof(copy), "%s", line);
    copy[strcspn(copy, "\r\n")] = '\0';

    token = strtok_r(copy, "|", &saveptr);

    while (token != NULL && field_count < 7) {
        fields[field_count] = token;
        field_count++;
        token = strtok_r(NULL, "|", &saveptr);
    }

    if (field_count != 7 || token != NULL) {
        return -1;
    }

    memset(job, 0, sizeof(*job));

    snprintf(job->patient_id, sizeof(job->patient_id), "%s", fields[1]);
    snprintf(job->name, sizeof(job->name), "%s", fields[2]);

    if (parse_int_field(fields[3], &job->age) != 0) {
        return -1;
    }

    if (parse_int_field(fields[4], &job->severity) != 0) {
        return -1;
    }

    if (bed_type_from_text(fields[5], &job->bed_type) != 0) {
        return -1;
    }

    if (parse_int_field(fields[6], &job->care_units) != 0) {
        return -1;
    }

    if (job->age < 0 || job->age > 120) {
        return -1;
    }

    if (job->severity < 1 || job->severity > 5) {
        return -1;
    }

    if (job->care_units <= 0) {
        return -1;
    }

    return 0;
}

static int *bed_wait_counter(HospitalBoard *board, BedType bed_type)
{
    if (bed_type == BED_GENERAL) {
        return &board->general_waiting;
    }

    if (bed_type == BED_ICU) {
        return &board->icu_waiting;
    }

    if (bed_type == BED_ISOLATION) {
        return &board->isolation_waiting;
    }

    return NULL;
}

int hospital_shm_create(int *out_shmid, HospitalBoard **out_board)
{
    int shmid;
    void *address;

    if (out_shmid == NULL || out_board == NULL) {
        errno = EINVAL;
        return -1;
    }

    shmid = shmget(HOSPITAL_SHM_KEY, sizeof(HospitalBoard), IPC_CREAT | 0666);

    if (shmid < 0) {
        return -1;
    }

    address = shmat(shmid, NULL, 0);

    if (address == (void *)-1) {
        return -1;
    }

    *out_shmid = shmid;
    *out_board = address;
    return 0;
}

int hospital_shm_attach(int *out_shmid, HospitalBoard **out_board)
{
    int shmid;
    void *address;

    if (out_shmid == NULL || out_board == NULL) {
        errno = EINVAL;
        return -1;
    }

    shmid = shmget(HOSPITAL_SHM_KEY, sizeof(HospitalBoard), 0666);

    if (shmid < 0) {
        return -1;
    }

    address = shmat(shmid, NULL, 0);

    if (address == (void *)-1) {
        return -1;
    }

    *out_shmid = shmid;
    *out_board = address;
    return 0;
}

void hospital_board_reset(HospitalBoard *board, pid_t parent_pid)
{
    if (board == NULL) {
        return;
    }

    memset(board, 0, sizeof(*board));
    board->magic = HOSPITAL_SHM_MAGIC;
    board->parent_pid = parent_pid;
    snprintf(board->last_status, sizeof(board->last_status),
             "shared board created");
}

int hospital_board_detach(HospitalBoard *board)
{
    if (board == NULL) {
        return 0;
    }

    return shmdt(board);
}

int hospital_shm_remove(int shmid)
{
    return shmctl(shmid, IPC_RMID, NULL);
}

void hospital_board_note_received(HospitalBoard *board, const PatientJob *job)
{
    int *counter;

    if (board == NULL || job == NULL || board->magic != HOSPITAL_SHM_MAGIC) {
        return;
    }

    board->total_received++;

    if (job->severity <= 2) {
        board->high_priority_seen++;
    }

    counter = bed_wait_counter(board, job->bed_type);

    if (counter != NULL) {
        *counter = *counter + 1;
    }

    snprintf(board->last_patient_id, sizeof(board->last_patient_id),
             "%s", job->patient_id);
    snprintf(board->last_status, sizeof(board->last_status),
             "received patient %s", job->patient_id);
}

void hospital_board_note_scheduled(HospitalBoard *board, const PatientJob *job)
{
    int *counter;

    if (board == NULL || job == NULL || board->magic != HOSPITAL_SHM_MAGIC) {
        return;
    }

    board->total_scheduled++;

    counter = bed_wait_counter(board, job->bed_type);

    if (counter != NULL && *counter > 0) {
        *counter = *counter - 1;
    }

    snprintf(board->last_patient_id, sizeof(board->last_patient_id),
             "%s", job->patient_id);
    snprintf(board->last_status, sizeof(board->last_status),
             "scheduled patient %s", job->patient_id);
}
