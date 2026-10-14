/*
 * gantt.c  -- 
 *
 * gantt_display() prints the Gantt chart as text, for example:
 *
 * +------+------+------+
 * |  P1  | idle |  P2  |
 * +------+------+------+
 * 0      3      6      9
 *
 * Long charts wrap onto several rows so they fit in the terminal.
 * main.c prints the "Gantt Chart:" heading, so it is not printed here.
 */

#include <stdio.h>
#include <string.h>
#include "scheduler.h"

#define CELL_WIDTH       6    /* characters between two '|' bars   */
#define BLOCKS_PER_ROW  10    /* wrap after this many blocks       */

static void print_border(int blocks)
{
    for (int i = 0; i < blocks; i++) {
        putchar('+');
        for (int k = 0; k < CELL_WIDTH; k++)
            putchar('-');
    }
    printf("+\n");
}

void gantt_display(const GanttChart *g)
{
    if (g->count == 0) {
        printf("(empty chart)\n");
        return;
    }

    for (int row = 0; row < g->count; row += BLOCKS_PER_ROW) {
        int last = row + BLOCKS_PER_ROW;
        if (last > g->count)
            last = g->count;
        int blocks = last - row;

        /* top border */
        print_border(blocks);

        /* process names, centred in each cell */
        for (int i = row; i < last; i++) {
            char label[16];
            if (g->blocks[i].pid == IDLE_PID)
                snprintf(label, sizeof label, "idle");
            else
                snprintf(label, sizeof label, "P%d", g->blocks[i].pid);

            int len = (int)strlen(label);
            if (len > CELL_WIDTH)
                len = CELL_WIDTH;
            int left  = (CELL_WIDTH - len) / 2;
            int right = CELL_WIDTH - len - left;

            printf("|%*s%.*s%*s", left, "", len, label, right, "");
        }
        printf("|\n");

        /* bottom border */
        print_border(blocks);

        /* time marks, lined up with the left bar of each cell */
        for (int i = row; i < last; i++)
            printf("%-*d", CELL_WIDTH + 1, g->blocks[i].start);
        printf("%d\n", g->blocks[last - 1].end);

        if (last < g->count)
            printf("\n");
    }

    if (g->overflow)
        printf("\nNote: the chart is too long, so only the first %d blocks "
               "are shown.\n", g->count);
}
