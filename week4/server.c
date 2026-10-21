/* OWNER: Member 2 - integration and backend */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "deadlock.h"

static System sys;
static int ready = 0;

/* ---------- tiny growable string buffer ---------- */
typedef struct { char *d; size_t n, cap; } Buf;
static void binit(Buf *b) { b->cap = 1024; b->n = 0; b->d = malloc(b->cap); b->d[0] = 0; }
static void bp(Buf *b, const char *fmt, ...) {
    for (;;) {
        va_list ap;
        va_start(ap, fmt);
        int k = vsnprintf(b->d + b->n, b->cap - b->n, fmt, ap);
        va_end(ap);
        if (k >= 0 && (size_t)k < b->cap - b->n) { b->n += k; return; }
        b->cap = b->cap * 2 + 256;
        b->d = realloc(b->d, b->cap);
    }
}
static void arr(Buf *b, const int *v, int k) {
    bp(b, "[");
    for (int i = 0; i < k; i++) bp(b, "%s%d", i ? "," : "", v[i]);
    bp(b, "]");
}
static void mat(Buf *b, int a[][MAX_R], int n, int m) {
    bp(b, "[");
    for (int i = 0; i < n; i++) { bp(b, i ? "," : ""); arr(b, a[i], m); }
    bp(b, "]");
}
static void err(Buf *b, const char *msg) { bp(b, "{\"error\":\"%s\"}", msg); }

/* ---------- JSON state for the browser ---------- */
static void state_json(Buf *b) {
    if (!ready) { bp(b, "{\"ready\":false}"); return; }
    int seq[MAX_P], len = 0, dead[MAX_P];
    double u[MAX_R];
    int safe = is_safe(&sys, seq, &len);
    int nd = detect_deadlock(&sys, dead);
    resource_utilization(&sys, u);
    bp(b, "{\"ready\":true,\"n\":%d,\"m\":%d,\"total\":", sys.n, sys.m);
    arr(b, sys.total, sys.m);
    bp(b, ",\"available\":"); arr(b, sys.available, sys.m);
    bp(b, ",\"alloc\":");   mat(b, sys.alloc, sys.n, sys.m);
    bp(b, ",\"max\":");     mat(b, sys.max, sys.n, sys.m);
    bp(b, ",\"need\":");    mat(b, sys.need, sys.n, sys.m);
    bp(b, ",\"request\":"); mat(b, sys.request, sys.n, sys.m);
    bp(b, ",\"terminated\":"); arr(b, sys.terminated, sys.n);
    bp(b, ",\"state\":\"%s\",\"safe\":%s,\"seq\":", state_str(get_state(&sys)), safe ? "true" : "false");
    arr(b, seq, safe ? len : 0);
    bp(b, ",\"dead\":"); arr(b, dead, nd);
    bp(b, ",\"util\":[");
    for (int j = 0; j < sys.m; j++) bp(b, "%s%.1f", j ? "," : "", u[j]);
    bp(b, "]}");
}

/* ---------- history: one snapshot per state change (for before/during/after view) ---------- */
#define MAX_H 40
static char *hist_state[MAX_H];
static char hist_label[MAX_H][96];
static int hist_n = 0;
static void hist_reset(void) {
    for (int i = 0; i < hist_n; i++) free(hist_state[i]);
    hist_n = 0;
}
static void snapshot(const char *label) {
    if (!ready) return;
    if (hist_n == MAX_H) {
        free(hist_state[0]);
        memmove(hist_state, hist_state + 1, (MAX_H - 1) * sizeof *hist_state);
        memmove(hist_label, hist_label + 1, (MAX_H - 1) * sizeof *hist_label);
        hist_n--;
    }
    Buf b; binit(&b); state_json(&b);
    hist_state[hist_n] = b.d;
    snprintf(hist_label[hist_n], sizeof hist_label[0], "%s", label);
    hist_n++;
}
static void history_json(Buf *b) {
    bp(b, "[");
    for (int i = 0; i < hist_n; i++)
        bp(b, "%s{\"label\":\"%s\",\"state\":%s}", i ? "," : "", hist_label[i], hist_state[i]);
    bp(b, "]");
}

/* ---------- API handlers (body = whitespace-separated integers) ---------- */
static int nums(const char *s, int *v, int max) {
    int c = 0; char *e;
    for (;;) {
        long x = strtol(s, &e, 10);
        if (e == s) break;
        if (c < max) v[c] = (int)x;
        c++; s = e;
    }
    return c > max ? max + 1 : c;
}

static void api_setup(const int *v, int c, Buf *b) {
    if (c < 2 || v[0] < 1 || v[0] > MAX_P || v[1] < 1 || v[1] > MAX_R ||
        c != 2 + v[1] + 2 * v[0] * v[1]) { err(b, "Bad setup data"); return; }
    for (int i = 2; i < c; i++) if (v[i] < 0) { err(b, "Negative values are not allowed"); return; }
    System t;
    sys_init(&t, v[0], v[1]);
    int k = 2;
    for (int j = 0; j < t.m; j++) t.total[j] = v[k++];
    for (int i = 0; i < t.n; i++) for (int j = 0; j < t.m; j++) t.alloc[i][j] = v[k++];
    for (int i = 0; i < t.n; i++) for (int j = 0; j < t.m; j++) t.max[i][j] = v[k++];
    compute_available(&t);
    compute_need(&t);
    char e[120];
    if (!validate_system(&t, e, sizeof e)) { err(b, e); return; }
    sys = t; ready = 1;
    hist_reset();
    snapshot("Initial state");
    state_json(b);
}

static void api_request(const int *v, int c, Buf *b, int force) {
    if (!ready || c != 1 + sys.m || v[0] < 0 || v[0] >= sys.n || sys.terminated[v[0]]) { err(b, "Invalid request"); return; }
    for (int j = 1; j < c; j++) if (v[j] < 0) { err(b, "Invalid request"); return; }
    char lb[96];
    if (force) {
        set_pending_request(&sys, v[0], v + 1);
        snprintf(lb, sizeof lb, "P%d blocked, waiting for resources", v[0]);
        snapshot(lb);
        state_json(b);
        return;
    }
    ReqResult r = request_resources(&sys, v[0], v + 1);
    snprintf(lb, sizeof lb, "P%d request: %s", v[0], req_result_str(r));
    snapshot(lb);
    bp(b, "{\"result\":\"%s\",\"code\":%d,\"state\":", req_result_str(r), r);
    state_json(b);
    bp(b, "}");
}

static void api_recover(const int *v, int c, Buf *b) {
    if (!ready || c != 2) { err(b, "Invalid recover request"); return; }
    int dead[MAX_P], nd = detect_deadlock(&sys, dead);
    if (nd == 0) { err(b, "No deadlock to recover from"); return; }
    int p = v[0] < 0 ? choose_victim(&sys, dead, nd) : v[0];
    if (p < 0 || p >= sys.n) { err(b, "Bad victim"); return; }
    if (v[1] == 0) recover_terminate(&sys, p); else recover_preempt(&sys, p);
    char lb[96];
    snprintf(lb, sizeof lb, "Recovery: P%d %s", p, v[1] == 0 ? "terminated" : "preempted");
    snapshot(lb);
    bp(b, "{\"victim\":%d,\"state\":", p);
    state_json(b);
    bp(b, "}");
}

/* ---------- HTTP ---------- */
static void send_all(int fd, const char *d, size_t n) {
    while (n) { ssize_t k = write(fd, d, n); if (k <= 0) return; d += k; n -= k; }
}
static void respond(int fd, int code, const char *type, const char *body, size_t n) {
    char h[200];
    int k = snprintf(h, sizeof h, "HTTP/1.1 %d X\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", code, type, n);
    send_all(fd, h, k);
    send_all(fd, body, n);
}
static void serve_file(int fd, const char *path) {
    char fn[300];
    if (strstr(path, "..")) { respond(fd, 404, "text/plain", "Not found", 9); return; }
    const char *dirs[] = { "member3_ui", "member4_graph" };   /* where the web files live */
    FILE *f = NULL;
    for (int k = 0; k < 2 && !f; k++) {
        snprintf(fn, sizeof fn, "%s%s%s", dirs[k], path, strcmp(path, "/") == 0 ? "index.html" : "");
        f = fopen(fn, "rb");
    }
    if (!f) { respond(fd, 404, "text/plain", "Not found", 9); return; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    char *d = malloc(sz + 1);
    size_t got = fread(d, 1, sz, f);
    fclose(f);
    const char *t = strstr(fn, ".css") ? "text/css" : strstr(fn, ".js") ? "application/javascript" : "text/html";
    respond(fd, 200, t, d, got);
    free(d);
}

static void handle(int fd) {
    char req[16384];
    int len = 0, hend = -1;
    while (len < (int)sizeof req - 1) {
        ssize_t k = read(fd, req + len, sizeof req - 1 - len);
        if (k <= 0) break;
        len += k; req[len] = 0;
        char *e = strstr(req, "\r\n\r\n");
        if (e) {
            hend = (int)(e - req) + 4;
            char *cl = strcasestr(req, "Content-Length:");
            if (len >= hend + (cl ? atoi(cl + 15) : 0)) break;
        }
    }
    if (hend < 0) return;
    char method[8], path[256];
    if (sscanf(req, "%7s %255s", method, path) != 2) return;
    char *q = strchr(path, '?'); if (q) *q = 0;
    const char *body = req + hend;

    if (strncmp(path, "/api/", 5) != 0) { serve_file(fd, path); return; }
    Buf b; binit(&b);
    int v[256], c = nums(body, v, 256);
    const char *p = path + 5;
    if      (!strcmp(p, "state"))   state_json(&b);
    else if (!strcmp(p, "setup"))   api_setup(v, c, &b);
    else if (!strcmp(p, "request")) api_request(v, c, &b, 0);
    else if (!strcmp(p, "pending")) api_request(v, c, &b, 1);
    else if (!strcmp(p, "recover")) api_recover(v, c, &b);
    else if (!strcmp(p, "history")) history_json(&b);
    else if (!strcmp(p, "clear"))   { memset(sys.request, 0, sizeof sys.request); snapshot("Pending requests cleared"); state_json(&b); }
    else err(&b, "Unknown endpoint");
    respond(fd, 200, "application/json", b.d, b.n);
    free(b.d);
}

int main(int argc, char **argv) {
    int port = argc > 1 ? atoi(argv[1]) : 8080;
    signal(SIGPIPE, SIG_IGN);
    int s = socket(AF_INET, SOCK_STREAM, 0), on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = INADDR_ANY };
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0 || listen(s, 16) < 0) { perror("bind/listen"); return 1; }
    printf("Deadlock simulator running: http://localhost:%d\n", port);
    for (;;) {
        int fd = accept(s, NULL, NULL);
        if (fd < 0) continue;
        handle(fd);
        close(fd);
    }
}
