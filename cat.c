/*
 * cat - concatenate files and print on the standard output
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
#include <sys/stat.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *program_name = "cat";

/* Options */
static int opt_number_nonblank = 0;  /* -b: number non-blank lines */
static int opt_show_ends       = 0;  /* -E: show $ at end of each line */
static int opt_number          = 0;  /* -n: number all lines */
static int opt_squeeze_blank   = 0;  /* -s: squeeze multiple blank lines */
static int opt_show_tabs       = 0;  /* -T: show tabs as ^I */
static int opt_show_nonprint   = 0;  /* -v: show non-printing chars */

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

static void warn(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", program_name);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/* ---- Usage ---- */

static void usage(void) {
    fprintf(stderr,
"Usage: %s [OPTION]... [FILE]...\n"
"Concatenate FILE(s) to standard output.\n"
"\n"
"With no FILE, or when FILE is -, read standard input.\n"
"\n"
"  -A, --show-all           equivalent to -vET\n"
"  -b, --number-nonblank    number nonempty output lines, overrides -n\n"
"  -e                       equivalent to -vE\n"
"  -E, --show-ends          display $ at end of each line\n"
"  -n, --number             number all output lines\n"
"  -s, --squeeze-blank      suppress repeated empty output lines\n"
"  -t                       equivalent to -vT\n"
"  -T, --show-tabs          display TAB characters as ^I\n"
"  -u                       (ignored, for POSIX compatibility)\n"
"  -v, --show-nonprinting   use ^ and M- notation, except for LFD and TAB\n"
"  -h, --help               display this help and exit\n",
        program_name);
    exit(EXIT_FAILURE);
}

/* ---- Output buffer for performance ---- */

#define OUTBUF_SIZE (1 << 16)  /* 64 KiB */
static char outbuf[OUTBUF_SIZE];
static int  outbuf_pos = 0;

static inline void flush_out(void) {
    if (outbuf_pos > 0) {
        fwrite(outbuf, 1, outbuf_pos, stdout);
        outbuf_pos = 0;
    }
}

static inline void put_char(unsigned char c) {
    outbuf[outbuf_pos++] = c;
    if (outbuf_pos == OUTBUF_SIZE)
        flush_out();
}


static void put_line_number(long n) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%6ld\t", n);
    for (int i = 0; i < len; i++)
        put_char((unsigned char)buf[i]);
}

/* ---- Core: simple pass-through (no transformations needed) ---- */

static int cat_simple(int fd) {
    char buf[OUTBUF_SIZE];
    ssize_t nr;
    while ((nr = read(fd, buf, sizeof(buf))) > 0) {
        const char *p = buf;
        ssize_t rem = nr;
        while (rem > 0) {
            ssize_t nw = write(STDOUT_FILENO, p, rem);
            if (nw < 0) {
                if (errno == EINTR) continue;
                warn("write error: %s", strerror(errno));
                return -1;
            }
            p   += nw;
            rem -= nw;
        }
    }
    if (nr < 0) return -1;
    return 0;
}

/* ---- Core: process with transformations ---- */


/*
 * Full-featured processing: handles all flags including line numbering
 * Uses a line-oriented approach for correctness.
 */
static int cat_full(int fd, const char *name, long *line_num) {
    static unsigned char inbuf[OUTBUF_SIZE];
    static int at_line_start = -1; /* -1 = uninitialized */
    static int prev_was_blank = 0;
    static int cur_line_blank = 1; /* current line so far is blank */

    if (at_line_start == -1) {
        at_line_start = 1;
        prev_was_blank = 0;
        cur_line_blank = 1;
    }

    ssize_t nr;
    while ((nr = read(fd, inbuf, sizeof(inbuf))) > 0) {
        const unsigned char *p = inbuf;
        const unsigned char *end = p + nr;

        while (p < end) {
            unsigned char c = *p++;

            if (c == '\n') {
                /* squeeze_blank: if both this and previous line are blank, skip */
                if (opt_squeeze_blank && cur_line_blank && prev_was_blank) {
                    /* skip this newline entirely */
                    at_line_start = 1;
                    cur_line_blank = 1;
                    continue;
                }

                prev_was_blank = cur_line_blank;
                cur_line_blank = 1;

                if (opt_show_ends)
                    put_char('$');
                put_char('\n');
                at_line_start = 1;
                continue;
            }

            /* Non-newline: emit line number if at start of line */
            if (at_line_start) {
                at_line_start = 0;
                /* Number this line? */
                if (opt_number && !opt_number_nonblank) {
                    put_line_number(++(*line_num));
                } else if (opt_number_nonblank) {
                    put_line_number(++(*line_num));
                }
            }

            cur_line_blank = 0;

            if (c == '\t' && opt_show_tabs) {
                put_char('^');
                put_char('I');
            } else if (opt_show_nonprint &&
                       c != '\t' &&
                       (c < 32 || c == 127 || c >= 128)) {
                unsigned char d = c;
                if (d >= 128) {
                    put_char('M');
                    put_char('-');
                    d -= 128;
                }
                if (d < 32) {
                    put_char('^');
                    put_char((unsigned char)(d + 64));
                } else if (d == 127) {
                    put_char('^');
                    put_char('?');
                } else {
                    put_char(d);
                }
            } else {
                put_char(c);
            }
        }
    }

    if (nr < 0) {
        warn("%s: %s", name, strerror(errno));
        return -1;
    }
    return 0;
}

/* ---- Process one file ---- */

static int do_cat(const char *path, long *line_num) {
    int fd;
    int is_stdin = 0;

    if (strcmp(path, "-") == 0) {
        fd = STDIN_FILENO;
        is_stdin = 1;
    } else {
        fd = open(path, O_RDONLY);
        if (fd < 0) {
            warn("%s: %s", path, strerror(errno));
            return -1;
        }
        /* Check it's not a directory */
        struct stat st;
        if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
            warn("%s: Is a directory", path);
            close(fd);
            return -1;
        }
    }

    int ret;
    int need_transform = opt_number || opt_number_nonblank ||
                         opt_show_ends || opt_squeeze_blank ||
                         opt_show_tabs || opt_show_nonprint;

    if (!need_transform) {
        ret = cat_simple(fd);
    } else {
        ret = cat_full(fd, path, line_num);
    }

    flush_out();
    if (ferror(stdout)) {
        warn("write error: %s", strerror(errno));
        ret = -1;
    }

    if (!is_stdin)
        close(fd);

    return ret;
}

/* ---- Main ---- */

int main(int argc, char **argv) {
    program_name = argv[0];

    int i;
    for (i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] != '-' || strcmp(arg, "-") == 0) break;
        if (strcmp(arg, "--") == 0) { i++; break; }

        /* Long options */
        if (strncmp(arg, "--", 2) == 0) {
            char *opt = arg + 2;
            if (strcmp(opt, "show-all") == 0) {
                opt_show_nonprint = opt_show_ends = opt_show_tabs = 1;
            } else if (strcmp(opt, "number-nonblank") == 0) {
                opt_number_nonblank = 1; opt_number = 0;
            } else if (strcmp(opt, "show-ends") == 0) {
                opt_show_ends = 1;
            } else if (strcmp(opt, "number") == 0) {
                if (!opt_number_nonblank) opt_number = 1;
            } else if (strcmp(opt, "squeeze-blank") == 0) {
                opt_squeeze_blank = 1;
            } else if (strcmp(opt, "show-tabs") == 0) {
                opt_show_tabs = 1;
            } else if (strcmp(opt, "show-nonprinting") == 0) {
                opt_show_nonprint = 1;
            } else if (strcmp(opt, "help") == 0) {
                usage();
            } else {
                die("unrecognized option '--%s'", opt);
            }
            continue;
        }

        /* Short options */
        for (int j = 1; arg[j]; j++) {
            switch (arg[j]) {
            case 'A': opt_show_nonprint = opt_show_ends = opt_show_tabs = 1; break;
            case 'b': opt_number_nonblank = 1; opt_number = 0; break;
            case 'e': opt_show_nonprint = opt_show_ends = 1; break;
            case 'E': opt_show_ends = 1; break;
            case 'n': if (!opt_number_nonblank) opt_number = 1; break;
            case 's': opt_squeeze_blank = 1; break;
            case 't': opt_show_nonprint = opt_show_tabs = 1; break;
            case 'T': opt_show_tabs = 1; break;
            case 'u': break; /* ignored, POSIX compat */
            case 'v': opt_show_nonprint = 1; break;
            case 'h': usage(); break;
            default:
                die("invalid option -- '%c'", arg[j]);
            }
        }
    }

    /* -b overrides -n */
    if (opt_number_nonblank) opt_number = 0;

    int ret = 0;
    long line_num = 0;

    if (i >= argc) {
        /* No files: read stdin */
        if (do_cat("-", &line_num) != 0)
            ret = 1;
    } else {
        for (; i < argc; i++) {
            if (do_cat(argv[i], &line_num) != 0)
                ret = 1;
        }
    }

    return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
