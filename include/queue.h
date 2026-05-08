/* File: include/queue.h */

#ifndef QUEUE_H
#define QUEUE_H

#include <stddef.h>

#include "common.h"

typedef struct PatientNode PatientNode;

typedef struct {
    PatientNode *head;
    PatientNode *tail;
    size_t length;
    unsigned long next_order;
} PatientQueue;

void patient_queue_init(PatientQueue *queue);
int patient_queue_push_fcfs(PatientQueue *queue, const PatientJob *job);
int patient_queue_push_priority(PatientQueue *queue, const PatientJob *job);
int patient_queue_pop(PatientQueue *queue, PatientJob *out_job);
int patient_queue_is_empty(const PatientQueue *queue);
size_t patient_queue_size(const PatientQueue *queue);
void patient_queue_destroy(PatientQueue *queue);

#endif
