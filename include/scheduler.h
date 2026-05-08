/* File: include/scheduler.h */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#define SCHEDULER_WORKER_PATH "./scheduler_worker"

int scheduler_worker_main_from_fd(int read_fd);

#endif
