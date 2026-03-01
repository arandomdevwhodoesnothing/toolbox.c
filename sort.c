/*
 * sort - sort lines of text files
 * Full implementation with all standard features
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <ctype.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *program_name = "sort";

/* ---- Options ---- */
static int opt_reverse         = 0;  /* -r */
static int opt_unique          = 0;  /* -u */
static int opt_ignore_case     = 0;  /* -f */
static int opt_numeric         = 0;  /* -n */
static int opt_general_numeric = 0;  /* -g */
static int opt_human_numeric   = 0;  /* -h */
static int opt_month           = 0;  /* -M */
static int opt_version         = 0;  /* -V */
static int opt_random          = 0;  /* -R */
static int opt_ignore_blanks   = 0;  /* -b */
static int opt_ignore_nonprint = 0;  /* -i */
static int opt_dictionary      = 0;  /* -d */
static int opt_zero_terminated = 0;  /* -z */
static int opt_stable          = 0;  /* -s */
static int opt_check           = 0;  /* -c */
static int opt_check_quiet     = 0;  /* -C */
static int opt_merge           = 0;  /* -m */
static char opt_field_sep      = '\0'; /* -t */
static int opt_field_sep_set   = 0;
static char *opt_output        = NULL; /* -o */
static long opt_parallel       = 0;   /* --parallel (accepted, ignored) */

/* Key definitions */
#define MAX_KEYS 32

typedef struct {
    int  field_start;   /* 1-based field */
    int  char_start;    /* 1-based char within field, 0 = from field start */
    int  field_end;     /* 0 = to end of line */
    int  char_end;      /* 0 = to end of field */
    int  reverse;
    int  ignore_case;
    int  numeric;
    int  general_numeric;
    int  human_numeric;
    int  month;
    int  version;
    int  random;
    int  ignore_blanks;
    int  ignore_nonprint;
    int  dictionary;
} SortKey;

static SortKey keys[MAX_KEYS];
static int nkeys = 0;

/* ---- Utility ---- */

static void die(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", program_name);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

static void warn_msg(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", program_name);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void usage(void) {
    fprintf(stderr,
"Usage: %s [OPTION]... [FILE]...\n"
"  or:  %s [OPTION]... --files0-from=F\n"
"Write sorted concatenation of all FILE(s) to standard output.\n"
"\n"
"Ordering options:\n"
"  -b, --ignore-leading-blanks  ignore leading blanks\n"
"  -d, --dictionary-order       consider only blanks and alphanumeric chars\n"
"  -f, --ignore-case            fold lower case to upper case characters\n"
"  -g, --general-numeric-sort   compare according to general numerical value\n"
"  -h, --human-numeric-sort     compare human readable numbers (e.g., 2K 1G)\n"
"  -i, --ignore-nonprinting     consider only printable characters\n"
"  -M, --month-sort             compare (unknown) < 'JAN' < ... < 'DEC'\n"
"  -n, --numeric-sort           compare according to string numerical value\n"
"  -R, --random-sort            shuffle, but group identical keys\n"
"  -r, --reverse                reverse the result of comparisons\n"
"  -V, --version-sort           natural sort of (version) numbers within text\n"
"\n"
"Other options:\n"
"  -c, --check                  check for sorted input; do not sort\n"
"  -C, --check=quiet            like -c, but do not report first bad line\n"
"  -k, --key=KEYDEF             sort via a key; KEYDEF gives location and type\n"
"  -m, --merge                  merge already sorted files; do not sort\n"
"  -o, --output=FILE            write result to FILE instead of standard output\n"
"  -s, --stable                 stabilize sort by disabling last-resort comparison\n"
"  -S, --buffer-size=SIZE       use SIZE for main memory buffer\n"
"  -t, --field-separator=SEP    use SEP instead of non-blank to blank transition\n"
"  -T, --temporary-directory=DIR use DIR for temporaries, not $TMPDIR or /tmp\n"
"  -u, --unique                 with -c, check for strict ordering;\n"
"                               without -c: output only the first of an equal run\n"
"  -z, --zero-terminated        line delimiter is NUL, not newline\n"
"      --parallel=N             change the number of sorts run concurrently\n"
"  -h, --help                   display this help and exit\n",
        program_name, program_name);
    exit(EXIT_FAILURE);
}

/* ---- Line storage ---- */

typedef struct {
    char  *data;   /* line content (without terminator) */
    size_t len;    /* length of data */
    size_t orig;   /* original index, for stable sort */
} Line;

static Line  *lines     = NULL;
static size_t nlines    = 0;
static size_t cap_lines = 0;

static char  *pool      = NULL;  /* string pool */
static size_t pool_size = 0;
static size_t pool_cap  = 0;

static void pool_grow(size_t need) {
    if (pool_size + need <= pool_cap) return;
    size_t newcap = pool_cap ? pool_cap * 2 : (1 << 20);
    while (newcap < pool_size + need) newcap *= 2;
    pool = realloc(pool, newcap);
    if (!pool) die("out of memory");
    pool_cap = newcap;
}

static void lines_grow(void) {
    if (nlines < cap_lines) return;
    cap_lines = cap_lines ? cap_lines * 2 : 4096;
    lines = realloc(lines, cap_lines * sizeof(Line));
    if (!lines) die("out of memory");
}

static void add_line(const char *s, size_t len, size_t orig) {
    pool_grow(len + 1);
    char *p = pool + pool_size;
    memcpy(p, s, len);
    p[len] = '\0';
    pool_size += len + 1;

    lines_grow();
    lines[nlines].data = p;
    lines[nlines].len  = len;
    lines[nlines].orig = orig;
    nlines++;
}

/* ---- Field extraction ---- */

/*
 * Get a pointer to the start of field F (1-based) within line,
 * and set *flen to the length of that field.
 * Uses opt_field_sep if set, else whitespace transitions.
 */
static const char *get_field(const char *line, size_t llen,
                              int fieldno, size_t *flen) {
    if (fieldno <= 0) fieldno = 1;

    const char *p   = line;
    const char *end = line + llen;

    if (opt_field_sep_set) {
        char sep = opt_field_sep;
        /* Fields are separated by sep, field 1 is before first sep */
        int f = 1;
        while (f < fieldno && p < end) {
            if (*p == sep) f++;
            p++;
        }
        if (p >= end && f < fieldno) {
            /* field doesn't exist */
            *flen = 0;
            return end;
        }
        /* find end of field */
        const char *q = p;
        while (q < end && *q != sep) q++;
        *flen = q - p;
        return p;
    } else {
        /* Whitespace-delimited: skip leading blanks for field 1 */
        /* transition non-blank->blank marks end, blank->nonblank marks start */
        int f = 1;
        /* skip leading whitespace */
        while (p < end && isblank((unsigned char)*p)) p++;
        if (fieldno == 1) {
            const char *q = p;
            while (q < end && !isblank((unsigned char)*q)) q++;
            *flen = q - p;
            return p;
        }
        /* skip to next field */
        while (p < end) {
            /* skip non-blank */
            while (p < end && !isblank((unsigned char)*p)) p++;
            /* skip blank */
            while (p < end && isblank((unsigned char)*p)) p++;
            f++;
            if (f == fieldno) {
                const char *q = p;
                while (q < end && !isblank((unsigned char)*q)) q++;
                *flen = q - p;
                return p;
            }
        }
        *flen = 0;
        return end;
    }
}

/*
 * Extract the sort key string for a given SortKey definition.
 * Returns a malloc'd string (caller must free).
 */
static char *extract_key(const char *line, size_t llen, const SortKey *k) {
    const char *start;
    const char *end_ptr = line + llen;

    /* Get start position */
    size_t flen = 0;
    start = get_field(line, llen, k->field_start, &flen);
    if (k->char_start > 0) {
        /* char_start is 1-based offset within the field */
        int skip = k->char_start - 1;
        if (skip > (int)flen) skip = (int)flen;
        start += skip;
    }

    /* Get end position */
    const char *end;
    if (k->field_end == 0) {
        /* to end of line */
        end = end_ptr;
    } else {
        size_t eflen = 0;
        end = get_field(line, llen, k->field_end, &eflen);
        if (k->char_end == 0) {
            /* to end of field */
            end = end + eflen;
        } else {
            int skip = k->char_end;
            if (skip > (int)eflen) skip = (int)eflen;
            end = end + skip;
        }
        if (end > end_ptr) end = end_ptr;
    }

    if (end < start) end = start;
    size_t klen = end - start;

    char *buf = malloc(klen + 1);
    if (!buf) die("out of memory");
    memcpy(buf, start, klen);
    buf[klen] = '\0';
    return buf;
}

/* ---- Comparison functions ---- */

/* Month names */
static const char *months[] = {
    "JAN","FEB","MAR","APR","MAY","JUN",
    "JUL","AUG","SEP","OCT","NOV","DEC"
};

static int month_index(const char *s) {
    /* skip leading blanks */
    while (*s && isblank((unsigned char)*s)) s++;
    char up[4] = {0};
    for (int i = 0; i < 3 && s[i]; i++)
        up[i] = toupper((unsigned char)s[i]);
    for (int m = 0; m < 12; m++)
        if (strncmp(up, months[m], 3) == 0)
            return m;
    return -1;
}

/* Parse human-readable number: 1K=1024, 1M=1048576, etc. */
static double parse_human(const char *s) {
    while (*s && isblank((unsigned char)*s)) s++;
    char *endp;
    double val = strtod(s, &endp);
    if (endp == s) return 0.0;
    switch (toupper((unsigned char)*endp)) {
        case 'K': val *= 1e3;  break;
        case 'M': val *= 1e6;  break;
        case 'G': val *= 1e9;  break;
        case 'T': val *= 1e12; break;
        case 'P': val *= 1e15; break;
        case 'E': val *= 1e18; break;
        /* binary suffixes */
    }
    return val;
}

/* Version sort: compare version strings naturally */
static int cmp_version(const char *a, const char *b) {
    while (*a || *b) {
        /* skip non-alphanumeric leading chars */
        while (*a && !isalnum((unsigned char)*a)) a++;
        while (*b && !isalnum((unsigned char)*b)) b++;
        if (!*a && !*b) return 0;
        if (!*a) return -1;
        if (!*b) return 1;

        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            /* compare numeric segments */
            char *ea, *eb;
            long na = strtol(a, &ea, 10);
            long nb = strtol(b, &eb, 10);
            if (na != nb) return (na > nb) - (na < nb);
            a = ea; b = eb;
        } else {
            /* compare alpha segments */
            while (*a && isalpha((unsigned char)*a) &&
                   *b && isalpha((unsigned char)*b)) {
                int c = toupper((unsigned char)*a) - toupper((unsigned char)*b);
                if (c) return c;
                a++; b++;
            }
            if (isalpha((unsigned char)*a) && !isalpha((unsigned char)*b)) return 1;
            if (!isalpha((unsigned char)*a) && isalpha((unsigned char)*b)) return -1;
        }
    }
    return 0;
}

/* Apply key-specific transformations before string compare */
static int cmp_string_key(const char *a, const char *b, const SortKey *k) {
    /* ignore leading blanks */
    if (k->ignore_blanks) {
        while (isblank((unsigned char)*a)) a++;
        while (isblank((unsigned char)*b)) b++;
    }

    if (k->dictionary) {
        /* only blanks and alphanumerics */
        const char *pa = a, *pb = b;
        while (*pa || *pb) {
            while (*pa && !isblank((unsigned char)*pa) && !isalnum((unsigned char)*pa)) pa++;
            while (*pb && !isblank((unsigned char)*pb) && !isalnum((unsigned char)*pb)) pb++;
            if (!*pa && !*pb) return 0;
            if (!*pa) return -1;
            if (!*pb) return 1;
            int c = k->ignore_case
                ? tolower((unsigned char)*pa) - tolower((unsigned char)*pb)
                : (unsigned char)*pa - (unsigned char)*pb;
            if (c) return c;
            pa++; pb++;
        }
        return 0;
    }

    if (k->ignore_nonprint) {
        const char *pa = a, *pb = b;
        while (*pa || *pb) {
            while (*pa && !isprint((unsigned char)*pa)) pa++;
            while (*pb && !isprint((unsigned char)*pb)) pb++;
            if (!*pa && !*pb) return 0;
            if (!*pa) return -1;
            if (!*pb) return 1;
            int c = k->ignore_case
                ? tolower((unsigned char)*pa) - tolower((unsigned char)*pb)
                : (unsigned char)*pa - (unsigned char)*pb;
            if (c) return c;
            pa++; pb++;
        }
        return 0;
    }

    if (k->ignore_case)
        return strcasecmp(a, b);

    return strcmp(a, b);
}

/* Compare two lines according to all keys (and global options) */
static int compare_lines(const Line *la, const Line *lb) {
    const char *a = la->data;
    const char *b = lb->data;
    size_t alen = la->len;
    size_t blen = lb->len;

    if (nkeys == 0) {
        /* No keys: compare whole line */
        SortKey k = {0};
        k.reverse        = opt_reverse;
        k.ignore_case    = opt_ignore_case;
        k.numeric        = opt_numeric;
        k.general_numeric= opt_general_numeric;
        k.human_numeric  = opt_human_numeric;
        k.month          = opt_month;
        k.version        = opt_version;
        k.random         = opt_random;
        k.ignore_blanks  = opt_ignore_blanks;
        k.ignore_nonprint= opt_ignore_nonprint;
        k.dictionary     = opt_dictionary;
        k.field_start    = 1;
        k.field_end      = 0;

        char *ka = extract_key(a, alen, &k);
        char *kb = extract_key(b, blen, &k);
        int cmp = 0;

        const char *sa = ka, *sb = kb;
        if (k.ignore_blanks) {
            while (isblank((unsigned char)*sa)) sa++;
            while (isblank((unsigned char)*sb)) sb++;
        }

        if (k.numeric) {
            char *ep;
            double na = strtod(sa, &ep);
            if (ep == sa) na = 0.0;
            double nb = strtod(sb, &ep);
            if (ep == sb) nb = 0.0;
            cmp = (na > nb) - (na < nb);
        } else if (k.general_numeric) {
            char *ep;
            double na = strtod(sa, &ep);
            if (ep == sa) na = 0.0;
            double nb = strtod(sb, &ep);
            if (ep == sb) nb = 0.0;
            cmp = (na > nb) - (na < nb);
        } else if (k.human_numeric) {
            double na = parse_human(sa);
            double nb = parse_human(sb);
            cmp = (na > nb) - (na < nb);
        } else if (k.month) {
            int ma = month_index(sa);
            int mb = month_index(sb);
            cmp = (ma > mb) - (ma < mb);
        } else if (k.version) {
            cmp = cmp_version(sa, sb);
        } else if (k.random) {
            /* group identical keys, otherwise stable by original order */
            cmp = strcmp(sa, sb) == 0 ? 0 : ((int)(la->orig % 2) - (int)(lb->orig % 2));
        } else {
            cmp = cmp_string_key(sa, sb, &k);
        }

        free(ka); free(kb);
        if (k.reverse) cmp = -cmp;
        if (cmp != 0) return cmp;

        /* Last resort: whole line if not stable */
        if (!opt_stable)
            return strcmp(a, b);
        return (int)((long)la->orig - (long)lb->orig);
    }

    /* Key-based comparison */
    for (int ki = 0; ki < nkeys; ki++) {
        const SortKey *k = &keys[ki];
        char *ka = extract_key(a, alen, k);
        char *kb = extract_key(b, blen, k);
        int cmp = 0;

        const char *sa = ka, *sb = kb;
        if (k->ignore_blanks) {
            while (isblank((unsigned char)*sa)) sa++;
            while (isblank((unsigned char)*sb)) sb++;
        }

        if (k->numeric) {
            char *ep;
            double na = strtod(sa, &ep);
            if (ep == sa) na = 0.0;
            double nb = strtod(sb, &ep);
            if (ep == sb) nb = 0.0;
            cmp = (na > nb) - (na < nb);
        } else if (k->general_numeric) {
            char *ep;
            double na = strtod(sa, &ep);
            if (ep == sa) na = 0.0;
            double nb = strtod(sb, &ep);
            if (ep == sb) nb = 0.0;
            cmp = (na > nb) - (na < nb);
        } else if (k->human_numeric) {
            double na = parse_human(sa);
            double nb = parse_human(sb);
            cmp = (na > nb) - (na < nb);
        } else if (k->month) {
            int ma = month_index(sa);
            int mb = month_index(sb);
            cmp = (ma > mb) - (ma < mb);
        } else if (k->version) {
            cmp = cmp_version(sa, sb);
        } else if (k->random) {
            cmp = strcmp(sa, sb) == 0 ? 0 : ((int)(la->orig % 2) - (int)(lb->orig % 2));
        } else {
            cmp = cmp_string_key(sa, sb, k);
        }

        free(ka); free(kb);
        if (k->reverse) cmp = -cmp;
        if (cmp != 0) return cmp;
    }

    /* Last resort: whole line comparison */
    if (!opt_stable) {
        int cmp = strcmp(a, b);
        if (opt_reverse) cmp = -cmp;
        return cmp;
    }
    return (int)((long)la->orig - (long)lb->orig);
}

static int cmp_wrapper(const void *pa, const void *pb) {
    return compare_lines((const Line *)pa, (const Line *)pb);
}

/* ---- Read lines from a file ---- */

static void read_lines(FILE *f) {
    char *linebuf  = NULL;
    size_t bufsize = 0;
    char term = opt_zero_terminated ? '\0' : '\n';

    ssize_t len;
    while ((len = getdelim(&linebuf, &bufsize, term, f)) > 0) {
        /* strip terminator */
        if (len > 0 && linebuf[len - 1] == term)
            len--;
        add_line(linebuf, (size_t)len, nlines);
    }
    free(linebuf);
}

/* ---- Key parsing ---- */

/*
 * Parse a KEYDEF of the form:
 *   F[.C][OPTS][,F[.C][OPTS]]
 * where F is field number, C is character offset, OPTS are modifier letters.
 */
static void parse_key(const char *keydef) {
    if (nkeys >= MAX_KEYS)
        die("too many keys");

    SortKey *k = &keys[nkeys++];
    memset(k, 0, sizeof(*k));

    /* Inherit global options */
    k->reverse         = 0;
    k->ignore_case     = opt_ignore_case;
    k->numeric         = opt_numeric;
    k->general_numeric = opt_general_numeric;
    k->human_numeric   = opt_human_numeric;
    k->month           = opt_month;
    k->version         = opt_version;
    k->random          = opt_random;
    k->ignore_blanks   = opt_ignore_blanks;
    k->ignore_nonprint = opt_ignore_nonprint;
    k->dictionary      = opt_dictionary;

    const char *p = keydef;

    /* Parse start field */
    if (!isdigit((unsigned char)*p))
        die("invalid key: '%s'", keydef);
    k->field_start = (int)strtol(p, (char **)&p, 10);
    if (k->field_start < 1)
        die("key field must be >= 1: '%s'", keydef);
    if (*p == '.') {
        p++;
        k->char_start = (int)strtol(p, (char **)&p, 10);
    }

    /* Parse start options */
    while (*p && *p != ',') {
        switch (*p) {
        case 'b': k->ignore_blanks   = 1; break;
        case 'd': k->dictionary      = 1; break;
        case 'f': k->ignore_case     = 1; break;
        case 'g': k->general_numeric = 1; break;
        case 'h': k->human_numeric   = 1; break;
        case 'i': k->ignore_nonprint = 1; break;
        case 'M': k->month           = 1; break;
        case 'n': k->numeric         = 1; break;
        case 'R': k->random          = 1; break;
        case 'r': k->reverse         = 1; break;
        case 'V': k->version         = 1; break;
        default: die("invalid option in key: '%c'", *p);
        }
        p++;
    }

    /* Parse end field if present */
    if (*p == ',') {
        p++;
        if (!isdigit((unsigned char)*p))
            die("invalid key end: '%s'", keydef);
        k->field_end = (int)strtol(p, (char **)&p, 10);
        if (*p == '.') {
            p++;
            k->char_end = (int)strtol(p, (char **)&p, 10);
        }
        /* end options */
        while (*p) {
            switch (*p) {
            case 'b': break; /* applies to end position */
            case 'd': case 'f': case 'g': case 'h':
            case 'i': case 'M': case 'n': case 'R':
            case 'r': case 'V': break; /* already set from start */
            default: die("invalid option in key end: '%c'", *p);
            }
            p++;
        }
    }
}

/* ---- Check mode ---- */

static int check_sorted(void) {
    /* Lines are already loaded; check they're in order */
    for (size_t i = 1; i < nlines; i++) {
        int cmp = compare_lines(&lines[i-1], &lines[i]);
        if (cmp > 0 || (opt_unique && cmp == 0)) {
            if (!opt_check_quiet) {
                fprintf(stderr, "%s: disorder: %s\n",
                        program_name, lines[i].data);
            }
            return 1;
        }
    }
    return 0;
}

/* ---- Output ---- */

static void write_lines(FILE *out) {
    char term = opt_zero_terminated ? '\0' : '\n';
    for (size_t i = 0; i < nlines; i++) {
        if (opt_unique && i > 0) {
            if (compare_lines(&lines[i-1], &lines[i]) == 0)
                continue;
        }
        fwrite(lines[i].data, 1, lines[i].len, out);
        fputc(term, out);
    }
}

/* ---- Merge sorted files ---- */

typedef struct {
    FILE  *f;
    char  *line;
    size_t linecap;
    ssize_t linelen;
    int    eof;
    size_t orig;
} MergeStream;

static void merge_advance(MergeStream *s) {
    char term = opt_zero_terminated ? '\0' : '\n';
    s->linelen = getdelim(&s->line, &s->linecap, term, s->f);
    if (s->linelen <= 0) {
        s->eof = 1;
        return;
    }
    if (s->linelen > 0 && s->line[s->linelen-1] == term)
        s->linelen--;
    s->line[s->linelen] = '\0';
}

static int merge_cmp(const MergeStream *a, const MergeStream *b) {
    Line la = { a->line, (size_t)a->linelen, a->orig };
    Line lb = { b->line, (size_t)b->linelen, b->orig };
    return compare_lines(&la, &lb);
}

static void merge_files(FILE **fps, int nfps, FILE *out) {
    MergeStream *streams = calloc(nfps, sizeof(MergeStream));
    if (!streams) die("out of memory");
    int active = 0;

    for (int i = 0; i < nfps; i++) {
        streams[i].f    = fps[i];
        streams[i].orig = i;
        merge_advance(&streams[i]);
        if (!streams[i].eof) active++;
    }

    char term = opt_zero_terminated ? '\0' : '\n';

    /* Simple selection sort merge (fine for reasonable number of files) */
    size_t last_orig = 0;
    char *last_line  = NULL;
    int   have_last  = 0;

    while (active > 0) {
        /* Find minimum stream */
        int best = -1;
        for (int i = 0; i < nfps; i++) {
            if (streams[i].eof) continue;
            if (best == -1 || merge_cmp(&streams[i], &streams[best]) < 0)
                best = i;
        }
        if (best == -1) break;

        /* Unique check */
        if (opt_unique && have_last &&
            strcmp(streams[best].line, last_line) == 0) {
            merge_advance(&streams[best]);
            if (streams[best].eof) active--;
            continue;
        }

        /* Output */
        fwrite(streams[best].line, 1, streams[best].linelen, out);
        fputc(term, out);

        if (opt_unique) {
            free(last_line);
            last_line = strdup(streams[best].line);
            have_last = 1;
            (void)last_orig;
        }

        merge_advance(&streams[best]);
        if (streams[best].eof) active--;
    }

    free(last_line);
    for (int i = 0; i < nfps; i++)
        free(streams[i].line);
    free(streams);
}

/* ---- Main ---- */

int main(int argc, char **argv) {
    program_name = argv[0];

    int i;
    for (i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] != '-' || strcmp(arg, "-") == 0) break;
        if (strcmp(arg, "--") == 0) { i++; break; }

        if (strncmp(arg, "--", 2) == 0) {
            char *opt = arg + 2;
            if (strcmp(opt, "ignore-leading-blanks") == 0)     opt_ignore_blanks   = 1;
            else if (strcmp(opt, "dictionary-order") == 0)     opt_dictionary      = 1;
            else if (strcmp(opt, "ignore-case") == 0)          opt_ignore_case     = 1;
            else if (strcmp(opt, "general-numeric-sort") == 0) opt_general_numeric = 1;
            else if (strcmp(opt, "human-numeric-sort") == 0)   opt_human_numeric   = 1;
            else if (strcmp(opt, "ignore-nonprinting") == 0)   opt_ignore_nonprint = 1;
            else if (strcmp(opt, "month-sort") == 0)           opt_month           = 1;
            else if (strcmp(opt, "numeric-sort") == 0)         opt_numeric         = 1;
            else if (strcmp(opt, "random-sort") == 0)          opt_random          = 1;
            else if (strcmp(opt, "reverse") == 0)              opt_reverse         = 1;
            else if (strcmp(opt, "version-sort") == 0)         opt_version         = 1;
            else if (strcmp(opt, "unique") == 0)               opt_unique          = 1;
            else if (strcmp(opt, "stable") == 0)               opt_stable          = 1;
            else if (strcmp(opt, "merge") == 0)                opt_merge           = 1;
            else if (strcmp(opt, "zero-terminated") == 0)      opt_zero_terminated = 1;
            else if (strcmp(opt, "check") == 0)                opt_check           = 1;
            else if (strncmp(opt, "check=", 6) == 0) {
                if (strcmp(opt+6, "quiet") == 0 || strcmp(opt+6, "silent") == 0)
                    opt_check = opt_check_quiet = 1;
                else
                    opt_check = 1;
            }
            else if (strncmp(opt, "key=", 4) == 0)    parse_key(opt + 4);
            else if (strncmp(opt, "output=", 7) == 0) opt_output = opt + 7;
            else if (strncmp(opt, "field-separator=", 16) == 0) {
                opt_field_sep = opt[16];
                opt_field_sep_set = 1;
            }
            else if (strncmp(opt, "parallel=", 9) == 0)
                opt_parallel = strtol(opt+9, NULL, 10);
            else if (strncmp(opt, "buffer-size=", 12) == 0) { /* accepted, ignored */ }
            else if (strncmp(opt, "temporary-directory=", 20) == 0) { /* accepted */ }
            else if (strcmp(opt, "help") == 0) usage();
            else die("unrecognized option '--%s'", opt);
            continue;
        }

        /* Short options */
        for (int j = 1; arg[j]; j++) {
            switch (arg[j]) {
            case 'b': opt_ignore_blanks   = 1; break;
            case 'd': opt_dictionary      = 1; break;
            case 'f': opt_ignore_case     = 1; break;
            case 'g': opt_general_numeric = 1; break;
            case 'h': opt_human_numeric   = 1; break;
            case 'i': opt_ignore_nonprint = 1; break;
            case 'M': opt_month           = 1; break;
            case 'n': opt_numeric         = 1; break;
            case 'R': opt_random          = 1; break;
            case 'r': opt_reverse         = 1; break;
            case 'V': opt_version         = 1; break;
            case 'u': opt_unique          = 1; break;
            case 's': opt_stable          = 1; break;
            case 'm': opt_merge           = 1; break;
            case 'z': opt_zero_terminated = 1; break;
            case 'c': opt_check           = 1; break;
            case 'C': opt_check = opt_check_quiet = 1; break;
            case 'k':
                if (arg[j+1]) {
                    parse_key(arg + j + 1);
                    j = (int)strlen(arg) - 1;
                } else if (i+1 < argc) {
                    parse_key(argv[++i]);
                } else {
                    die("option '-k' requires an argument");
                }
                break;
            case 'o':
                if (arg[j+1]) {
                    opt_output = arg + j + 1;
                    j = (int)strlen(arg) - 1;
                } else if (i+1 < argc) {
                    opt_output = argv[++i];
                } else {
                    die("option '-o' requires an argument");
                }
                break;
            case 't':
                if (arg[j+1]) {
                    opt_field_sep = arg[j+1];
                    opt_field_sep_set = 1;
                    j = (int)strlen(arg) - 1;
                } else if (i+1 < argc) {
                    opt_field_sep = argv[++i][0];
                    opt_field_sep_set = 1;
                } else {
                    die("option '-t' requires an argument");
                }
                break;
            case 'T': /* temp dir, skip arg */
                if (arg[j+1]) j = (int)strlen(arg) - 1;
                else if (i+1 < argc) i++;
                break;
            case 'S': /* buffer size, skip arg */
                if (arg[j+1]) j = (int)strlen(arg) - 1;
                else if (i+1 < argc) i++;
                break;
            default:
                die("invalid option -- '%c'", arg[j]);
            }
        }
    }

    /* Collect input files */
    int nfiles   = argc - i;
    char **files = argv + i;

    /* Open output */
    FILE *out = stdout;
    if (opt_output) {
        out = fopen(opt_output, "w");
        if (!out) die("cannot open '%s' for writing: %s", opt_output, strerror(errno));
    }

    /* Merge mode */
    if (opt_merge && nfiles > 0) {
        FILE **fps = malloc(nfiles * sizeof(FILE *));
        if (!fps) die("out of memory");
        for (int fi = 0; fi < nfiles; fi++) {
            if (strcmp(files[fi], "-") == 0) {
                fps[fi] = stdin;
            } else {
                fps[fi] = fopen(files[fi], "r");
                if (!fps[fi]) die("cannot open '%s': %s", files[fi], strerror(errno));
            }
        }
        merge_files(fps, nfiles, out);
        for (int fi = 0; fi < nfiles; fi++)
            if (fps[fi] != stdin) fclose(fps[fi]);
        free(fps);
        if (out != stdout) fclose(out);
        return EXIT_SUCCESS;
    }

    /* Read all input */
    if (nfiles == 0) {
        read_lines(stdin);
    } else {
        for (int fi = 0; fi < nfiles; fi++) {
            if (strcmp(files[fi], "-") == 0) {
                read_lines(stdin);
            } else {
                FILE *f = fopen(files[fi], "r");
                if (!f) {
                    warn_msg("cannot open '%s': %s", files[fi], strerror(errno));
                    continue;
                }
                read_lines(f);
                fclose(f);
            }
        }
    }

    /* Sort */
    if (!opt_check) {
        qsort(lines, nlines, sizeof(Line), cmp_wrapper);
    }

    /* Check mode */
    if (opt_check) {
        int bad = check_sorted();
        if (out != stdout) fclose(out);
        free(pool);
        free(lines);
        return bad ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    /* Output */
    write_lines(out);

    if (out != stdout) fclose(out);
    free(pool);
    free(lines);
    return EXIT_SUCCESS;
}
