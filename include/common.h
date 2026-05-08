/* File: include/common.h */

#ifndef COMMON_H
#define COMMON_H

#include <sys/types.h>

#define HOSPITAL_VERSION "1.0-final"

#define PATIENT_ID_LEN 32
#define PATIENT_NAME_LEN 64
#define LOG_LINE_LEN 256
#define BOARD_STATUS_LEN 96

#define HOSPITAL_SHM_MAGIC 0x48504244u

typedef enum {
    BED_GENERAL = 1,
    BED_ICU = 2,
    BED_ISOLATION = 3
} BedType;

typedef enum {
    POLICY_FCFS = 1,
    POLICY_PRIORITY = 2
} SchedulePolicy;

typedef struct {
    int patient_no;
    char patient_id[PATIENT_ID_LEN];
    char name[PATIENT_NAME_LEN];
    int age;
    int severity;
    BedType bed_type;
    int care_units;
    unsigned long arrival_order;
} PatientJob;

typedef struct {
    unsigned int magic;
    pid_t parent_pid;
    pid_t scheduler_pid;
    int pipe_generated;
    int fifo_forwarded;
    int total_received;
    int total_scheduled;
    int high_priority_seen;
    int general_waiting;
    int icu_waiting;
    int isolation_waiting;
    char last_patient_id[PATIENT_ID_LEN];
    char last_status[BOARD_STATUS_LEN];
} HospitalBoard;

#endif
