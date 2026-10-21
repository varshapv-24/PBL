/* logic and testing */
#ifndef DEADLOCK_H
#define DEADLOCK_H

#include <stddef.h>

#define MAX_P 10
#define MAX_R 10

typedef struct {
    int n, m;                      /* processes, resource types          */
    int total[MAX_R];              /* total instances of each resource   */
    int available[MAX_R];          /* Available vector                   */
    int alloc[MAX_P][MAX_R];       /* Allocation matrix                  */
    int max[MAX_P][MAX_R];         /* Maximum matrix                     */
    int need[MAX_P][MAX_R];        /* Need = Max - Allocation            */
    int request[MAX_P][MAX_R];     /* pending (blocked) requests         */
    int terminated[MAX_P];         /* 1 if process was killed/finished   */
} System;

typedef enum { STATE_SAFE, STATE_UNSAFE, STATE_DEADLOCKED } SysState;

typedef enum {
    REQ_GRANTED,         /* request granted, state stays safe            */
    REQ_EXCEEDS_NEED,    /* error: asks for more than its declared max   */
    REQ_WAIT,            /* not enough Available, process must wait      */
    REQ_UNSAFE,          /* would leave system unsafe, denied (rolled back) */
    REQ_INVALID          /* bad process id / negative values / terminated */
} ReqResult;

/* setup */
void sys_init(System *s, int n, int m);
void compute_available(System *s);                 /* from total - sum(alloc) */
void compute_need(System *s);                      /* Need = Max - Alloc      */
int  validate_system(const System *s, char *err, size_t len); /* 1 = ok */

/* Banker's algorithm */
int  is_safe(const System *s, int seq[], int *seq_len);       /* 1 = safe   */
ReqResult request_resources(System *s, int p, const int req[]);
void set_pending_request(System *s, int p, const int req[]);  /* no checks  */

/* detection and state */
int  detect_deadlock(const System *s, int dead[]);            /* returns count */
SysState get_state(const System *s);

/* recovery */
int  choose_victim(const System *s, const int dead[], int count);
void recover_terminate(System *s, int p);          /* kill, free everything   */
void recover_preempt(System *s, int p);            /* take resources, restart */

/* analysis */
void resource_utilization(const System *s, double util[]);    /* percent per resource */

const char *req_result_str(ReqResult r);
const char *state_str(SysState st);

#endif
