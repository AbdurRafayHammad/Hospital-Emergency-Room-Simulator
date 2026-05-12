# Hospital-Emergency-Room-Simulator
A complete Hospital Emergency Room Simulation System developed in C language that combines major Operating System concepts into one realistic system-level application.

This project simulates how a real hospital emergency department manages patients, scheduling, memory allocation, synchronization, and concurrent operations using low-level OS mechanisms.

**Project Highlights**

✅ Multi-Process Architecture
✅ Real-Time Patient Scheduling
✅ Inter-Process Communication (IPC)
✅ Shared Memory System
✅ Thread Synchronization
✅ Memory Allocation & Fragmentation Simulation
✅ Linux Shell Scripting
✅ Logging & Monitoring System

**OS Concepts Implemented**

Operating System Concept	Implementation
Process Management	fork() & execv()
CPU Scheduling	FCFS & Priority Scheduling
IPC	Pipes, FIFO, Shared Memory
Threads	Receptionist & Nurse Thread Pool
Synchronization	Mutexes, Semaphores, Condition Variables
Memory Management	First-Fit, Best-Fit, Worst-Fit
Fragmentation	External Fragmentation Tracking
Linux Environment	Makefile & Bash Scripts

**System Architecture**

The simulator is divided into multiple modules that work together like a mini operating system.

🔹 Admissions Controller

Acts as the main parent process responsible for:

Patient management
Scheduling coordination
Resource handling
Shared memory maintenance
🔹 Scheduler Worker

Runs as a child process and schedules patients based on severity level.

🔹 Patient Simulator

Generates patient records dynamically and sends them into the system.

🔹 Nurse Thread Pool

Multiple concurrent nurse threads treat patients simultaneously.

🔹 Shared Memory Board

Stores:

Patient statistics
Bed allocation data
Scheduling information
Process IDs

**Scheduling Algorithms**

The system supports two scheduling strategies:

🟢 FCFS Scheduling

Patients are treated in arrival order.

🔴 Priority Scheduling

Critical patients receive treatment first.

Severity Level	Priority
1	Critical
5	Stable

This mimics real hospital emergency room behavior.

**Inter-Process Communication (IPC)**

The project demonstrates multiple IPC mechanisms.

📌 Anonymous Pipes

Used for communication between parent and child processes.

📌 Named FIFO

Allows unrelated processes to exchange patient records dynamically.

📌 Shared Memory

Provides fast shared access to hospital-wide data structures.

**Multithreading & Synchronization**

The simulator uses concurrent threads to simulate real-time hospital activity.

👨‍⚕️ Thread Roles
Receptionist Thread
Scheduler Thread
Nurse Threads
🔐 Synchronization Mechanisms
Mutex Locks
Counting Semaphores
Condition Variables

These mechanisms ensure:

Safe concurrent execution
No race conditions
Proper resource coordination

**Memory Management System**

The hospital ward is modeled as a contiguous memory region.

Supported allocation strategies:

✅ First-Fit
✅ Best-Fit
✅ Worst-Fit

The system also demonstrates:

External Fragmentation
Memory Coalescing
Paging Simulation

This provides practical understanding of real OS memory allocation techniques.

**Logging & Monitoring**

The simulator automatically maintains detailed logs for:

Scheduling activity
Process creation
Thread execution
Memory allocation
Fragmentation statistics
Coalescing operations

**Linux & Shell Scripting**

The project integrates Linux utilities using:

Bash Scripts
Makefile Automation
GCC Compilation
Linux System Calls

Scripts are included for:

Running the system
Validating patient input
Managing processes
Automating execution
 Build Instructions
 Compile Project
make clean && make all
 Run Modes
 Demo Mode
./hospital_system --demo

Demonstrates:

FCFS Scheduling
Priority Scheduling
🔗 Pipe Demo
./hospital_system --pipe-demo 5

Demonstrates:

fork()
execv()
Anonymous Pipes
Shared Memory
SIGCHLD
📡 FIFO Demo
Terminal 1
./hospital_system --fifo 4
Terminal 2
./patient_simulator 4

Demonstrates Named FIFO IPC.

**Thread Demo**
./hospital_system --thread-demo 10 3

Demonstrates:

Multithreading
Mutex Synchronization
Semaphores
Condition Variables
**Memory Demo**
./hospital_system --memory-demo all 12

Demonstrates:

Allocation Strategies
Fragmentation
Paging
Coalescing
**Technologies Used**

Technology	Purpose
C Programming	Core Development
GCC	Compilation
POSIX Threads	Multithreading
Linux System Calls	OS Features
Bash Scripts	Automation
Makefile	Build Management
**Learning Outcomes**

Process Lifecycle Management
CPU Scheduling Algorithms
IPC Mechanisms
Concurrent Programming
Synchronization Techniques
Memory Allocation
Fragmentation Handling
Linux System Programming

This project transformed operating system theory into a complete real-world implementation.
