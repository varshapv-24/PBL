/*
 * round_robin.c  --
 *
 * Round Robin scheduling with a user-given time quantum.
 *
 * Rules used:
 *  - Processes join the ready queue in order of arrival (ties: smaller pid).
 *  - A process that arrives while another is running (including exactly
 *    when its time slice ends) is queued BEFORE the preempted process
 *    goes back to the end of the queue.
 *  - If the queue is empty, the CPU is idle until the next arrival
 *    (recorded as an IDLE_PID block in the Gantt chart).
 */

#include "scheduler.h"

void round_robin(Process p[], int n, int quantum, GanttChart *g)
{
    int order[MAX_PROCESSES];   /* process indices sorted by arrival */
    int queue[MAX_PROCESSES];   /* circular ready queue of indices   */
    int head = 0, tail = 0, size = 0;
    int next = 0;               /* next process in 'order' to enqueue */
    int done = 0;
    int t = 0;

    gantt_init(g);
    if (n < 1 || n > MAX_PROCESSES)
        return;                 /* safety: arrays below hold MAX_PROCESSES */
    reset_processes(p, n);

    if (quantum < 1)
        quantum = 1;            /* safety: avoid an infinite loop */

    /* sort indices by arrival time, then by pid (insertion sort) */
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

    while (done < n) {
        /* put every process that has arrived by time t into the queue */
        while (next < n && p[order[next]].arrival_time <= t) {
            queue[tail] = order[next++];
            tail = (tail + 1) % MAX_PROCESSES;
            size++;
        }

        /* nothing ready: CPU idles until the next arrival */
        if (size == 0) {
            int arrive = p[order[next]].arrival_time;
            gantt_add(g, IDLE_PID, t, arrive);
            t = arrive;
            continue;
        }

        /* take the process at the front of the queue */
        int i = queue[head];
        head = (head + 1) % MAX_PROCESSES;
        size--;

        if (p[i].start_time == -1)
            p[i].start_time = t;            /* first time on the CPU */

        int run = (p[i].remaining_time < quantum) ? p[i].remaining_time
                                                  : quantum;
        gantt_add(g, p[i].pid, t, t + run);
        t += run;
        p[i].remaining_time -= run;

        /* processes that arrived during this slice go in first */
        while (next < n && p[order[next]].arrival_time <= t) {
            queue[tail] = order[next++];
            tail = (tail + 1) % MAX_PROCESSES;
            size++;
        }

        if (p[i].remaining_time > 0) {
            /* not finished: back to the end of the queue */
            queue[tail] = i;
            tail = (tail + 1) % MAX_PROCESSES;
            size++;
        } else {
            p[i].completion_time = t;
            done++;
        }
    }
}
