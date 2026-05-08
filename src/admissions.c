/* File: src/admissions.c */

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common.h"
#include "ipc_manager.h"
#include "memory_manager.h"
#include "queue.h"
#include "scheduler.h"
#include "thread_manager.h"
#include "utils.h"

typedef enum {
    SOURCE_PIPE_DEMO = 1,
    SOURCE_FIFO_INPUT = 2
} AdmissionSource;

static volatile sig_atomic_t scheduler_sigchld_seen = 0;
static volatile sig_atomic_t shutdown_requested = 0;

static void handle_sigchld(int signal_number)
{
    (void)signal_number;
    scheduler_sigchld_seen = 1;
}

static void handle_shutdown(int signal_number)
{
    (void)signal_number;
    shutdown_requested = 1;
}

static void show_usage(void)
{
    puts("Usage:");
    puts("  ./hospital_system --demo");
    puts("  ./hospital_system --pipe-demo 5");
    puts("  ./hospital_system --fifo 5");
    puts("  ./hospital_system --fifo-forever");
    puts("  ./hospital_system --thread-demo 10 3");
    puts("  ./hospital_system --memory-demo first|best|worst|all 12");
}

static int install_sigchld_handler(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_sigchld;
    action.sa_flags = SA_RESTART;

    if (sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }

    return sigaction(SIGCHLD, &action, NULL);
}

static int install_shutdown_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_shutdown;

    if (sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }

    if (sigaction(SIGTERM, &action, NULL) != 0) {
        return -1;
    }

    return sigaction(SIGINT, &action, NULL);
}

static int parse_limit(const char *text, int *out_limit)
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

    *out_limit = (int)parsed;
    return 0;
}

static int create_shared_board(int *shmid, HospitalBoard **board)
{
    if (hospital_shm_create(shmid, board) != 0) {
        perror("shared memory create");
        return -1;
    }

    hospital_board_reset(*board, getpid());

    printf("shared memory board created key=0x%lx shmid=%d\n",
           (unsigned long)HOSPITAL_SHM_KEY,
           *shmid);

    append_log_line("logs/schedule_log.txt", "SHM",
                    "parent created shared hospital board");

    return 0;
}

static void cleanup_shared_board(int shmid, HospitalBoard *board)
{
    if (board != NULL && hospital_board_detach(board) != 0) {
        perror("shared memory detach");
    }

    if (shmid >= 0 && hospital_shm_remove(shmid) != 0) {
        perror("shared memory remove");
    }
}

static void print_board_snapshot(const char *title, const HospitalBoard *board)
{
    if (board == NULL || board->magic != HOSPITAL_SHM_MAGIC) {
        puts("shared board snapshot unavailable");
        return;
    }

    printf("\n%s\n", title);
    printf("  parent_pid=%ld scheduler_pid=%ld\n",
           (long)board->parent_pid,
           (long)board->scheduler_pid);
    printf("  pipe_generated=%d fifo_forwarded=%d\n",
           board->pipe_generated,
           board->fifo_forwarded);
    printf("  total_received=%d total_scheduled=%d high_priority_seen=%d\n",
           board->total_received,
           board->total_scheduled,
           board->high_priority_seen);
    printf("  waiting general=%d icu=%d isolation=%d\n",
           board->general_waiting,
           board->icu_waiting,
           board->isolation_waiting);
    printf("  last_patient=%s status=%s\n",
           board->last_patient_id,
           board->last_status);
}

static void hold_shm_for_screenshot(void)
{
    if (!shutdown_requested) {
        puts("\nShared memory is still present for 5 seconds.");
        puts("Open another terminal and run: ipcs -m");
        sleep(5);
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

static PatientJob make_generated_patient(int number)
{
    PatientJob job;

    memset(&job, 0, sizeof(job));

    job.patient_no = number;
    snprintf(job.patient_id, sizeof(job.patient_id), "PIPE-%04d", number);
    snprintf(job.name, sizeof(job.name), "PipePatient_%02d", number);
    job.age = 21 + ((number * 5) % 50);
    job.severity = 1 + ((number * 3) % 5);
    job.bed_type = choose_bed_type(number);
    job.care_units = 6 + ((number * 4) % 21);

    return job;
}

static PatientJob make_demo_patient(int number, const char *name, int severity,
                                    BedType bed_type, int care_units)
{
    PatientJob job;

    memset(&job, 0, sizeof(job));

    job.patient_no = number;
    snprintf(job.patient_id, sizeof(job.patient_id), "DEMO-%03d", number);
    snprintf(job.name, sizeof(job.name), "%s", name);
    job.age = 20 + number;
    job.severity = severity;
    job.bed_type = bed_type;
    job.care_units = care_units;

    return job;
}

static void print_patient_line(const char *label, const PatientJob *job)
{
    printf("%s patient=%s name=%s severity=%d bed=%s units=%d order=%lu\n",
           label,
           job->patient_id,
           job->name,
           job->severity,
           bed_type_to_text(job->bed_type),
           job->care_units,
           job->arrival_order);
}

static int write_all(int fd, const char *buffer, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        ssize_t written = write(fd, buffer + sent, length - sent);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }

            return -1;
        }

        if (written == 0) {
            return -1;
        }

        sent += (size_t)written;
    }

    return 0;
}

static int start_scheduler_child(int *write_fd, pid_t *child_pid)
{
    int pipe_fd[2];
    pid_t pid;

    if (pipe(pipe_fd) != 0) {
        perror("pipe");
        return -1;
    }

    scheduler_sigchld_seen = 0;
    pid = fork();

    if (pid < 0) {
        perror("fork");
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return -1;
    }

    if (pid == 0) {
        char fd_text[32];
        char *child_args[] = {
            SCHEDULER_WORKER_PATH,
            fd_text,
            NULL
        };

        close(pipe_fd[1]);
        snprintf(fd_text, sizeof(fd_text), "%d", pipe_fd[0]);

        execv(SCHEDULER_WORKER_PATH, child_args);

        perror("execv scheduler_worker");
        close(pipe_fd[0]);
        _exit(127);
    }

    close(pipe_fd[0]);

    *write_fd = pipe_fd[1];
    *child_pid = pid;

    printf("parent forked scheduler child PID=%ld\n", (long)pid);
    append_log_line("logs/schedule_log.txt", "PARENT",
                    "forked scheduler worker");

    return 0;
}

static int wait_for_scheduler(pid_t child_pid)
{
    int status;
    pid_t waited;

    do {
        waited = waitpid(child_pid, &status, 0);
    } while (waited < 0 && errno == EINTR);

    if (waited < 0) {
        perror("waitpid");
        return 1;
    }

    if (scheduler_sigchld_seen) {
        puts("parent observed SIGCHLD from scheduler child");
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);

        printf("scheduler child exited with code %d\n", code);
        return code;
    }

    if (WIFSIGNALED(status)) {
        printf("scheduler child killed by signal %d\n", WTERMSIG(status));
        return 1;
    }

    puts("scheduler child ended in unexpected state");
    return 1;
}

static void note_parent_received(HospitalBoard *board, const PatientJob *job,
                                 AdmissionSource source)
{
    hospital_board_note_received(board, job);

    if (board == NULL || board->magic != HOSPITAL_SHM_MAGIC) {
        return;
    }

    if (source == SOURCE_PIPE_DEMO) {
        board->pipe_generated++;
    } else {
        board->fifo_forwarded++;
    }
}

static int send_patient_to_scheduler(int write_fd, const PatientJob *job,
                                     HospitalBoard *board,
                                     AdmissionSource source)
{
    char line[LOG_LINE_LEN];

    if (format_patient_record(job, line, sizeof(line)) != 0) {
        return -1;
    }

    note_parent_received(board, job, source);

    if (write_all(write_fd, line, strlen(line)) != 0) {
        return -1;
    }

    printf("parent wrote patient=%s severity=%d into anonymous pipe\n",
           job->patient_id,
           job->severity);

    return 0;
}

static int run_pipe_demo(int count)
{
    int shmid = -1;
    HospitalBoard *board = NULL;
    int write_fd;
    pid_t child_pid;
    int i;
    int child_status;

    if (create_shared_board(&shmid, &board) != 0) {
        return 1;
    }

    if (install_sigchld_handler() != 0) {
        perror("sigaction");
        cleanup_shared_board(shmid, board);
        return 1;
    }

    if (start_scheduler_child(&write_fd, &child_pid) != 0) {
        cleanup_shared_board(shmid, board);
        return 1;
    }

    for (i = 1; i <= count; i++) {
        PatientJob job = make_generated_patient(i);

        if (send_patient_to_scheduler(write_fd, &job, board,
                                      SOURCE_PIPE_DEMO) != 0) {
            perror("write anonymous pipe");
            close(write_fd);
            child_status = wait_for_scheduler(child_pid);
            cleanup_shared_board(shmid, board);
            return child_status;
        }

        sleep(1);
    }

    close(write_fd);
    puts("parent closed pipe write end");

    child_status = wait_for_scheduler(child_pid);
    print_board_snapshot("Shared memory board after scheduling:", board);
    hold_shm_for_screenshot();
    cleanup_shared_board(shmid, board);

    return child_status;
}

static int run_demo(void)
{
    PatientQueue fcfs_queue;
    PatientQueue priority_queue;
    PatientJob patients[] = {
        make_demo_patient(1, "Nadia", 4, BED_GENERAL, 7),
        make_demo_patient(2, "Hassan", 1, BED_ICU, 18),
        make_demo_patient(3, "Mariam", 3, BED_ISOLATION, 11),
        make_demo_patient(4, "Bilal", 1, BED_ICU, 15)
    };
    size_t count = sizeof(patients) / sizeof(patients[0]);
    size_t i;
    PatientJob job;

    patient_queue_init(&fcfs_queue);
    patient_queue_init(&priority_queue);

    for (i = 0; i < count; i++) {
        if (patient_queue_push_fcfs(&fcfs_queue, &patients[i]) != 0 ||
            patient_queue_push_priority(&priority_queue, &patients[i]) != 0) {
            patient_queue_destroy(&fcfs_queue);
            patient_queue_destroy(&priority_queue);
            return 1;
        }
    }

    puts("FCFS scheduling order:");
    while (patient_queue_pop(&fcfs_queue, &job)) {
        print_patient_line("  scheduled", &job);
    }

    puts("\nPriority scheduling order:");
    while (patient_queue_pop(&priority_queue, &job)) {
        print_patient_line("  scheduled", &job);
    }

    return 0;
}

static int forward_fifo_line(int write_fd, const char *line, int *forwarded,
                             HospitalBoard *board)
{
    PatientJob job;

    if (parse_patient_record(line, &job) != 0) {
        append_log_line("logs/schedule_log.txt", "FIFO-BAD",
                        "parent rejected malformed FIFO record");
        return 0;
    }

    note_parent_received(board, &job, SOURCE_FIFO_INPUT);

    if (write_all(write_fd, line, strlen(line)) != 0) {
        return -1;
    }

    *forwarded = *forwarded + 1;

    printf("parent forwarded FIFO patient=%s to scheduler child\n",
           job.patient_id);

    return 0;
}

static int read_fifo_and_forward(int write_fd, int limit, int *forwarded,
                                 HospitalBoard *board)
{
    int fd;
    FILE *stream;
    char line[LOG_LINE_LEN];

    puts("parent waiting for patient_simulator on FIFO...");
    fflush(stdout);

    fd = hospital_fifo_open_read(HOSPITAL_FIFO_PATH);

    if (fd < 0) {
        if (errno == EINTR && shutdown_requested) {
            return 1;
        }

        perror("open FIFO for read");
        return -1;
    }

    stream = fdopen(fd, "r");

    if (stream == NULL) {
        perror("fdopen FIFO");
        close(fd);
        return -1;
    }

    while (!shutdown_requested &&
           (limit == 0 || *forwarded < limit) &&
           fgets(line, sizeof(line), stream) != NULL) {
        if (forward_fifo_line(write_fd, line, forwarded, board) != 0) {
            fclose(stream);
            return -1;
        }
    }

    if (ferror(stream) && !shutdown_requested) {
        perror("read FIFO");
        fclose(stream);
        return -1;
    }

    fclose(stream);
    return shutdown_requested ? 1 : 0;
}

static int run_fifo_parent(int limit)
{
    int shmid = -1;
    HospitalBoard *board = NULL;
    int write_fd;
    int forwarded = 0;
    pid_t child_pid;
    int child_status;
    int read_result;

    if (create_shared_board(&shmid, &board) != 0) {
        return 1;
    }

    if (install_sigchld_handler() != 0 || install_shutdown_handlers() != 0) {
        perror("sigaction");
        cleanup_shared_board(shmid, board);
        return 1;
    }

    if (hospital_fifo_create(HOSPITAL_FIFO_PATH) != 0) {
        perror("mkfifo");
        cleanup_shared_board(shmid, board);
        return 1;
    }

    printf("FIFO ready at %s\n", HOSPITAL_FIFO_PATH);

    if (start_scheduler_child(&write_fd, &child_pid) != 0) {
        hospital_fifo_remove(HOSPITAL_FIFO_PATH);
        cleanup_shared_board(shmid, board);
        return 1;
    }

    while (!shutdown_requested && (limit == 0 || forwarded < limit)) {
        read_result = read_fifo_and_forward(write_fd, limit, &forwarded, board);

        if (read_result < 0) {
            close(write_fd);
            wait_for_scheduler(child_pid);
            hospital_fifo_remove(HOSPITAL_FIFO_PATH);
            cleanup_shared_board(shmid, board);
            return 1;
        }

        if (read_result > 0) {
            break;
        }

        if (limit == 0 && !shutdown_requested) {
            puts("FIFO writer disconnected; parent will wait again");
        }
    }

    close(write_fd);
    hospital_fifo_remove(HOSPITAL_FIFO_PATH);

    child_status = wait_for_scheduler(child_pid);
    print_board_snapshot("Shared memory board after FIFO scheduling:", board);
    hold_shm_for_screenshot();
    cleanup_shared_board(shmid, board);

    return child_status;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);

    print_banner();
    print_phase_status();

    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--demo") == 0)) {
        return run_demo();
    }

    if (argc == 3 && strcmp(argv[1], "--pipe-demo") == 0) {
        int count;

        if (parse_limit(argv[2], &count) != 0) {
            puts("Error: pipe demo count must be positive.");
            return 1;
        }

        return run_pipe_demo(count);
    }

    if (argc == 3 && strcmp(argv[1], "--fifo") == 0) {
        int limit;

        if (parse_limit(argv[2], &limit) != 0) {
            puts("Error: FIFO limit must be positive.");
            return 1;
        }

        return run_fifo_parent(limit);
    }

    if (argc == 2 && strcmp(argv[1], "--fifo-forever") == 0) {
        return run_fifo_parent(0);
    }

    if (argc == 4 && strcmp(argv[1], "--thread-demo") == 0) {
        int patients;
        int nurses;

        if (parse_limit(argv[2], &patients) != 0 ||
            parse_limit(argv[3], &nurses) != 0) {
            puts("Error: thread demo needs positive patient and nurse counts.");
            return 1;
        }

        return run_thread_demo(patients, nurses);
    }

    if (argc == 4 && strcmp(argv[1], "--memory-demo") == 0) {
        int requests;
        AllocationStrategy strategy;

        if (parse_limit(argv[3], &requests) != 0) {
            puts("Error: memory demo request count must be positive.");
            return 1;
        }

        if (strcmp(argv[2], "all") == 0) {
            return run_memory_demo_all(requests);
        }

        if (parse_allocation_strategy(argv[2], &strategy) != 0) {
            puts("Error: strategy must be first, best, worst, or all.");
            return 1;
        }

        return run_memory_demo(strategy, requests);
    }

    show_usage();
    return 1;
}
