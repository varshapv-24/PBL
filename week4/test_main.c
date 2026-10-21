/* logic and testing */
#include <stdio.h>
#include <string.h>
#include "deadlock.h"

static int fails = 0;
#define CHECK(cond, name) do { int ok = (cond); \
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", name); if (!ok) fails++; } while (0)

static void print_seq(const int *seq, int n) {
    for (int i = 0; i < n; i++) printf("P%d%s", seq[i], i < n - 1 ? " -> " : "\n");
}

int main(void) {
    /* ---- classic textbook example: 5 processes, 3 resource types ---- */
    System s;
    int total[3]    = {10, 5, 7};
    int alloc[5][3] = {{0,1,0},{2,0,0},{3,0,2},{2,1,1},{0,0,2}};
    int max[5][3]   = {{7,5,3},{3,2,2},{9,0,2},{2,2,2},{4,3,3}};

    sys_init(&s, 5, 3);
    memcpy(s.total, total, sizeof total);
    for (int i = 0; i < 5; i++)
        for (int j = 0; j < 3; j++) { s.alloc[i][j] = alloc[i][j]; s.max[i][j] = max[i][j]; }
    compute_available(&s);
    compute_need(&s);

    char err[100];
    CHECK(validate_system(&s, err, sizeof err), "textbook input validates");
    CHECK(s.available[0] == 3 && s.available[1] == 3 && s.available[2] == 2, "Available = 3 3 2");
    CHECK(s.need[0][0] == 7 && s.need[0][1] == 4 && s.need[0][2] == 3, "Need of P0 = 7 4 3");

    int seq[MAX_P], len = 0;
    CHECK(is_safe(&s, seq, &len) && len == 5, "initial state is SAFE");
    printf("   safe sequence: "); print_seq(seq, len);
    int expect[5] = {1, 3, 4, 0, 2};
    CHECK(memcmp(seq, expect, sizeof expect) == 0, "sequence = P1,P3,P4,P0,P2");

    int r1[3] = {1,0,2};
    CHECK(request_resources(&s, 1, r1) == REQ_GRANTED, "P1 requests (1,0,2) -> granted");
    int r2[3] = {3,3,0};
    CHECK(request_resources(&s, 4, r2) == REQ_WAIT, "P4 requests (3,3,0) -> must wait");
    memset(s.request[4], 0, sizeof s.request[4]);
    int r3[3] = {0,2,0};
    CHECK(request_resources(&s, 0, r3) == REQ_UNSAFE, "P0 requests (0,2,0) -> unsafe, denied");
    CHECK(s.alloc[0][1] == 1, "unsafe request was rolled back");
    int r4[3] = {9,9,9};
    CHECK(request_resources(&s, 2, r4) == REQ_EXCEEDS_NEED, "request beyond max rejected");
    memset(s.request, 0, sizeof s.request);
    CHECK(get_state(&s) == STATE_SAFE, "state still SAFE");

    /* ---- deadlock: P0 holds R0 wants R1, P1 holds R1 wants R0 ---- */
    System d;
    sys_init(&d, 2, 2);
    d.total[0] = d.total[1] = 1;
    d.alloc[0][0] = 1; d.alloc[1][1] = 1;
    for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) d.max[i][j] = 1;
    compute_available(&d);
    compute_need(&d);
    int w0[2] = {0,1}, w1[2] = {1,0};
    set_pending_request(&d, 0, w0);
    set_pending_request(&d, 1, w1);

    int dead[MAX_P];
    int cnt = detect_deadlock(&d, dead);
    CHECK(cnt == 2 && get_state(&d) == STATE_DEADLOCKED, "circular wait detected as DEADLOCKED");

    int v = choose_victim(&d, dead, cnt);
    printf("   victim chosen: P%d\n", v);
    recover_terminate(&d, v);
    CHECK(detect_deadlock(&d, NULL) == 0, "deadlock resolved after terminating victim");
    CHECK(d.terminated[v] == 1 && d.available[0] + d.available[1] >= 1, "victim's resources released");

    /* ---- utilization ---- */
    double u[MAX_R];
    resource_utilization(&s, u);
    CHECK(u[0] > 0.0 && u[0] <= 100.0, "utilization within 0..100%");

    printf("\n%s (%d failure%s)\n", fails ? "SOME TESTS FAILED" : "ALL TESTS PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
