# Hospital-Emergency-Room-Simulator
A complete Operating Systems semester project developed in C that simulates a hospital emergency room while integrating core OS concepts such as process management, IPC, CPU scheduling, synchronization, threading, and memory management.

Features
Process creation using fork() and execv()
CPU scheduling algorithms
Inter-Process Communication (IPC)
Anonymous Pipes
Named FIFO Communication
Shared Memory
POSIX Threads
Mutexes and Condition Variables
Counting Semaphores
Best-Fit / First-Fit / Worst-Fit allocation
External Fragmentation handling
Memory coalescing
Linux shell scripting
Logging system
Makefile-based build system
Operating System Concepts Used
OS Concept	Implementation
Process Management	Parent-child processes using fork()
CPU Scheduling	FCFS and Priority Scheduling
IPC	Pipes, FIFOs, Shared Memory
Threads	Nurse and Receptionist thread pool
Synchronization	Mutexes, Semaphores, Condition Variables
Memory Management	Dynamic ward allocation
Fragmentation	External fragmentation simulation
Linux Environment	Shell scripts and Makefile
System Architecture

The system contains multiple independent modules working together:

Admissions Controller
Scheduler Worker
Patient Simulator
Nurse Thread Pool
Shared Memory Board

The admissions controller acts as the central process and coordinates scheduling, patient handling, and memory allocation.

Scheduling Algorithms

The simulator supports:

FCFS Scheduling

Patients are treated in arrival order.

Priority Scheduling

Critical patients are treated first based on severity level.

Severity levels range from:

Severity	Priority
1	Critical
5	Stable
IPC Mechanisms
Anonymous Pipes

Used for communication between parent and scheduler processes.

Named FIFO

Used for communication between unrelated processes.

Shared Memory

Stores hospital-wide statistics and bed allocation information.

Threading and Synchronization

The project uses multiple threads to simulate real hospital activity.

Thread Roles
Receptionist Thread
Scheduler Thread
Nurse Threads
Synchronization Tools
Mutex Locks
Condition Variables
Counting Semaphores

These mechanisms prevent race conditions and ensure safe concurrent execution.

Memory Management

The hospital ward is modeled as a contiguous memory space.

Supported allocation strategies:

First-Fit
Best-Fit
Worst-Fit

The simulator also demonstrates:

External Fragmentation
Memory Coalescing
Paging Simulation
Logging System

The system automatically generates logs for:

Scheduling events
Memory allocation
Process activity
Fragmentation statistics
Thread operations
Build Instructions
Compile Project
make clean && make all
Run Modes
Demo Mode
./hospital_system --demo

Demonstrates FCFS vs Priority Scheduling.

Pipe Demo
./hospital_system --pipe-demo 5

Demonstrates:

fork()
execv()
Anonymous Pipes
Shared Memory
SIGCHLD
FIFO Demo

Terminal 1:

./hospital_system --fifo 4

Terminal 2:

./patient_simulator 4

Demonstrates Named FIFO IPC.

Thread Demo
./hospital_system --thread-demo 10 3

Demonstrates:

Multithreading
Mutexes
Condition Variables
Semaphores
Memory Demo
./hospital_system --memory-demo all 12

Demonstrates:

Memory allocation strategies
Fragmentation
Coalescing
Paging simulation
Technologies Used
C Programming
GCC Compiler
POSIX Threads
Linux System Calls
Bash Scripting
Makefile
Learning Outcomes

This project provided practical understanding of:

Process lifecycle management
CPU scheduling
IPC mechanisms
Thread synchronization
Dynamic memory allocation
Fragmentation handling
Linux system programming
