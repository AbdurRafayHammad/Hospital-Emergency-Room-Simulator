/* File: src/queue.c */

#include <stdlib.h>
#include <string.h>

#include "queue.h"

struct PatientNode {
    PatientJob job;
    struct PatientNode *next;
};

static PatientNode *make_node(const PatientJob *job, unsigned long order)
{
    PatientNode *node = malloc(sizeof(*node));

    if (node == NULL) {
        return NULL;
    }

    memcpy(&node->job, job, sizeof(node->job));
    node->job.arrival_order = order;
    node->next = NULL;

    return node;
}

static int current_patient_goes_first(const PatientJob *current,
                                      const PatientJob *incoming)
{
    if (current->severity < incoming->severity) {
        return 1;
    }

    if (current->severity == incoming->severity &&
        current->arrival_order <= incoming->arrival_order) {
        return 1;
    }

    return 0;
}

void patient_queue_init(PatientQueue *queue)
{
    queue->head = NULL;
    queue->tail = NULL;
    queue->length = 0;
    queue->next_order = 1;
}

int patient_queue_push_fcfs(PatientQueue *queue, const PatientJob *job)
{
    PatientNode *node = make_node(job, queue->next_order);

    if (node == NULL) {
        return -1;
    }

    queue->next_order++;

    if (queue->tail == NULL) {
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }

    queue->length++;
    return 0;
}

int patient_queue_push_priority(PatientQueue *queue, const PatientJob *job)
{
    PatientNode *node = make_node(job, queue->next_order);
    PatientNode *previous = NULL;
    PatientNode *walk = queue->head;

    if (node == NULL) {
        return -1;
    }

    queue->next_order++;

    while (walk != NULL && current_patient_goes_first(&walk->job, &node->job)) {
        previous = walk;
        walk = walk->next;
    }

    if (previous == NULL) {
        node->next = queue->head;
        queue->head = node;
    } else {
        previous->next = node;
        node->next = walk;
    }

    if (node->next == NULL) {
        queue->tail = node;
    }

    queue->length++;
    return 0;
}

int patient_queue_pop(PatientQueue *queue, PatientJob *out_job)
{
    PatientNode *old_head;

    if (queue->head == NULL) {
        return 0;
    }

    old_head = queue->head;
    memcpy(out_job, &old_head->job, sizeof(*out_job));

    queue->head = old_head->next;

    if (queue->head == NULL) {
        queue->tail = NULL;
    }

    free(old_head);
    queue->length--;

    return 1;
}

int patient_queue_is_empty(const PatientQueue *queue)
{
    return queue->length == 0;
}

size_t patient_queue_size(const PatientQueue *queue)
{
    return queue->length;
}

void patient_queue_destroy(PatientQueue *queue)
{
    PatientJob unused;

    while (patient_queue_pop(queue, &unused)) {
        /* drain queue */
    }
}
