/*
 * pageswap.h 
 * ---------------------------------------------------------------
 * Every .c file includes this header. It defines the data structures
 * and the function prototypes that connect everyone's work.
 * Do NOT change anything here without telling the whole team.
 */
#ifndef PAGESWAP_H
#define PAGESWAP_H

#include <stddef.h>

#define MAX_REFS     200      /* longest reference string accepted      */
#define MAX_FRAMES   10       /* most page frames accepted              */
#define MAX_PAGE_NUM 9999     /* largest page number accepted           */
#define EMPTY        (-1)     /* marks an empty frame / "nothing evicted" */

/* One row of the simulation table (one page request). */
typedef struct {
    int page;                    /* page requested                       */
    int frames[MAX_FRAMES];      /* frame contents AFTER this request    */
    int hit;                     /* 1 = page hit, 0 = page fault         */
    int replaced;                /* page evicted, or EMPTY if none       */
} Step;

/* Complete result of running one algorithm. */
typedef struct {
    char   name[16];             /* "FIFO", "LRU" or "Optimal"           */
    int    n_frames;
    int    n_steps;
    Step   steps[MAX_REFS];
    int    hits;
    int    faults;
    double hit_ratio;            /* percent                              */
    double fault_ratio;          /* percent                              */
} Result;

/* ---- Member 1 : validation.c ---------------------------------------- */
int parse_refs(const char *text, int *refs, int *n, char *err, size_t err_len);
int validate_frames(int frames, char *err, size_t err_len);

/* ---- Member 1 : fifo.c ---------------------------------------------- */
void run_fifo(const int *refs, int n, int n_frames, Result *out);

/* ---- Member 2 : lru.c ------------------------------------------------ */
void run_lru(const int *refs, int n, int n_frames, Result *out);

/* ---- Member 2 : json.c ----------------------------------------------- */
size_t json_build_success(const Result *results, int count, const char *best,
                          char *buf, size_t cap);
size_t json_build_error(const char *message, char *buf, size_t cap);

/* ---- Member 3 : optimal.c -------------------------------------------- */
void run_optimal(const int *refs, int n, int n_frames, Result *out);

/* ---- Member 3 : stats.c (helpers used by ALL algorithms) ------------- */
void result_init(Result *r, const char *name, int n_steps, int n_frames);
void result_record(Result *r, int index, int page, const int *frames,
                   int n_frames, int hit, int replaced);
void result_finalize(Result *r);
void find_best(const Result *results, int count, char *out, size_t cap);

#endif /* PAGESWAP_H */
