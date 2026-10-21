/* logic and testing */
#include <stdio.h>
#include <string.h>
#include "deadlock.h"

static int fails = 0;
#define CHECK(c, n) do { int ok = (c); printf("[%s] %s\n", ok ? "PASS" : "FAIL", n); if (!ok) fails++; } while (0)

static void setup(System *s, int n, int m, const int *total, const int *alloc, const int *max) {
    sys_init(s, n, m);
    for (int j = 0; j < m; j++) s->total[j] = total[j];
    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++) { s->alloc[i][j] = alloc[i * m + j]; s->max[i][j] = max[i * m + j]; }
    compute_available(s);
    compute_need(s);
}

/* 3 processes in a circular wait over 3 single-instance resources */
static void cycle(System *s) {
    int t[3] = {1,1,1}, a[9] = {1,0,0, 0,1,0, 0,0,1}, m[9] = {1,1,1, 1,1,1, 1,1,1};
    int w0[3] = {0,1,0}, w1[3] = {0,0,1}, w2[3] = {1,0,0};
    setup(s, 3, 3, t, a, m);
    set_pending_request(s, 0, w0); set_pending_request(s, 1, w1); set_pending_request(s, 2, w2);
}

int main(void) {
    System s; char err[100];

    /* unsafe but not deadlocked: 12 units, nobody is waiting */
    int t1[1] = {12}, a1[3] = {5,2,4}, m1[3] = {10,4,9};
    setup(&s, 3, 1, t1, a1, m1);
    CHECK(!is_safe(&s, NULL, NULL), "UNSAFE state has no safe sequence");
    CHECK(detect_deadlock(&s, NULL) == 0, "unsafe state with no waiting is not deadlock");
    CHECK(get_state(&s) == STATE_UNSAFE, "classified UNSAFE");

    /* invalid requests */
    int neg[1] = {-1}, one[1] = {1};
    CHECK(request_resources(&s, 0, neg) == REQ_INVALID, "negative request rejected");
    CHECK(request_resources(&s, 7, one) == REQ_INVALID, "unknown process rejected");

    /* validation */
    int t2[1] = {10}, a2[1] = {5}, m2[1] = {3};
    setup(&s, 1, 1, t2, a2, m2);
    CHECK(!validate_system(&s, err, sizeof err), "Allocation > Max rejected");
    int t3[1] = {3}, a3[2] = {2,2}, m3[2] = {5,5};
    setup(&s, 2, 1, t3, a3, m3);
    CHECK(!validate_system(&s, err, sizeof err), "Allocation > Total rejected");

    /* three-process cycle and recovery */
    int dead[MAX_P];
    cycle(&s);
    CHECK(detect_deadlock(&s, dead) == 3 && get_state(&s) == STATE_DEADLOCKED, "3-process cycle detected");
    double u[MAX_R];
    resource_utilization(&s, u);
    CHECK(u[0] == 100.0 && u[1] == 100.0 && u[2] == 100.0, "utilization is 100% when nothing is available");
    recover_terminate(&s, 0);
    CHECK(detect_deadlock(&s, NULL) == 0 && s.terminated[0] && s.available[0] == 1, "terminate breaks the cycle and frees R0");

    cycle(&s);
    recover_preempt(&s, 0);
    int back = 1;
    for (int j = 0; j < 3; j++) back &= (s.alloc[0][j] == 0 && s.need[0][j] == s.max[0][j]);
    CHECK(back && !s.terminated[0] && s.available[0] == 1 && detect_deadlock(&s, NULL) == 0, "preempt returns resources, process restarts with Need = Max");

    /* a process outside the cycle is not blamed */
    int t4[3] = {1,1,1}, a4[9] = {1,0,0, 0,1,0, 0,0,1}, m4[9] = {1,1,1, 1,1,1, 1,1,1};
    int w0[3] = {0,1,0}, w1[3] = {1,0,0};
    setup(&s, 3, 3, t4, a4, m4);
    set_pending_request(&s, 0, w0); set_pending_request(&s, 1, w1);
    int c = detect_deadlock(&s, dead);
    CHECK(c == 2 && dead[0] == 0 && dead[1] == 1, "only P0 and P1 reported, bystander P2 is not");
    CHECK(choose_victim(&s, dead, c) == 0, "victim is the deadlocked process holding the least");

    printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL EDGE TESTS PASSED", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
