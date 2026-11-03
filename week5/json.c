/*
 * json.c  
 * Converts simulation results into JSON text so the HTML page can read them.
 * Empty frames are sent as -1 and "nothing evicted" is also -1.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "pageswap.h"

/* Appends formatted text to buf, never writing past cap. */
static void append(char *buf, size_t cap, size_t *len, const char *fmt, ...)
{
    if (*len >= cap - 1) return;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + *len, cap - *len, fmt, ap);
    va_end(ap);
    if (w < 0) return;
    *len += (size_t)w;
    if (*len > cap - 1) *len = cap - 1;
}

/* Writes a quoted, escaped JSON string. */
static void append_string(char *buf, size_t cap, size_t *len, const char *s)
{
    append(buf, cap, len, "\"");
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') append(buf, cap, len, "\\%c", *s);
        else if ((unsigned char)*s < 0x20) append(buf, cap, len, " ");
        else append(buf, cap, len, "%c", *s);
    }
    append(buf, cap, len, "\"");
}

static void append_result(char *buf, size_t cap, size_t *len, const Result *r)
{
    append(buf, cap, len, "{\"name\":");
    append_string(buf, cap, len, r->name);
    append(buf, cap, len,
           ",\"frames\":%d,\"total\":%d,\"hits\":%d,\"faults\":%d,"
           "\"hitRatio\":%.1f,\"faultRatio\":%.1f,\"steps\":[",
           r->n_frames, r->n_steps, r->hits, r->faults,
           r->hit_ratio, r->fault_ratio);

    for (int i = 0; i < r->n_steps; i++) {
        const Step *s = &r->steps[i];
        append(buf, cap, len, "%s{\"page\":%d,\"frames\":[",
               i ? "," : "", s->page);
        for (int f = 0; f < r->n_frames; f++)
            append(buf, cap, len, "%s%d", f ? "," : "", s->frames[f]);
        append(buf, cap, len, "],\"hit\":%s,\"replaced\":%d}",
               s->hit ? "true" : "false", s->replaced);
    }
    append(buf, cap, len, "]}");
}

size_t json_build_success(const Result *results, int count, const char *best,
                          char *buf, size_t cap)
{
    size_t len = 0;
    buf[0] = '\0';
    append(buf, cap, &len, "{\"ok\":true,\"best\":");
    append_string(buf, cap, &len, best);
    append(buf, cap, &len, ",\"results\":[");
    for (int i = 0; i < count; i++) {
        if (i) append(buf, cap, &len, ",");
        append_result(buf, cap, &len, &results[i]);
    }
    append(buf, cap, &len, "]}");
    return len;
}

size_t json_build_error(const char *message, char *buf, size_t cap)
{
    size_t len = 0;
    buf[0] = '\0';
    append(buf, cap, &len, "{\"ok\":false,\"error\":");
    append_string(buf, cap, &len, message);
    append(buf, cap, &len, "}");
    return len;
}
