/* File: include/memory_manager.h */

#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

typedef enum {
    ALLOC_FIRST_FIT = 1,
    ALLOC_BEST_FIT = 2,
    ALLOC_WORST_FIT = 3
} AllocationStrategy;

const char *allocation_strategy_name(AllocationStrategy strategy);
int parse_allocation_strategy(const char *text, AllocationStrategy *out_strategy);
int run_memory_demo(AllocationStrategy strategy, int request_count);
int run_memory_demo_all(int request_count);

#endif
