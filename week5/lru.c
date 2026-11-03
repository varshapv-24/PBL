/*
 * lru.c  
 * Least Recently Used page replacement.
 * Rule: when memory is full, evict the page that was used longest ago.
 */
#include "pageswap.h"

void run_lru(const int *refs, int n, int n_frames, Result *out)
{
    int frames[MAX_FRAMES];
    int last_used[MAX_FRAMES];       /* index of the last request that used each frame */

    result_init(out, "LRU", n, n_frames);
    for (int f = 0; f < n_frames; f++) {
        frames[f] = EMPTY;
        last_used[f] = -1;
    }

    for (int i = 0; i < n; i++) {
        int page = refs[i];
        int hit = 0;
        int replaced = EMPTY;

        /* 1. Is the page already in memory? */
        for (int f = 0; f < n_frames; f++) {
            if (frames[f] == page) {
                hit = 1;
                last_used[f] = i;    /* a hit refreshes the "recently used" time */
                break;
            }
        }

        /* 2. Page fault: use an empty frame, otherwise evict the LRU page. */
        if (!hit) {
            int victim = -1;

            for (int f = 0; f < n_frames; f++) {
                if (frames[f] == EMPTY) { victim = f; break; }
            }
            if (victim == -1) {
                victim = 0;
                for (int f = 1; f < n_frames; f++) {
                    if (last_used[f] < last_used[victim]) victim = f;
                }
                replaced = frames[victim];
            }
            frames[victim] = page;
            last_used[victim] = i;
        }
        result_record(out, i, page, frames, n_frames, hit, replaced);
    }
    result_finalize(out);
}
