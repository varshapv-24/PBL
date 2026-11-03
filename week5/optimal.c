/*
 * optimal.c  
 * Optimal (Belady's) page replacement.
 * Rule: when memory is full, evict the page that will not be used for the
 * longest time in the future (or never used again). It needs to see the
 * whole reference string, so it is a benchmark, not a real OS algorithm.
 */
#include "pageswap.h"

/* Index of the next request for `page` after position `from`; n if never. */
static int next_use(const int *refs, int n, int from, int page)
{
    for (int j = from + 1; j < n; j++) {
        if (refs[j] == page) return j;
    }
    return n;
}

void run_optimal(const int *refs, int n, int n_frames, Result *out)
{
    int frames[MAX_FRAMES];

    result_init(out, "Optimal", n, n_frames);
    for (int f = 0; f < n_frames; f++) frames[f] = EMPTY;

    for (int i = 0; i < n; i++) {
        int page = refs[i];
        int hit = 0;
        int replaced = EMPTY;

        for (int f = 0; f < n_frames; f++) {
            if (frames[f] == page) { hit = 1; break; }
        }

        if (!hit) {
            int victim = -1;

            for (int f = 0; f < n_frames; f++) {
                if (frames[f] == EMPTY) { victim = f; break; }
            }
            if (victim == -1) {
                int farthest = -1;
                for (int f = 0; f < n_frames; f++) {
                    int when = next_use(refs, n, i, frames[f]);
                    if (when > farthest) { farthest = when; victim = f; }
                }
                replaced = frames[victim];
            }
            frames[victim] = page;
        }
        result_record(out, i, page, frames, n_frames, hit, replaced);
    }
    result_finalize(out);
}
