/*
 * fcfs_sjf_priority.c  
 *
 * Non-preemptive scheduling algorithms:
 *   fcfs()         First-Come-First-Served
 *   sjf()          Shortest Job First (non-preemptive)
 *   priority_np()  Priority scheduling (non-preemptive)
 *
 * Tie-breaking (same rule as the rest of the team):
 *   earlier arrival first, then smaller pid.
 *
 * If no process has arrived yet, the CPU is idle until the next arrival
 * (recorded as an IDLE_PID block in the Gantt chart).
 *
 * These functions do not print anything and do not calculate WT / TAT / RT;
 * calculate_metrics() in metrics.c does that.
 */

#include "scheduler.h"

/* ------------------------------------------------------------------ */
/* FCFS                                                                */
/* ------------------------------------------------------------------ */

void fcfs(Process p[], int n, GanttChart *g)
{
    int order[MAX_PROCESSES];   /* process indices sorted by arrival */
    int t = 0;

    gantt_init(g);
    if (n < 1 || n > MAX_PROCESSES)
        return;
    reset_processes(p, n);

    /* sort indices by arrival time, then pid (insertion sort) */
    for (int i = 0; i < n; i++)
        order[i] = i;
    for (int i = 1; i < n; i++) {
        int key = order[i];
        int j = i - 1;
        while (j >= 0 &&
               (p[order[j]].arrival_time > p[key].arrival_time ||
                (p[order[j]].arrival_time == p[key].arrival_time &&
                 p[order[j]].pid > p[key].pid))) {
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = key;
    }

    /* run the processes one after another in that order */
    for (int k = 0; k < n; k++) {
        int i = order[k];

        if (t < p[i].arrival_time) {                 /* CPU idle gap */
            gantt_add(g, IDLE_PID, t, p[i].arrival_time);
            t = p[i].arrival_time;
        }

        p[i].start_time = t;
        gantt_add(g, p[i].pid, t, t + p[i].burst_time);
        t += p[i].burst_time;
        p[i].completion_time = t;
        p[i].remaining_time = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Shared engine for SJF and Priority (they differ only in the key)    */
/* ------------------------------------------------------------------ */

enum { BY_BURST, BY_PRIORITY };

/* The number that decides who is chosen: smaller key wins. */
static int key_of(const Process *x, int mode)
{
    return (mode == BY_BURST) ? x->burst_time : x->priority;
}

/* Is process a a better choice than process b? */
static int better(const Process *a, const Process *b, int mode)
{
    if (key_of(a, mode) != key_of(b, mode))
        return key_of(a, mode) < key_of(b, mode);
    if (a->arrival_time != b->arrival_time)
        return a->arrival_time < b->arrival_time;
    return a->pid < b->pid;
}

static void non_preemptive(Process p[], int n, GanttChart *g, int mode)
{
    int t = 0;
    int done = 0;

    gantt_init(g);
    if (n < 1 || n > MAX_PROCESSES)
        return;
    reset_processes(p, n);

    while (done < n) {
        int best = -1;

        /* among processes that have arrived and are not finished,
         * pick the best one */
        for (int i = 0; i < n; i++) {
            if (p[i].remaining_time == 0 || p[i].arrival_time > t)
                continue;
            if (best == -1 || better(&p[i], &p[best], mode))
                best = i;
        }

        /* nobody is ready: CPU idles until the next arrival */
        if (best == -1) {
            int next_arrival = -1;
            for (int i = 0; i < n; i++) {
                if (p[i].remaining_time == 0)
                    continue;
                if (next_arrival == -1 || p[i].arrival_time < next_arrival)
                    next_arrival = p[i].arrival_time;
            }
            gantt_add(g, IDLE_PID, t, next_arrival);
            t = next_arrival;
            continue;
        }

        /* run the chosen process to completion (non-preemptive) */
        p[best].start_time = t;
        gantt_add(g, p[best].pid, t, t + p[best].burst_time);
        t += p[best].burst_time;
        p[best].completion_time = t;
        p[best].remaining_time = 0;
        done++;
    }
}

/* ------------------------------------------------------------------ */
/* SJF and Priority                                                    */
/* ------------------------------------------------------------------ */

/* Shortest Job First, non-preemptive: smallest burst time wins. */
void sjf(Process p[], int n, GanttChart *g)
{
    non_preemptive(p, n, g, BY_BURST);
}

/* Priority, non-preemptive: smallest priority NUMBER wins
 * (lower number = higher priority). */
void priority_np(Process p[], int n, GanttChart *g)
{
    non_preemptive(p, n, g, BY_PRIORITY);
}
