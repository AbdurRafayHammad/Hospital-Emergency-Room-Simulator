/* File: src/patient_simulator.c */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "ipc_manager.h"
#include "utils.h"

static void show_usage(void)
{
    puts("Usage:");
    puts("  ./patient_simulator");
    puts("  ./patient_simulator 8");
}

static int parse_count(const char *text, int *out_count)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }

    if (parsed <= 0 || parsed > INT_MAX) {
        return -1;
    }

    *out_count = (int)parsed;
    return 0;
}

static void small_delay_ms(long milliseconds)
{
    struct timespec wait_time;

    wait_time.tv_sec = milliseconds / 1000;
    wait_time.tv_nsec = (milliseconds % 1000) * 1000000L;

    while (nanosleep(&wait_time, &wait_time) != 0 && errno == EINTR) {
        /* keep sleeping */
    }
}

static BedType choose_bed_type(int number)
{
    if (number % 3 == 0) {
        return BED_ISOLATION;
    }

    if (number % 2 == 0) {
        return BED_ICU;
    }

    return BED_GENERAL;
}

static PatientJob make_patient(int number)
{
    PatientJob job;

    memset(&job, 0, sizeof(job));

    job.patient_no = number;
    snprintf(job.patient_id, sizeof(job.patient_id), "SIM-%04d", number);
    snprintf(job.name, sizeof(job.name), "SimPatient_%02d", number);
    job.age = 19 + ((number * 9) % 58);
    job.severity = 1 + ((number * 2) % 5);
    job.bed_type = choose_bed_type(number);
    job.care_units = 5 + ((number * 4) % 20);

    return job;
}

int main(int argc, char **argv)
{
    int count = 5;
    int fd;
    FILE *stream;
    int i;

    if (argc > 2) {
        show_usage();
        return 1;
    }

    if (argc == 2 && parse_count(argv[1], &count) != 0) {
        puts("Error: patient count must be a positive number.");
        return 1;
    }

    print_banner();
    printf("Patient simulator sending %d records through %s\n",
           count, HOSPITAL_FIFO_PATH);

    fd = hospital_fifo_open_write(HOSPITAL_FIFO_PATH);

    if (fd < 0) {
        perror("open FIFO for write");
        puts("Start the hospital reader first:");
        puts("  ./hospital_system --fifo 5");
        return 1;
    }

    stream = fdopen(fd, "w");

    if (stream == NULL) {
        perror("fdopen");
        close(fd);
        return 1;
    }

    for (i = 1; i <= count; i++) {
        PatientJob job = make_patient(i);
        char line[LOG_LINE_LEN];

        if (format_patient_record(&job, line, sizeof(line)) != 0) {
            puts("Error: could not format patient record.");
            fclose(stream);
            return 1;
        }

        if (fputs(line, stream) == EOF) {
            perror("write FIFO");
            fclose(stream);
            return 1;
        }

        fflush(stream);

        printf("sent patient=%s severity=%d bed=%s units=%d\n",
               job.patient_id,
               job.severity,
               bed_type_to_text(job.bed_type),
               job.care_units);

        small_delay_ms(250);
    }

    fclose(stream);
    puts("patient simulator completed");

    return 0;
}
