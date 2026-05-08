/* File: src/utils.c */

#include <stdio.h>
#include <strings.h>
#include <time.h>

#include "common.h"
#include "utils.h"

void print_banner(void)
{
    puts("==================================================");
    puts(" Hospital Patient Triage & Bed Allocator");
    puts(" OS Lab Semester Project");
    puts("==================================================");
}

void print_phase_status(void)
{
    printf("Build version: %s\n", HOSPITAL_VERSION);
    puts("Final build: IPC, scheduling, pthreads, and memory allocation are active.");
}

const char *bed_type_to_text(BedType bed_type)
{
    if (bed_type == BED_GENERAL) {
        return "GENERAL";
    }

    if (bed_type == BED_ICU) {
        return "ICU";
    }

    if (bed_type == BED_ISOLATION) {
        return "ISOLATION";
    }

    return "UNKNOWN";
}

int bed_type_from_text(const char *text, BedType *out_type)
{
    if (text == NULL || out_type == NULL) {
        return -1;
    }

    if (strcasecmp(text, "GENERAL") == 0) {
        *out_type = BED_GENERAL;
        return 0;
    }

    if (strcasecmp(text, "ICU") == 0) {
        *out_type = BED_ICU;
        return 0;
    }

    if (strcasecmp(text, "ISOLATION") == 0) {
        *out_type = BED_ISOLATION;
        return 0;
    }

    return -1;
}

int make_timestamp(char *buffer, size_t buffer_size)
{
    time_t now = time(NULL);
    struct tm local_now;

    if (localtime_r(&now, &local_now) == NULL) {
        return -1;
    }

    if (strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &local_now) == 0) {
        return -1;
    }

    return 0;
}

int append_log_line(const char *path, const char *tag, const char *message)
{
    char stamp[32];
    FILE *file = fopen(path, "a");

    if (file == NULL) {
        return -1;
    }

    if (make_timestamp(stamp, sizeof(stamp)) != 0) {
        fclose(file);
        return -1;
    }

    fprintf(file, "[%s] %-12s %s\n", stamp, tag, message);
    fclose(file);

    return 0;
}
