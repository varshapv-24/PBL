/*
 * scheduler.h  --  shared header for the CPU Scheduling Simulator
 *
 * Everyone includes this file. Do NOT change the structs or function
 * signatures without telling the whole team.
 *
 * File ownership:
 *   Member 1 : fcfs_sjf_priority.c    (fcfs, sjf, priority_np)
 *   Member 2 : srtf_priority.c        (srtf, priority_p)
 *   Member 3 : round_robin.c, gantt.c (round_robin, gantt_display)
 *   Leader   : main.c, metrics.c, scheduler.h, Makefile, README.md
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

#define MAX_PROCESSES 50
#define MAX_BLOCKS    5000   /* max Gantt blocks for one run */
#define IDLE_PID      -1     /* pid used for CPU idle blocks */

/* ------------------------------------------------------------------ */
/* Process                                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    /* --- input (set by the user, never changed by algorithms) --- */
    int pid;               /* Process ID                                */
    int arrival_time;      /* Arrival Time                              */
    int burst_time;        /* Burst Time                                */
    int priority;          /* Priority (LOWER number = HIGHER priority) */

    /* --- filled in by the scheduling algorithms --- */
    int remaining_time;    /* Remaining burst time (starts = burst_time)*/
    int start_time;        /* First time on CPU (-1 = not started yet)  */
    int completion_time;   /* Completion Time                           */

    /* --- filled in by calculate_metrics() --- */
    int waiting_time;      /* WT  = TAT - burst_time                    */
    int turnaround_time;   /* TAT = completion_time - arrival_time      */
    int response_time;     /* RT  = start_time - arrival_time           */
} Process;

/* ------------------------------------------------------------------ */
/* Gantt chart                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    int pid;               /* process id, or IDLE_PID for idle          */
    int start;             /* block start time                          */
    int end;               /* block end time                            */
} GanttBlock;

typedef struct {
    GanttBlock blocks[MAX_BLOCKS];
    int count;
    int overflow;          /* 1 if blocks were dropped (chart too long) */
} GanttChart;

/* Overall results for one algorithm run */
typedef struct {
    double avg_waiting;
    double avg_turnaround;
    double avg_response;
    double cpu_utilization;   /* in percent */
    int    total_time;        /* time at which the last block ends */
} Metrics;

/* ------------------------------------------------------------------ */
/* Helper functions (ready to use, defined here so everyone has them   */
/* from day 1)                                                         */
/* ------------------------------------------------------------------ */

/* Empty the Gantt chart. Call at the start of every algorithm. */
static inline void gantt_init(GanttChart *g)
{
    g->count = 0;
    g->overflow = 0;
}

/*
 * Add the interval [start, end) for pid to the chart.
 *  - If the previous block is the same pid and touches this one,
 *    the two are merged automatically. So preemptive algorithms can
 *    simply call gantt_add(g, pid, t, t + 1) every time unit.
 *  - For idle time use pid = IDLE_PID.
 */
static inline void gantt_add(GanttChart *g, int pid, int start, int end)
{
    if (end <= start)
        return;
    if (g->count > 0) {
        GanttBlock *last = &g->blocks[g->count - 1];
        if (last->pid == pid && last->end == start) {
            last->end = end;
            return;
        }
    }
    if (g->count < MAX_BLOCKS) {
        g->blocks[g->count].pid   = pid;
        g->blocks[g->count].start = start;
        g->blocks[g->count].end   = end;
        g->count++;
    } else {
        g->overflow = 1;   /* chart full: remember that blocks were lost */
    }
}

/*
 * Reset the fields that algorithms fill in, so the same process array
 * can be run through several algorithms. Call at the START of every
 * algorithm.
 */
static inline void reset_processes(Process p[], int n)
{
    for (int i = 0; i < n; i++) {
        p[i].remaining_time  = p[i].burst_time;
        p[i].start_time      = -1;
        p[i].completion_time = 0;
        p[i].waiting_time    = 0;
        p[i].turnaround_time = 0;
        p[i].response_time   = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Scheduling algorithms                                               */
/*                                                                     */
/* Every algorithm must:                                               */
/*   1. call reset_processes(p, n) and gantt_init(g) first             */
/*   2. set start_time the FIRST time a process gets the CPU           */
/*   3. set completion_time when a process finishes                    */
/*   4. record every run and every idle gap with gantt_add()           */
/*   5. NOT print anything and NOT calculate WT / TAT / RT             */
/*                                                                     */
/* Tie-breaking rule (everyone uses the same one):                     */
/*   earlier arrival first, then smaller pid.                          */
/* ------------------------------------------------------------------ */

/* Member 1  (fcfs_sjf_priority.c) */
void fcfs(Process p[], int n, GanttChart *g);
void sjf(Process p[], int n, GanttChart *g);           /* non-preemptive */
void priority_np(Process p[], int n, GanttChart *g);   /* non-preemptive */

/* Member 2  (srtf_priority.c) */
void srtf(Process p[], int n, GanttChart *g);          /* preemptive SJF */
void priority_p(Process p[], int n, GanttChart *g);    /* preemptive     */

/* Member 3  (round_robin.c, gantt.c) */
void round_robin(Process p[], int n, int quantum, GanttChart *g);
void gantt_display(const GanttChart *g);

/* ------------------------------------------------------------------ */
/* Leader  (main.c, metrics.c)                                         */
/* ------------------------------------------------------------------ */
void input_processes(Process p[], int n);

/* Fills waiting_time, turnaround_time, response_time for each process
 * and the averages + CPU utilization (busy time / total time * 100)
 * in m. */
void calculate_metrics(Process p[], int n, const GanttChart *g, Metrics *m);

void display_results(const Process p[], int n, const Metrics *m);

/* ------------------------------------------------------------------ */
/* Optional extras (Member 1, if the team wants them)                  */
/* ------------------------------------------------------------------ */
void generate_random_processes(Process p[], int n);
void compare_all(Process p[], int n, int quantum);

#endif /* SCHEDULER_H */
