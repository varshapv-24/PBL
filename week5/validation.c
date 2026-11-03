/*
 * validation.c  
 * Reads and validates the user's input (spec section 5, step 1 and
 * section 7: "handle invalid input and edge cases").
 */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include "pageswap.h"

/*
 * Turns text such as "7 0 1 2 0 3" (spaces or commas) into an int array.
 * Returns 1 on success, 0 on failure (a readable message goes into err).
 */
int parse_refs(const char *text, int *refs, int *n, char *err, size_t err_len)
{
    const char *p = text;
    *n = 0;

    if (text == NULL) {
        snprintf(err, err_len, "The page-reference string is missing.");
        return 0;
    }

    while (*p) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (*p == '\0') break;

        if (!isdigit((unsigned char)*p)) {
            snprintf(err, err_len,
                     "Invalid input near '%c'. Use only whole numbers 0-%d "
                     "separated by spaces.", *p, MAX_PAGE_NUM);
            return 0;
        }

        char *end;
        errno = 0;
        long v = strtol(p, &end, 10);

        if (*end != '\0' && !isspace((unsigned char)*end) && *end != ',') {
            snprintf(err, err_len,
                     "Invalid character '%c' inside a page number.", *end);
            return 0;
        }
        if (errno != 0 || v > MAX_PAGE_NUM) {
            snprintf(err, err_len,
                     "Page numbers must be between 0 and %d.", MAX_PAGE_NUM);
            return 0;
        }
        if (*n >= MAX_REFS) {
            snprintf(err, err_len,
                     "Too many references. The limit is %d.", MAX_REFS);
            return 0;
        }
        refs[(*n)++] = (int)v;
        p = end;
    }

    if (*n == 0) {
        snprintf(err, err_len, "The page-reference string is empty.");
        return 0;
    }
    return 1;
}

/* Frame count must be a positive number, at most MAX_FRAMES. */
int validate_frames(int frames, char *err, size_t err_len)
{
    if (frames < 1) {
        snprintf(err, err_len,
                 "The number of frames must be at least 1.");
        return 0;
    }
    if (frames > MAX_FRAMES) {
        snprintf(err, err_len,
                 "The number of frames cannot be more than %d.", MAX_FRAMES);
        return 0;
    }
    return 1;
}
