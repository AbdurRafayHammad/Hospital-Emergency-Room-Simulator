/* File: src/thread_manager.c */

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "queue.h"
#include "thread_manager.h"
#include "utils.h"

#define MAX_NURSES 8

typedef struct ThreadHospitalState ThreadHospitalState;

typedef struct {
    int nurse_no;
    ThreadHospitalState *state;
} NurseArgs;

struct ThreadHospitalState {
    PatientQueue incoming_queue;
    PatientQueue scheduled_queue;

    pthread_mutex_t incoming_lock;
    pthread_mutex_t scheduled_lock;
    pthread_mutex_t stats_lock;
    pthread_cond_t incoming_ready;
    sem_t patient_ready;

    int patient_target;
    int nurse_count;
    int reception_done;
    int scheduler_done;

    int produced_count;
    int scheduled_count;
    int treated_count;
};

static void small_delay_ms(long milliseconds)
{
    struct timespec wait_time;

    wait_time.tv_sec = milliseconds / 1000;
    wait_time.tv_nsec = (milliseconds % 1000) * 1000000L;

    while (nanosleep(&wait_time, &wait_time) != 0 && errno == EINTR) {
        /* continue sleeping */
    }
}

static BedType choose_thread_bed(int number)
{
    if (number % 4 == 0) {
        return BED_ICU;
    }

    if (number % 3 == 0) {
        return BED_ISOLATION;
    }

    return BED_GENERAL;
}

static PatientJob make_thread_patient(int number)
{
    PatientJob job;

    memset(&job, 0, sizeof(job));

    job.patient_no = number;
    snprintf(job.patient_id, sizeof(job.patient_id), "THR-%04d", number);
    snprintf(job.name, sizeof(job.name), "ThreadPatient_%02d", number);
    job.age = 18 + ((number * 11) % 60);
    job.severity = 1 + ((number * 4) % 5);
    job.bed_type = choose_thread_bed(number);
    job.care_units = 4 + ((number * 5) % 18);

    return job;
}

static void log_thread_event(const char *tag, const PatientJob *job)
{
    char message[LOG_LINE_LEN];

    snprintf(message, sizeof(message),
             "patient=%s severity=%d bed=%s units=%d",
             job->patient_id,
             job->severity,
             bed_type_to_text(job->bed_type),
             job->care_units);

    append_log_line("logs/schedule_log.txt", tag, message);
}

static int wait_patient_sem(sem_t *sem)
{
    while (sem_wait(sem) != 0) {
        if (errno != EINTR) {
            return -1;
        }
    }

    return 0;
}

static void *receptionist_thread(void *arg)
{
    ThreadHospitalState *state = arg;
    int i;

    for (i = 1; i <= state->patient_target; i++) {
        PatientJob job = make_thread_patient(i);

        pthread_mutex_lock(&state->incoming_lock);

        if (patient_queue_push_fcfs(&state->incoming_queue, &job) != 0) {
            puts("receptionist could not queue patient");
            state->reception_done = 1;
            pthread_cond_broadcast(&state->incoming_ready);
            pthread_mutex_unlock(&state->incoming_lock);
            return NULL;
        }

        state->produced_count++;
        printf("receptionist admitted patient=%s severity=%d bed=%s\n",
               job.patient_id,
               job.severity,
               bed_type_to_text(job.bed_type));

        log_thread_event("RECEPTION", &job);
        pthread_cond_signal(&state->incoming_ready);
        pthread_mutex_unlock(&state->incoming_lock);

        small_delay_ms(120);
    }

    pthread_mutex_lock(&state->incoming_lock);
    state->reception_done = 1;
    pthread_cond_broadcast(&state->incoming_ready);
    pthread_mutex_unlock(&state->incoming_lock);

    puts("receptionist finished admissions");
    return NULL;
}

static int move_one_patient_to_priority(ThreadHospitalState *state,
                                        const PatientJob *job)
{
    pthread_mutex_lock(&state->scheduled_lock);

    if (patient_queue_push_priority(&state->scheduled_queue, job) != 0) {
        pthread_mutex_unlock(&state->scheduled_lock);
        return -1;
    }

    pthread_mutex_unlock(&state->scheduled_lock);

    pthread_mutex_lock(&state->stats_lock);
    state->scheduled_count++;
    pthread_mutex_unlock(&state->stats_lock);

    printf("scheduler ranked patient=%s severity=%d\n",
           job->patient_id,
           job->severity);

    log_thread_event("THREAD-SCH", job);
    return 0;
}

static void *scheduler_thread(void *arg)
{
    ThreadHospitalState *state = arg;

    while (1) {
        PatientJob job;
        int has_patient;

        pthread_mutex_lock(&state->incoming_lock);

        while (patient_queue_is_empty(&state->incoming_queue) &&
               !state->reception_done) {
            pthread_cond_wait(&state->incoming_ready, &state->incoming_lock);
        }

        if (patient_queue_is_empty(&state->incoming_queue) &&
            state->reception_done) {
            pthread_mutex_unlock(&state->incoming_lock);
            break;
        }

        has_patient = patient_queue_pop(&state->incoming_queue, &job);
        pthread_mutex_unlock(&state->incoming_lock);

        if (has_patient && move_one_patient_to_priority(state, &job) != 0) {
            break;
        }
    }

    pthread_mutex_lock(&state->scheduled_lock);
    state->scheduler_done = 1;
    pthread_mutex_unlock(&state->scheduled_lock);

    for (int i = 0; i < state->scheduled_count + state->nurse_count; i++) {
        sem_post(&state->patient_ready);
    }

    puts("scheduler released priority queue to nurses");
    return NULL;
}

static void treat_patient(int nurse_no, const PatientJob *job)
{
    printf("nurse-%d treating patient=%s severity=%d bed=%s units=%d\n",
           nurse_no,
           job->patient_id,
           job->severity,
           bed_type_to_text(job->bed_type),
           job->care_units);

    small_delay_ms(90 + job->care_units * 8);
}

static void *nurse_thread(void *arg)
{
    NurseArgs *nurse = arg;
    ThreadHospitalState *state = nurse->state;

    while (1) {
        PatientJob job;
        int has_patient;

        if (wait_patient_sem(&state->patient_ready) != 0) {
            return NULL;
        }

        pthread_mutex_lock(&state->scheduled_lock);
        has_patient = patient_queue_pop(&state->scheduled_queue, &job);

        if (!has_patient && state->scheduler_done) {
            pthread_mutex_unlock(&state->scheduled_lock);
            break;
        }

        pthread_mutex_unlock(&state->scheduled_lock);

        if (!has_patient) {
            continue;
        }

        treat_patient(nurse->nurse_no, &job);
        log_thread_event("NURSE", &job);

        pthread_mutex_lock(&state->stats_lock);
        state->treated_count++;
        pthread_mutex_unlock(&state->stats_lock);
    }

    printf("nurse-%d leaving duty\n", nurse->nurse_no);
    return NULL;
}

static int init_thread_state(ThreadHospitalState *state, int patients, int nurses)
{
    memset(state, 0, sizeof(*state));

    state->patient_target = patients;
    state->nurse_count = nurses;

    patient_queue_init(&state->incoming_queue);
    patient_queue_init(&state->scheduled_queue);

    if (pthread_mutex_init(&state->incoming_lock, NULL) != 0) {
        return -1;
    }

    if (pthread_mutex_init(&state->scheduled_lock, NULL) != 0) {
        pthread_mutex_destroy(&state->incoming_lock);
        return -1;
    }

    if (pthread_mutex_init(&state->stats_lock, NULL) != 0) {
        pthread_mutex_destroy(&state->scheduled_lock);
        pthread_mutex_destroy(&state->incoming_lock);
        return -1;
    }

    if (pthread_cond_init(&state->incoming_ready, NULL) != 0) {
        pthread_mutex_destroy(&state->stats_lock);
        pthread_mutex_destroy(&state->scheduled_lock);
        pthread_mutex_destroy(&state->incoming_lock);
        return -1;
    }

    if (sem_init(&state->patient_ready, 0, 0) != 0) {
        pthread_cond_destroy(&state->incoming_ready);
        pthread_mutex_destroy(&state->stats_lock);
        pthread_mutex_destroy(&state->scheduled_lock);
        pthread_mutex_destroy(&state->incoming_lock);
        return -1;
    }

    return 0;
}

static void destroy_thread_state(ThreadHospitalState *state)
{
    sem_destroy(&state->patient_ready);
    pthread_cond_destroy(&state->incoming_ready);
    pthread_mutex_destroy(&state->stats_lock);
    pthread_mutex_destroy(&state->scheduled_lock);
    pthread_mutex_destroy(&state->incoming_lock);

    patient_queue_destroy(&state->incoming_queue);
    patient_queue_destroy(&state->scheduled_queue);
}

int run_thread_demo(int patient_count, int nurse_count)
{
    ThreadHospitalState state;
    pthread_t receptionist;
    pthread_t scheduler;
    pthread_t nurses[MAX_NURSES];
    NurseArgs nurse_args[MAX_NURSES];
    int started_nurses = 0;

    if (patient_count <= 0 || nurse_count <= 0 || nurse_count > MAX_NURSES) {
        puts("Error: use positive patient count and 1 to 8 nurses.");
        return 1;
    }

    if (init_thread_state(&state, patient_count, nurse_count) != 0) {
        perror("thread state init");
        return 1;
    }

    puts("thread demo starting: receptionist + scheduler + nurse pool");

    if (pthread_create(&receptionist, NULL, receptionist_thread, &state) != 0) {
        perror("pthread_create receptionist");
        destroy_thread_state(&state);
        return 1;
    }

    if (pthread_create(&scheduler, NULL, scheduler_thread, &state) != 0) {
        perror("pthread_create scheduler");
        pthread_join(receptionist, NULL);
        destroy_thread_state(&state);
        return 1;
    }

    for (int i = 0; i < nurse_count; i++) {
        nurse_args[i].nurse_no = i + 1;
        nurse_args[i].state = &state;

        if (pthread_create(&nurses[i], NULL, nurse_thread, &nurse_args[i]) != 0) {
            perror("pthread_create nurse");
            break;
        }

        started_nurses++;
    }

    pthread_join(receptionist, NULL);
    pthread_join(scheduler, NULL);

    for (int i = 0; i < started_nurses; i++) {
        pthread_join(nurses[i], NULL);
    }

    printf("\nThread demo summary:\n");
    printf("  produced=%d scheduled=%d treated=%d nurses=%d\n",
           state.produced_count,
           state.scheduled_count,
           state.treated_count,
           nurse_count);

    append_log_line("logs/schedule_log.txt", "THREAD",
                    "producer-consumer thread demo completed");

    destroy_thread_state(&state);
    return 0;
}
