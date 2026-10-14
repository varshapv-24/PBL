/*
 * srtf_priority.c 
 *
 * Preemptive scheduling algorithms for the CPU Scheduling Simulator:
 *   - srtf()       : Shortest Remaining Time First (preemptive SJF)
 *   - priority_p() : Priority scheduling, preemptive
 *                    (LOWER number = HIGHER priority)
 *
 * Both algorithms advance the clock ONE time unit at a time. At every
 * unit the scheduler re-checks all arrived, unfinished processes and
 * picks the best one, so a newly arrived process can interrupt the
 * running process immediately.
 *
 * Tie-breaking (team rule): earlier arrival first, then smaller pid.
 *
 * As required by scheduler.h, these functions:
 *   1. call reset_processes() and gantt_init() first
 *   2. set start_time the first time a process gets the CPU
 *   3. set completion_time when a process finishes
 *   4. record every run and every idle gap with gantt_add()
 *      (consecutive units of the same pid are merged by gantt_add)
 *   5. do NOT print and do NOT compute WT / TAT / RT
 */

#include "scheduler.h"

/* Which rule decides "best" process at each time unit */
#define MODE_SRTF     0
#define MODE_PRIORITY 1

/*
 * Returns 1 if process a is a better choice than process b,
 * otherwise 0, according to the selected mode.
 */
static int is_better(const Process *a, const Process *b, int mode)
{
    if (mode == MODE_SRTF) {
        if (a->remaining_time != b->remaining_time)
            return a->remaining_time < b->remaining_time;
    } else { /* MODE_PRIORITY: lower number = higher priority */
        if (a->priority != b->priority)
            return a->priority < b->priority;
    }

    /* Tie-break: earlier arrival, then smaller pid */
    if (a->arrival_time != b->arrival_time)
        return a->arrival_time < b->arrival_time;
    return a->pid < b->pid;
}

/*
 * Index of the best arrived & unfinished process at time t,
 * or -1 if no process is ready (CPU idle).
 */
static int pick_process(const Process p[], int n, int t, int mode)
{
    int best = -1;

    for (int i = 0; i < n; i++) {
        if (p[i].arrival_time > t || p[i].remaining_time <= 0)
            continue;
        if (best == -1 || is_better(&p[i], &p[best], mode))
            best = i;
    }
    return best;
}

/*
 * Earliest arrival time among unfinished processes that have not
 * arrived yet (arrival_time > t). Used to jump over idle gaps.
 * Returns -1 if there is none.
 */
static int next_arrival(const Process p[], int n, int t)
{
    int next = -1;

    for (int i = 0; i < n; i++) {
        if (p[i].remaining_time <= 0 || p[i].arrival_time <= t)
            continue;
        if (next == -1 || p[i].arrival_time < next)
            next = p[i].arrival_time;
    }
    return next;
}

/* Shared engine for both preemptive algorithms */
static void run_preemptive(Process p[], int n, GanttChart *g, int mode)
{
    int t = 0;
    int finished = 0;

    reset_processes(p, n);
    gantt_init(g);

    /* A process with burst 0 needs no CPU: finish it on arrival */
    for (int i = 0; i < n; i++) {
        if (p[i].remaining_time <= 0) {
            p[i].remaining_time  = 0;
            p[i].start_time      = p[i].arrival_time;
            p[i].completion_time = p[i].arrival_time;
            finished++;
        }
    }

    while (finished < n) {
        int cur = pick_process(p, n, t, mode);

        if (cur == -1) {
            /* CPU idle: jump to the next arrival and record idle block */
            int next = next_arrival(p, n, t);
            if (next == -1)
                break;               /* safety: nothing left to run */
            gantt_add(g, IDLE_PID, t, next);
            t = next;
            continue;
        }

        /* First time on the CPU */
        if (p[cur].start_time == -1)
            p[cur].start_time = t;

        /* Run for exactly one time unit (gantt_add merges same-pid units) */
        gantt_add(g, p[cur].pid, t, t + 1);
        p[cur].remaining_time--;
        t++;

        /* Finished? */
        if (p[cur].remaining_time == 0) {
            p[cur].completion_time = t;
            finished++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Public functions (signatures from scheduler.h)                      */
/* ------------------------------------------------------------------ */

/* Shortest Remaining Time First (preemptive SJF) */
void srtf(Process p[], int n, GanttChart *g)
{
    run_preemptive(p, n, g, MODE_SRTF);
}

/* Priority scheduling, preemptive (lower number = higher priority) */
void priority_p(Process p[], int n, GanttChart *g)
{
    run_preemptive(p, n, g, MODE_PRIORITY);
}
