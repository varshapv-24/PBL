/*
 * stats.c  
 * Helper functions used by FIFO, LRU and Optimal, plus the comparison logic.
 */
#include <stdio.h>
#include <string.h>
#include "pageswap.h"

/* Prepares an empty Result before an algorithm starts. */
void result_init(Result *r, const char *name, int n_steps, int n_frames)
{
    memset(r, 0, sizeof(*r));
    snprintf(r->name, sizeof(r->name), "%s", name);
    r->n_steps = n_steps;
    r->n_frames = n_frames;
}

/* Stores one row of the simulation table (state AFTER request `index`). */
void result_record(Result *r, int index, int page, const int *frames,
                   int n_frames, int hit, int replaced)
{
    Step *s = &r->steps[index];
    s->page = page;
    for (int f = 0; f < n_frames; f++) s->frames[f] = frames[f];
    s->hit = hit;
    s->replaced = replaced;
}

/* Counts hits/faults and computes the two ratios (spec section 5, step 8). */
void result_finalize(Result *r)
{
    r->hits = 0;
    r->faults = 0;
    for (int i = 0; i < r->n_steps; i++) {
        if (r->steps[i].hit) r->hits++;
        else r->faults++;
    }
    if (r->n_steps > 0) {
        r->hit_ratio   = (double)r->hits   / r->n_steps * 100.0;
        r->fault_ratio = (double)r->faults / r->n_steps * 100.0;
    }
}

/* Writes the name(s) of the algorithm(s) with the FEWEST faults into out.
 * If several tie, they are joined with " = " (e.g. "LRU = Optimal"). */
void find_best(const Result *results, int count, char *out, size_t cap)
{
    int min = results[0].faults;
    size_t len = 0;

    for (int i = 1; i < count; i++)
        if (results[i].faults < min) min = results[i].faults;

    out[0] = '\0';
    for (int i = 0; i < count; i++) {
        if (results[i].faults != min) continue;
        int w = snprintf(out + len, cap - len, "%s%s",
                         len ? " = " : "", results[i].name);
        if (w < 0 || (size_t)w >= cap - len) break;
        len += (size_t)w;
    }
}
