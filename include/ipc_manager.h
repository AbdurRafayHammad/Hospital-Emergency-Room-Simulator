/* File: include/ipc_manager.h */

#ifndef IPC_MANAGER_H
#define IPC_MANAGER_H

#include <stddef.h>
#include <sys/ipc.h>

#include "common.h"

#define HOSPITAL_FIFO_PATH "/tmp/hospital_triage_fifo"
#define HOSPITAL_SHM_KEY ((key_t)0x48807123)

int hospital_fifo_create(const char *path);
int hospital_fifo_remove(const char *path);
int hospital_fifo_open_read(const char *path);
int hospital_fifo_open_write(const char *path);

int hospital_shm_create(int *out_shmid, HospitalBoard **out_board);
int hospital_shm_attach(int *out_shmid, HospitalBoard **out_board);
void hospital_board_reset(HospitalBoard *board, pid_t parent_pid);
int hospital_board_detach(HospitalBoard *board);
int hospital_shm_remove(int shmid);

void hospital_board_note_received(HospitalBoard *board, const PatientJob *job);
void hospital_board_note_scheduled(HospitalBoard *board, const PatientJob *job);

int format_patient_record(const PatientJob *job, char *buffer, size_t buffer_size);
int parse_patient_record(const char *line, PatientJob *job);

#endif
