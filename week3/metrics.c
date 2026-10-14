/*
 * metrics.c  
 *
 * calculate_metrics() : fills WT / TAT / RT for every process and the
 *                       averages + CPU utilization for the whole run
 * display_results()   : prints the results table
 */

#include <stdio.h>
#include "scheduler.h"

void calculate_metrics(Process p[], int n, const GanttChart *g, Metrics *m)
{
    double sum_wt = 0, sum_tat = 0, sum_rt = 0;
    int total_burst = 0;
    int first_arrival = p[0].arrival_time;
    int last_end = 0;
    int busy = 0;

    /* ---- per-process metrics ---- */
    for (int i = 0; i < n; i++) {
        /* Safety check: the algorithm must set start_time */
        if (p[i].start_time < 0) {
            fprintf(stderr,
                    "Warning: P%d never got the CPU (start_time not set).\n",
                    p[i].pid);
            p[i].start_time = p[i].arrival_time;
        }

        p[i].turnaround_time = p[i].completion_time - p[i].arrival_time;
        p[i].waiting_time    = p[i].turnaround_time - p[i].burst_time;
        p[i].response_time   = p[i].start_time - p[i].arrival_time;

        sum_tat += p[i].turnaround_time;
        sum_wt  += p[i].waiting_time;
        sum_rt  += p[i].response_time;

        total_burst += p[i].burst_time;
        if (p[i].arrival_time < first_arrival)
            first_arrival = p[i].arrival_time;
        if (p[i].completion_time > last_end)
            last_end = p[i].completion_time;
    }

    /* ---- busy time from the Gantt chart (idle blocks don't count) ---- */
    for (int i = 0; i < g->count; i++) {
        if (g->blocks[i].pid != IDLE_PID)
            busy += g->blocks[i].end - g->blocks[i].start;
        if (g->blocks[i].end > last_end)
            last_end = g->blocks[i].end;
    }

    /* If the chart ran out of room, its busy time is incomplete, so use
     * the total burst time instead (every process runs its full burst). */
    if (g->overflow) {
        fprintf(stderr,
                "Warning: Gantt chart was too long and was truncated; "
                "metrics are still correct.\n");
        busy = total_burst;
    }

    /* Safety check: busy time must equal the total burst time */
    if (busy != total_burst) {
        fprintf(stderr,
                "Warning: Gantt chart shows %d busy time units but the "
                "total burst time is %d.\n"
                "         An algorithm is not recording its Gantt chart "
                "correctly.\n", busy, total_burst);
    }

    /* ---- averages ---- */
    m->avg_waiting    = sum_wt  / n;
    m->avg_turnaround = sum_tat / n;
    m->avg_response   = sum_rt  / n;
    m->total_time     = last_end;

    /* CPU utilization = busy time / elapsed time * 100.
     * Elapsed time is counted from the first arrival, so waiting for
     * the very first process to show up is not counted as idle. */
    int elapsed = last_end - first_arrival;
    m->cpu_utilization = (elapsed > 0) ? (100.0 * busy / elapsed) : 0.0;
}

#define COL 8   /* width of each number column in the results table */

static void table_line(void)
{
    for (int c = 0; c < 8; c++) {
        putchar('+');
        for (int k = 0; k < COL; k++)
            putchar('-');
    }
    printf("+\n");
}

void display_results(const Process p[], int n, const Metrics *m)
{
    printf("\nResults:\n");
    table_line();
    printf("|%*s|%*s|%*s|%*s|%*s|%*s|%*s|%*s|\n",
           COL, "PID", COL, "AT", COL, "BT", COL, "PR",
           COL, "CT", COL, "TAT", COL, "WT", COL, "RT");
    table_line();
    for (int i = 0; i < n; i++) {
        printf("|%*d|%*d|%*d|%*d|%*d|%*d|%*d|%*d|\n",
               COL, p[i].pid,
               COL, p[i].arrival_time,
               COL, p[i].burst_time,
               COL, p[i].priority,
               COL, p[i].completion_time,
               COL, p[i].turnaround_time,
               COL, p[i].waiting_time,
               COL, p[i].response_time);
    }
    table_line();
    printf("AT = Arrival, BT = Burst, PR = Priority, CT = Completion,\n");
    printf("TAT = Turnaround, WT = Waiting, RT = Response\n\n");

    printf("Average Waiting Time    : %.2f\n", m->avg_waiting);
    printf("Average Turnaround Time : %.2f\n", m->avg_turnaround);
    printf("Average Response Time   : %.2f\n", m->avg_response);
    printf("Total Time              : %d\n",   m->total_time);
    printf("CPU Utilization         : %.2f%%\n", m->cpu_utilization);
}
