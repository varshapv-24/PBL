/*
 * server.c  
 * A tiny web server written in plain C (POSIX sockets, no libraries).
 *
 *   GET /          -> sends index.html (the GUI)
 *   GET /simulate  -> runs the algorithms and replies with JSON
 *                     e.g. /simulate?refs=7+0+1+2&frames=3&algo=all
 *
 * Start it from the project folder:  ./pagesim        (port 8080)
 *                                    ./pagesim 9000   (custom port)
 * Then open http://localhost:8080 in the browser.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "pageswap.h"

#define DEFAULT_PORT 8080
#define JSON_CAP     (512 * 1024)

/* ---------- small helpers ------------------------------------------------ */

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Decodes %XX and '+' from a URL query value. */
static void url_decode(char *dst, const char *src, size_t cap)
{
    size_t o = 0;
    for (; *src && o + 1 < cap; src++) {
        if (*src == '+') {
            dst[o++] = ' ';
        } else if (*src == '%' && hex_value(src[1]) >= 0 && hex_value(src[2]) >= 0) {
            dst[o++] = (char)(hex_value(src[1]) * 16 + hex_value(src[2]));
            src += 2;
        } else {
            dst[o++] = *src;
        }
    }
    dst[o] = '\0';
}

/* Finds key=value in a query string and copies the decoded value to out. */
static int query_get(const char *query, const char *key, char *out, size_t cap)
{
    size_t klen = strlen(key);
    const char *p = query;

    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            size_t vlen = strcspn(v, "&");
            char raw[4096];
            if (vlen >= sizeof(raw)) vlen = sizeof(raw) - 1;
            memcpy(raw, v, vlen);
            raw[vlen] = '\0';
            url_decode(out, raw, cap);
            return 1;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    out[0] = '\0';
    return 0;
}

static void send_all(int fd, const char *data, size_t len)
{
    while (len > 0) {
        ssize_t w = write(fd, data, len);
        if (w <= 0) return;
        data += w;
        len  -= (size_t)w;
    }
}

static void send_response(int fd, int status, const char *status_text,
                          const char *type, const char *body, size_t len)
{
    char head[256];
    int h = snprintf(head, sizeof(head),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n"
                     "Content-Length: %zu\r\nCache-Control: no-store\r\n"
                     "Connection: close\r\n\r\n",
                     status, status_text, type, len);
    send_all(fd, head, (size_t)h);
    send_all(fd, body, len);
}

static void send_json_error(int fd, const char *message)
{
    char buf[1024];
    size_t len = json_build_error(message, buf, sizeof(buf));
    send_response(fd, 400, "Bad Request", "application/json", buf, len);
}

/* ---------- route: GET / ------------------------------------------------- */

static void serve_index(int fd)
{
    FILE *fp = fopen("index.html", "rb");
    if (!fp) {
        const char *msg = "index.html not found. Run ./pagesim from the "
                          "folder that contains index.html.";
        send_response(fd, 404, "Not Found", "text/plain", msg, strlen(msg));
        return;
    }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *data = malloc((size_t)size + 1);
    if (!data) { fclose(fp); return; }
    size_t got = fread(data, 1, (size_t)size, fp);
    fclose(fp);

    send_response(fd, 200, "OK", "text/html; charset=utf-8", data, got);
    free(data);
}

/* ---------- route: GET /simulate ----------------------------------------- */

static void handle_simulate(int fd, const char *query)
{
    static Result results[3];
    int  refs[MAX_REFS], n = 0;
    char refs_text[4096], frames_text[64], algo[32], err[256];

    query_get(query, "refs",   refs_text,   sizeof(refs_text));
    query_get(query, "frames", frames_text, sizeof(frames_text));
    query_get(query, "algo",   algo,        sizeof(algo));

    /* Validate the reference string (Member 1's code). */
    if (!parse_refs(refs_text, refs, &n, err, sizeof(err))) {
        send_json_error(fd, err);
        return;
    }

    /* Validate the frame count: it must be a whole number. */
    char *end;
    long frames = strtol(frames_text, &end, 10);
    if (frames_text[0] == '\0' || *end != '\0') {
        send_json_error(fd, "The number of frames must be a whole number.");
        return;
    }
    if (!validate_frames((int)frames, err, sizeof(err))) {
        send_json_error(fd, err);
        return;
    }

    /* Run the chosen algorithm(s). */
    int count = 0;
    if (strcmp(algo, "fifo") == 0) {
        run_fifo(refs, n, (int)frames, &results[count++]);
    } else if (strcmp(algo, "lru") == 0) {
        run_lru(refs, n, (int)frames, &results[count++]);
    } else if (strcmp(algo, "optimal") == 0) {
        run_optimal(refs, n, (int)frames, &results[count++]);
    } else if (strcmp(algo, "all") == 0) {
        run_fifo(refs, n, (int)frames, &results[count++]);
        run_lru(refs, n, (int)frames, &results[count++]);
        run_optimal(refs, n, (int)frames, &results[count++]);
    } else {
        send_json_error(fd, "Choose FIFO, LRU, Optimal or Compare all.");
        return;
    }

    char best[64];
    find_best(results, count, best, sizeof(best));

    char *json = malloc(JSON_CAP);
    if (!json) { send_json_error(fd, "Server ran out of memory."); return; }
    size_t len = json_build_success(results, count, best, json, JSON_CAP);
    send_response(fd, 200, "OK", "application/json", json, len);
    free(json);
}

/* ---------- one client connection ---------------------------------------- */

static void handle_client(int fd)
{
    char req[8192];
    ssize_t got = read(fd, req, sizeof(req) - 1);
    if (got <= 0) return;
    req[got] = '\0';

    char method[8], target[4096];
    if (sscanf(req, "%7s %4095s", method, target) != 2 ||
        strcmp(method, "GET") != 0) {
        const char *msg = "Only GET requests are supported.";
        send_response(fd, 405, "Method Not Allowed", "text/plain",
                      msg, strlen(msg));
        return;
    }

    char *query = strchr(target, '?');
    if (query) *query++ = '\0';
    else query = target + strlen(target);

    if (strcmp(target, "/") == 0 || strcmp(target, "/index.html") == 0) {
        serve_index(fd);
    } else if (strcmp(target, "/simulate") == 0) {
        handle_simulate(fd, query);
    } else {
        const char *msg = "Page not found.";
        send_response(fd, 404, "Not Found", "text/plain", msg, strlen(msg));
    }
}

/* ---------- main --------------------------------------------------------- */

int main(int argc, char **argv)
{
    int port = (argc > 1) ? atoi(argv[1]) : DEFAULT_PORT;
    if (port < 1 || port > 65535) port = DEFAULT_PORT;

    signal(SIGPIPE, SIG_IGN);

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(srv, 16) < 0) { perror("listen"); return 1; }

    printf("Page Replacement Simulator running at http://localhost:%d\n", port);
    printf("Press Ctrl+C to stop.\n");

    for (;;) {
        int client = accept(srv, NULL, NULL);
        if (client < 0) continue;
        handle_client(client);
        close(client);
    }
}
