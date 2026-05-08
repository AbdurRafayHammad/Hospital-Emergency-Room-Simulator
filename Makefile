# File: Makefile

CC = gcc
CFLAGS = -Wall -Wextra -pthread -D_POSIX_C_SOURCE=200809L -Iinclude

HOSPITAL_TARGET = hospital_system
SIM_TARGET = patient_simulator
SCHED_TARGET = scheduler_worker

HOSPITAL_OBJ = src/admissions.o src/queue.o src/ipc_manager.o src/thread_manager.o src/memory_manager.o src/utils.o
SIM_OBJ = src/patient_simulator.o src/ipc_manager.o src/utils.o
SCHED_OBJ = src/scheduler.o src/queue.o src/ipc_manager.o src/utils.o

all: $(HOSPITAL_TARGET) $(SIM_TARGET) $(SCHED_TARGET)

$(HOSPITAL_TARGET): $(HOSPITAL_OBJ)
	$(CC) $(CFLAGS) -o $(HOSPITAL_TARGET) $(HOSPITAL_OBJ)

$(SIM_TARGET): $(SIM_OBJ)
	$(CC) $(CFLAGS) -o $(SIM_TARGET) $(SIM_OBJ)

$(SCHED_TARGET): $(SCHED_OBJ)
	$(CC) $(CFLAGS) -o $(SCHED_TARGET) $(SCHED_OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f src/*.o
	rm -f $(HOSPITAL_TARGET) $(SIM_TARGET) $(SCHED_TARGET)

run: all
	./$(HOSPITAL_TARGET) --demo

.PHONY: all clean run
