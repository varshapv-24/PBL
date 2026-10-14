/*
 * main.c  --  Leader's file
 *
 * Input, menu, and running the chosen scheduling algorithm.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scheduler.h"

/* ------------------------------------------------------------------ */
/* Safe integer input                                                  */
/* ------------------------------------------------------------------ */

/* Keeps asking until the user types an integer between min and max.
 * Exits the program cleanly if input ends (Ctrl+D / end of file). */
static int read_int(const char *prompt, int min, int max)
{
    char line[64];
    char *end;

    for (;;) {
        printf("%s", prompt);
        fflush(stdout);

        if (fgets(line, sizeof line, stdin) == NULL) {
            printf("\nInput ended. Exiting.\n");
            exit(0);
        }

        long v = strtol(line, &end, 10);
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
            end++;

        if (end == line || *end != '\0')
            printf("  Please enter a whole number.\n");
        else if (v < min || v > max)
            printf("  Please enter a value between %d and %d.\n", min, max);
        else
            return (int)v;
    }
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */

void input_processes(Process p[], int n)
{
    char prompt[64];

    for (int i = 0; i < n; i++) {
        int pid, duplicate;

        printf("\n--- Process %d of %d ---\n", i + 1, n);

        do {
            duplicate = 0;
            pid = read_int("  Process ID      : ", 0, 9999);
            for (int j = 0; j < i; j++) {
                if (p[j].pid == pid) {
                    printf("  ID %d is already used. Choose a different ID.\n",
                           pid);
                    duplicate = 1;
                    break;
                }
            }
        } while (duplicate);

        p[i].pid = pid;
        snprintf(prompt, sizeof prompt, "  Arrival Time    : ");
        p[i].arrival_time = read_int(prompt, 0, 100000);
        snprintf(prompt, sizeof prompt, "  Burst Time      : ");
        p[i].burst_time = read_int(prompt, 1, 100000);
        snprintf(prompt, sizeof prompt,
                 "  Priority (lower number = higher priority): ");
        p[i].priority = read_int(prompt, 0, 100000);
    }

    reset_processes(p, n);
}

static void show_processes(const Process p[], int n)
{
    const int W = 8;

    printf("\nCurrent processes:\n");
    for (int c = 0; c < 4; c++) { putchar('+'); for (int k = 0; k < W; k++) putchar('-'); }
    printf("+\n");
    printf("|%*s|%*s|%*s|%*s|\n", W, "PID", W, "AT", W, "BT", W, "PR");
    for (int c = 0; c < 4; c++) { putchar('+'); for (int k = 0; k < W; k++) putchar('-'); }
    printf("+\n");
    for (int i = 0; i < n; i++)
        printf("|%*d|%*d|%*d|%*d|\n", W, p[i].pid, W, p[i].arrival_time,
               W, p[i].burst_time, W, p[i].priority);
    for (int c = 0; c < 4; c++) { putchar('+'); for (int k = 0; k < W; k++) putchar('-'); }
    printf("+\n");
}

/* ------------------------------------------------------------------ */
/* Running an algorithm                                                */
/* ------------------------------------------------------------------ */

enum {
    CH_ENTER = 1, CH_FCFS, CH_SJF, CH_SRTF, CH_PRIO_NP, CH_PRIO_P, CH_RR,
    CH_SHOW, CH_EXIT
};

static void run_algorithm(int choice, const Process input[], int n)
{
    Process work[MAX_PROCESSES];   /* working copy: input stays untouched */
    GanttChart g;
    Metrics m;
    const char *name = "";

    memcpy(work, input, (size_t)n * sizeof(Process));

    switch (choice) {
    case CH_FCFS:
        name = "FCFS (First-Come-First-Served)";
        fcfs(work, n, &g);
        break;
    case CH_SJF:
        name = "SJF (Non-preemptive)";
        sjf(work, n, &g);
        break;
    case CH_SRTF:
        name = "SRTF (Preemptive SJF)";
        srtf(work, n, &g);
        break;
    case CH_PRIO_NP:
        name = "Priority (Non-preemptive)";
        priority_np(work, n, &g);
        break;
    case CH_PRIO_P:
        name = "Priority (Preemptive)";
        priority_p(work, n, &g);
        break;
    case CH_RR: {
        int quantum = read_int("Enter time quantum: ", 1, 100000);
        static char rr_name[64];
        snprintf(rr_name, sizeof rr_name,
                 "Round Robin (quantum = %d)", quantum);
        name = rr_name;
        round_robin(work, n, quantum, &g);
        break;
    }
    default:
        return;
    }

    calculate_metrics(work, n, &g, &m);

    printf("\n==================== %s ====================\n", name);
    printf("\nGantt Chart:\n");
    gantt_display(&g);
    display_results(work, n, &m);
}

/* ------------------------------------------------------------------ */
/* Menu                                                                */
/* ------------------------------------------------------------------ */

static void print_menu(int n)
{
    printf("\n=========== CPU Scheduling Simulator ===========\n");
    if (n > 0)
        printf("  (%d process%s loaded)\n", n, n == 1 ? "" : "es");
    else
        printf("  (no processes loaded yet)\n");
    printf("  1. Enter / change processes\n");
    printf("  2. FCFS\n");
    printf("  3. SJF (Non-preemptive)\n");
    printf("  4. SRTF (Preemptive SJF)\n");
    printf("  5. Priority (Non-preemptive)\n");
    printf("  6. Priority (Preemptive)\n");
    printf("  7. Round Robin\n");
    printf("  8. Show current processes\n");
    printf("  9. Exit\n");
    printf("================================================\n");
}

int main(void)
{
    Process procs[MAX_PROCESSES];
    int n = 0;

    for (;;) {
        print_menu(n);
        int choice = read_int("Choose an option: ", 1, 9);

        if (choice == CH_EXIT) {
            printf("Goodbye!\n");
            break;
        }

        if (choice == CH_ENTER) {
            char prompt[64];
            snprintf(prompt, sizeof prompt,
                     "Number of processes (1-%d): ", MAX_PROCESSES);
            n = read_int(prompt, 1, MAX_PROCESSES);
            input_processes(procs, n);
            show_processes(procs, n);
            continue;
        }

        if (n == 0) {
            printf("\nNo processes yet. Choose option 1 first.\n");
            continue;
        }

        if (choice == CH_SHOW)
            show_processes(procs, n);
        else
            run_algorithm(choice, procs, n);
    }

    return 0;
}
