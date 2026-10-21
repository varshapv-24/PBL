/* logic and testing */
#include "deadlock.h"
#include <stdio.h>
#include <string.h>

static int vec_leq(const int a[], const int b[], int m) {
    for (int j = 0; j < m; j++)
        if (a[j] > b[j]) return 0;
    return 1;
}

void sys_init(System *s, int n, int m) {
    memset(s, 0, sizeof *s);
    s->n = n;
    s->m = m;
}

void compute_available(System *s) {
    for (int j = 0; j < s->m; j++) {
        int used = 0;
        for (int i = 0; i < s->n; i++) used += s->alloc[i][j];
        s->available[j] = s->total[j] - used;
    }
}

void compute_need(System *s) {
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++)
            s->need[i][j] = s->terminated[i] ? 0 : s->max[i][j] - s->alloc[i][j];
}

int validate_system(const System *s, char *err, size_t len) {
    if (s->n < 1 || s->n > MAX_P || s->m < 1 || s->m > MAX_R) {
        snprintf(err, len, "Processes must be 1..%d and resources 1..%d", MAX_P, MAX_R);
        return 0;
    }
    for (int i = 0; i < s->n; i++)
        for (int j = 0; j < s->m; j++)
            if (s->alloc[i][j] > s->max[i][j]) {
                snprintf(err, len, "P%d holds more of R%d than its maximum", i, j);
                return 0;
            }
    for (int j = 0; j < s->m; j++) {
        int used = 0;
        for (int i = 0; i < s->n; i++) used += s->alloc[i][j];
        if (used > s->total[j]) {
            snprintf(err, len, "Allocated R%d (%d) exceeds total (%d)", j, used, s->total[j]);
            return 0;
        }
    }
    return 1;
}

/* Safety algorithm. Needs s->need to be up to date. */
int is_safe(const System *s, int seq[], int *seq_len) {
    int work[MAX_R], finish[MAX_P], count = 0, progress = 1;

    memcpy(work, s->available, sizeof work);
    for (int i = 0; i < s->n; i++) finish[i] = s->terminated[i];

    while (progress) {
        progress = 0;
        for (int i = 0; i < s->n; i++) {
            if (!finish[i] && vec_leq(s->need[i], work, s->m)) {
                for (int j = 0; j < s->m; j++) work[j] += s->alloc[i][j];
                finish[i] = 1;
                if (seq) seq[count] = i;
                count++;
                progress = 1;
            }
        }
    }
    if (seq_len) *seq_len = count;
    for (int i = 0; i < s->n; i++)
        if (!finish[i]) return 0;
    return 1;
}

void set_pending_request(System *s, int p, const int req[]) {
    memcpy(s->request[p], req, sizeof(int) * s->m);
}

ReqResult request_resources(System *s, int p, const int req[]) {
    if (p < 0 || p >= s->n || s->terminated[p]) return REQ_INVALID;
    for (int j = 0; j < s->m; j++)
        if (req[j] < 0) return REQ_INVALID;

    if (!vec_leq(req, s->need[p], s->m)) return REQ_EXCEEDS_NEED;

    if (!vec_leq(req, s->available, s->m)) {
        set_pending_request(s, p, req);
        return REQ_WAIT;
    }

    /* pretend to allocate */
    for (int j = 0; j < s->m; j++) {
        s->available[j] -= req[j];
        s->alloc[p][j]  += req[j];
        s->need[p][j]   -= req[j];
    }
    if (is_safe(s, NULL, NULL)) {
        memset(s->request[p], 0, sizeof s->request[p]);
        return REQ_GRANTED;
    }
    /* roll back */
    for (int j = 0; j < s->m; j++) {
        s->available[j] += req[j];
        s->alloc[p][j]  -= req[j];
        s->need[p][j]   += req[j];
    }
    set_pending_request(s, p, req);
    return REQ_UNSAFE;
}

/* Detection: a process that can never get its pending request is deadlocked. */
int detect_deadlock(const System *s, int dead[]) {
    int work[MAX_R], finish[MAX_P], progress = 1, count = 0;

    memcpy(work, s->available, sizeof work);
    for (int i = 0; i < s->n; i++) {
        int holds = 0;
        for (int j = 0; j < s->m; j++) holds += s->alloc[i][j];
        finish[i] = s->terminated[i] || holds == 0;
    }
    while (progress) {
        progress = 0;
        for (int i = 0; i < s->n; i++) {
            if (!finish[i] && vec_leq(s->request[i], work, s->m)) {
                for (int j = 0; j < s->m; j++) work[j] += s->alloc[i][j];
                finish[i] = 1;
                progress = 1;
            }
        }
    }
    for (int i = 0; i < s->n; i++)
        if (!finish[i]) { if (dead) dead[count] = i; count++; }
    return count;
}

SysState get_state(const System *s) {
    if (detect_deadlock(s, NULL) > 0) return STATE_DEADLOCKED;
    return is_safe(s, NULL, NULL) ? STATE_SAFE : STATE_UNSAFE;
}

/* Cheapest victim = deadlocked process holding the fewest resources. */
int choose_victim(const System *s, const int dead[], int count) {
    int best = -1, best_cost = 0;
    for (int k = 0; k < count; k++) {
        int p = dead[k], cost = 0;
        for (int j = 0; j < s->m; j++) cost += s->alloc[p][j];
        if (best < 0 || cost < best_cost) { best = p; best_cost = cost; }
    }
    return best;
}

static void release_all(System *s, int p) {
    for (int j = 0; j < s->m; j++) {
        s->available[j] += s->alloc[p][j];
        s->alloc[p][j] = 0;
        s->request[p][j] = 0;
    }
}

void recover_terminate(System *s, int p) {
    release_all(s, p);
    for (int j = 0; j < s->m; j++) { s->max[p][j] = 0; s->need[p][j] = 0; }
    s->terminated[p] = 1;
}

void recover_preempt(System *s, int p) {
    release_all(s, p);
    for (int j = 0; j < s->m; j++) s->need[p][j] = s->max[p][j];
}

void resource_utilization(const System *s, double util[]) {
    for (int j = 0; j < s->m; j++)
        util[j] = s->total[j] > 0
                ? 100.0 * (s->total[j] - s->available[j]) / s->total[j] : 0.0;
}

const char *req_result_str(ReqResult r) {
    switch (r) {
        case REQ_GRANTED:      return "GRANTED";
        case REQ_EXCEEDS_NEED: return "DENIED: exceeds declared maximum";
        case REQ_WAIT:         return "WAIT: resources not available";
        case REQ_UNSAFE:       return "DENIED: would lead to an unsafe state";
        default:               return "INVALID request";
    }
}

const char *state_str(SysState st) {
    switch (st) {
        case STATE_SAFE:       return "SAFE";
        case STATE_UNSAFE:     return "UNSAFE";
        default:               return "DEADLOCKED";
    }
}
