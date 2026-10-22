# CPU Process Scheduling Simulator 

A menu-driven CPU scheduling simulator written in C. The user enters a set of
processes, picks a scheduling algorithm, and the program prints a Gantt chart
and the performance metrics for that schedule.

## Features

- **Algorithms**
  - FCFS (First-Come-First-Served)
  - SJF (Shortest Job First), non-preemptive
  - SRTF (Shortest Remaining Time First), i.e. preemptive SJF
  - Priority scheduling, non-preemptive
  - Priority scheduling, preemptive
  - Round Robin with a configurable time quantum
- **Output for every run**
  - Gantt chart (idle CPU time is shown as `idle`)
  - Per-process Completion Time (CT), Turnaround Time (TAT), Waiting Time (WT)
    and Response Time (RT)
  - Average WT, average TAT, average RT, total time and CPU utilization
- **Easy switching:** enter the processes once, then run any algorithm from the
  menu, one after another, on the same data.
- **Input checking:** non-numbers, out-of-range values and duplicate process IDs
  are rejected and re-asked.

## How to build and run

Requires `gcc` and `make` (Linux, WSL, macOS, or a GitHub Codespace).

```
cd week3
make
./sim
```

Other commands:

| Command      | What it does                              |
|--------------|-------------------------------------------|
| `make`       | builds the program (creates `sim`)        |
| `make run`   | builds and starts the program             |
| `make clean` | deletes the compiled files                |

If `make` is not available, compile directly:

```
gcc -Wall -Wextra -o sim main.c metrics.c gantt.c round_robin.c fcfs_sjf_priority.c srtf_priority.c
```

## How to use

1. Choose **1** (Enter / change processes).
2. Enter the number of processes (1 to 50).
3. For each process enter: **Process ID**, **Arrival Time**, **Burst Time**
   and **Priority**.
4. Choose an algorithm from the menu (options 2 to 7). Round Robin asks for the
   time quantum.
5. Choose another algorithm to compare, or option **1** to enter new data.
6. Choose **9** to exit.

Menu:

```
1. Enter / change processes
2. FCFS
3. SJF (Non-preemptive)
4. SRTF (Preemptive SJF)
5. Priority (Non-preemptive)
6. Priority (Preemptive)
7. Round Robin
8. Show current processes
9. Exit
```

**Rules used by all algorithms**

- A **lower priority number means higher priority**.
- Ties are broken by earlier arrival time, then by smaller process ID.
- In Round Robin, a process that arrives while another is running (or exactly
  when its time slice ends) is queued before the preempted process.

## Formulas

| Metric              | Formula                                          |
|---------------------|--------------------------------------------------|
| Turnaround Time     | Completion Time - Arrival Time                   |
| Waiting Time        | Turnaround Time - Burst Time                     |
| Response Time       | First time on CPU - Arrival Time                 |
| CPU Utilization (%) | busy time / elapsed time x 100                   |

Elapsed time is counted from the first process's arrival, so waiting for the
very first process to arrive is not counted as idle time.

## Sample run

Test data (this example was run and checked by hand):

| Process ID | Arrival Time | Burst Time | Priority |
|------------|--------------|------------|----------|
| 1          | 0            | 5          | 1        |
| 2          | 1            | 3          | 2        |
| 3          | 2            | 1          | 3        |

Input: choose `1`, then `3` processes, then the values above. Then choose
`2` (FCFS) and `3` (SJF).

### FCFS

```
==================== FCFS (First-Come-First-Served) ====================

Gantt Chart:
+------+------+------+
|  P1  |  P2  |  P3  |
+------+------+------+
0      5      8      9

Results:
+--------+--------+--------+--------+--------+--------+--------+--------+
|     PID|      AT|      BT|      PR|      CT|     TAT|      WT|      RT|
+--------+--------+--------+--------+--------+--------+--------+--------+
|       1|       0|       5|       1|       5|       5|       0|       0|
|       2|       1|       3|       2|       8|       7|       4|       4|
|       3|       2|       1|       3|       9|       7|       6|       6|
+--------+--------+--------+--------+--------+--------+--------+--------+
AT = Arrival, BT = Burst, PR = Priority, CT = Completion,
TAT = Turnaround, WT = Waiting, RT = Response

Average Waiting Time    : 3.33
Average Turnaround Time : 6.33
Average Response Time   : 3.33
Total Time              : 9
CPU Utilization         : 100.00%
```

### SJF (non-preemptive)

```
==================== SJF (Non-preemptive) ====================

Gantt Chart:
+------+------+------+
|  P1  |  P3  |  P2  |
+------+------+------+
0      5      6      9

Results:
+--------+--------+--------+--------+--------+--------+--------+--------+
|     PID|      AT|      BT|      PR|      CT|     TAT|      WT|      RT|
+--------+--------+--------+--------+--------+--------+--------+--------+
|       1|       0|       5|       1|       5|       5|       0|       0|
|       2|       1|       3|       2|       9|       8|       5|       5|
|       3|       2|       1|       3|       6|       4|       3|       3|
+--------+--------+--------+--------+--------+--------+--------+--------+
AT = Arrival, BT = Burst, PR = Priority, CT = Completion,
TAT = Turnaround, WT = Waiting, RT = Response

Average Waiting Time    : 2.67
Average Turnaround Time : 5.67
Average Response Time   : 2.67
Total Time              : 9
CPU Utilization         : 100.00%
```

### All algorithms on the same data

Round Robin uses a time quantum of 2. CPU utilization is 100% for every
algorithm because the CPU is never idle.

| Algorithm                 | Execution order (Gantt chart)        | Avg WT | Avg TAT | Avg RT |
|---------------------------|--------------------------------------|--------|---------|--------|
| FCFS                      | P1, P2, P3                           | 3.33   | 6.33    | 3.33   |
| SJF                       | P1, P3, P2                           | 2.67   | 5.67    | 2.67   |
| SRTF                      | P1, P2, P3, P2, P1                   | 1.67   | 4.67    | 0.00   |
| Priority (non-preemptive) | P1, P2, P3                           | 3.33   | 6.33    | 3.33   |
| Priority (preemptive)     | P1, P2, P3                           | 3.33   | 6.33    | 3.33   |
| Round Robin (q = 2)       | P1, P2, P3, P1, P2, P1               | 3.33   | 6.33    | 1.00   |

SRTF gives the lowest average waiting time here, because the short processes
P2 and P3 interrupt the longer P1. The Priority results match FCFS because
the priorities (1, 2, 3) happen to be in the same order as the arrivals, so
no process is ever preempted.

## Project structure

| File                  | Contents                                                    |
|-----------------------|-------------------------------------------------------------|
| `scheduler.h`         | Shared structures (`Process`, `GanttChart`, `Metrics`), helper functions and all function declarations |
| `main.c`              | Menu, process input, running the chosen algorithm           |
| `metrics.c`           | `calculate_metrics()` and `display_results()`               |
| `fcfs_sjf_priority.c` | `fcfs()`, `sjf()`, `priority_np()`                          |
| `srtf_priority.c`     | `srtf()`, `priority_p()`                                    |
| `round_robin.c`       | `round_robin()`                                             |
| `gantt.c`             | `gantt_display()`                                           |
| `Makefile`            | Build instructions                                          |

Every algorithm has the same shape: it fills in each process's start and
completion time and records the Gantt chart, without printing anything. The
metrics and the printing are done in one place (`metrics.c`), so all algorithms
are measured the same way.

## Limits

- Up to 50 processes.
- A Gantt chart holds up to 5000 blocks. If a run needs more (for example, a
  very small quantum with very long bursts), the chart is truncated with a note,
  but the metrics are still correct.
