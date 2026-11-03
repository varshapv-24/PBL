/*
 * fifo.c  
 * First-In First-Out page replacement.
 * Rule: when memory is full, evict the page that has been in memory longest.
 */
#include "pageswap.h"

void run_fifo(const int *refs, int n, int n_frames, Result *out)
{
    int frames[MAX_FRAMES];
    int next = 0;                    /* slot holding the OLDEST page */

    result_init(out, "FIFO", n, n_frames);
    for (int f = 0; f < n_frames; f++) frames[f] = EMPTY;

    for (int i = 0; i < n; i++) {
        int page = refs[i];
        int hit = 0;
        int replaced = EMPTY;

        for (int f = 0; f < n_frames; f++) {
            if (frames[f] == page) { hit = 1; break; }
        }

        if (!hit) {
            replaced = frames[next];         /* EMPTY while frames are filling */
            frames[next] = page;
            next = (next + 1) % n_frames;    /* the next-oldest page */
        }
        result_record(out, i, page, frames, n_frames, hit, replaced);
    }
    result_finalize(out);
}
