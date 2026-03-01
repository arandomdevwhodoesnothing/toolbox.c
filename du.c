/*
 * du - disk usage utility
 * A complete, advanced implementation compatible with GNU du
 *
 * Supports:
 *   -a, --all              write counts for all files, not just directories
 *   -b, --bytes            print apparent sizes in bytes
 *   -B, --block-size=SIZE  scale sizes by SIZE before printing
 *   -c, --total            produce a grand total
 *   -d, --max-depth=N      print the total for a directory only if it is N
 *                          or fewer levels below the command line argument
 *   -D, --dereference-args dereference only symlinks that are listed on
 *                          the command line
 *   -h, --human-readable   print sizes in human readable format (1K 234M 2G)
 *   -H                     equivalent to --dereference-args (-D)
 *   -k                     like --block-size=1K
 *   -l, --count-links      count sizes many times if hard linked
 *   -L, --dereference      dereference all symbolic links
 *   -m                     like --block-size=1M
 *   -P, --no-dereference   don't follow any symbolic links (default)
 *   -s, --summarize        display only a total for each argument
 *   -S, --separate-dirs    do not include size of subdirectories
 *   -t, --threshold=SIZE   exclude entries smaller than SIZE if positive,
 *                          or entries greater than SIZE if negative
 *   -X, --exclude-from=FILE  exclude files that match any pattern in FILE
 *       --exclude=PATTERN  exclude files that match PATTERN
 *   -x, --one-file-system  skip directories on different file systems
 *   -0, --null             end each output line with NUL, not newline
 *       --time             show time of the last modification
 *       --time=WORD        show time as WORD instead of modification time:
 *                          atime, access, use, ctime or status
 *       --time-style=STYLE show times using STYLE
 *       --inodes           list inode usage information instead of block usage
 *       --apparent-size    print apparent sizes rather than disk usage
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <fnmatch.h>
#include <time.h>
#include <limits.h>
#include <stdarg.h>

/* ── constants ─────────────────────────────────────────────── */
#define PROGRAM_NAME   "du"
#define DEFAULT_BLOCK  512        /* POSIX default */
#define MAX_INODES     (1 << 20)  /* hash table size */
#define MAX_EXCLUDES   256
#define PATH_MAX_LEN   4096

/* ── inode dedup hash table ─────────────────────────────────── */
typedef struct inode_entry {
    dev_t dev;
    ino_t ino;
    struct inode_entry *next;
} inode_entry_t;

static inode_entry_t *inode_table[MAX_INODES];

static bool inode_seen(dev_t dev, ino_t ino) {
    uint64_t h = ((uint64_t)dev * 2654435761ULL ^ (uint64_t)ino) % MAX_INODES;
    for (inode_entry_t *e = inode_table[h]; e; e = e->next)
        if (e->dev == dev && e->ino == ino)
            return true;
    inode_entry_t *n = malloc(sizeof *n);
    if (!n) { perror("malloc"); exit(1); }
    n->dev = dev; n->ino = ino;
    n->next = inode_table[h];
    inode_table[h] = n;
    return false;
}

static void inode_table_free(void) {
    for (int i = 0; i < MAX_INODES; i++) {
        inode_entry_t *e = inode_table[i];
        while (e) { inode_entry_t *nx = e->next; free(e); e = nx; }
        inode_table[i] = NULL;
    }
}

/* ── exclude patterns ───────────────────────────────────────── */
static char *excludes[MAX_EXCLUDES];
static int   nexcludes = 0;

static void add_exclude(const char *pat) {
    if (nexcludes < MAX_EXCLUDES)
        excludes[nexcludes++] = strdup(pat);
}

static void add_excludes_from_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return; }
    char line[PATH_MAX_LEN];
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        while (len && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len) add_exclude(line);
    }
    fclose(f);
}

static bool is_excluded(const char *name) {
    for (int i = 0; i < nexcludes; i++)
        if (fnmatch(excludes[i], name, FNM_PATHNAME | FNM_PERIOD) == 0)
            return true;
    return false;
}

/* ── options ────────────────────────────────────────────────── */
typedef enum { TIME_MTIME, TIME_ATIME, TIME_CTIME } time_field_t;
typedef enum { TS_DEFAULT, TS_ISO, TS_LONG_ISO, TS_FULL_ISO, TS_LOCALE } time_style_t;

static struct {
    bool all;            /* -a */
    bool bytes;          /* -b / --apparent-size */
    long long block_size; /* -B, -k, -m */
    bool total;          /* -c */
    int  max_depth;      /* -d (−1 = unlimited) */
    bool deref_args;     /* -D/-H */
    bool human;          /* -h */
    bool count_links;    /* -l */
    bool deref;          /* -L */
    bool no_deref;       /* -P */
    bool summarize;      /* -s */
    bool separate_dirs;  /* -S */
    long long threshold; /* -t */
    bool one_fs;         /* -x */
    bool null_term;      /* -0 */
    bool show_time;      /* --time */
    time_field_t time_field;
    time_style_t time_style;
    bool inodes;         /* --inodes */
    bool apparent_size;  /* --apparent-size */
    bool verbose_errors;
} opt = {
    .block_size = DEFAULT_BLOCK,
    .max_depth  = -1,
    .threshold  = LLONG_MIN,
};

/* ── output ─────────────────────────────────────────────────── */
static long long grand_total  = 0;
static long long grand_inodes = 0;
static dev_t     start_dev    = 0;

static void warn(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: ", PROGRAM_NAME);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static char *format_size_human(long long sz) {
    static char buf[32];
    const char *units[] = {"", "K", "M", "G", "T", "P", "E"};
    double v = (double)sz * opt.block_size; /* blocks → bytes */
    if (opt.bytes || opt.apparent_size) v = (double)sz;
    int u = 0;
    while (v >= 1024 && u < 6) { v /= 1024; u++; }
    if (u == 0) snprintf(buf, sizeof buf, "%.0f", v);
    else if (v < 10) snprintf(buf, sizeof buf, "%.1f%s", v, units[u]);
    else             snprintf(buf, sizeof buf, "%.0f%s",  v, units[u]);
    return buf;
}

static void format_time(char *buf, size_t sz, struct stat *st) {
    time_t t;
    switch (opt.time_field) {
        case TIME_ATIME: t = st->st_atime; break;
        case TIME_CTIME: t = st->st_ctime; break;
        default:         t = st->st_mtime; break;
    }
    struct tm *tm = localtime(&t);
    switch (opt.time_style) {
        case TS_ISO:
            strftime(buf, sz, "%Y-%m-%d", tm); break;
        case TS_LONG_ISO:
            strftime(buf, sz, "%Y-%m-%d %H:%M", tm); break;
        case TS_FULL_ISO:
            strftime(buf, sz, "%Y-%m-%d %H:%M:%S %z", tm); break;
        default:
            strftime(buf, sz, "%Y-%m-%d %H:%M", tm); break;
    }
}

static void print_entry(long long blocks, long long inodes_cnt,
                        struct stat *st, const char *path) {
    if (opt.threshold != LLONG_MIN) {
        long long val = opt.inodes ? inodes_cnt : blocks;
        if (opt.threshold >= 0 && val < opt.threshold) return;
        if (opt.threshold <  0 && val > -opt.threshold) return;
    }

    if (opt.inodes) {
        printf("%-7lld", inodes_cnt);
    } else if (opt.human) {
        printf("%-7s", format_size_human(blocks));
    } else {
        long long disp = blocks;
        if (opt.bytes || opt.apparent_size) {
            /* already in bytes if apparent_size flag used internally */
            disp = blocks;
        }
        /* scale to chosen block size */
        long long scaled;
        if (opt.bytes || opt.apparent_size) {
            scaled = disp;
        } else {
            /* blocks are in 512-byte units internally */
            scaled = (disp * 512 + opt.block_size - 1) / opt.block_size;
        }
        printf("%-7lld", scaled);
    }

    if (opt.show_time && st) {
        char tbuf[64];
        format_time(tbuf, sizeof tbuf, st);
        printf("\t%s", tbuf);
    }

    printf("\t%s%c", path, opt.null_term ? '\0' : '\n');
}

/* ── block count helper ─────────────────────────────────────── */
static long long file_blocks(struct stat *st) {
    if (opt.apparent_size || opt.bytes)
        return st->st_size; /* apparent size in bytes */
    return st->st_blocks;   /* 512-byte units */
}

/* ── recursive traversal ────────────────────────────────────── */
/* Returns total blocks (512B units or bytes when apparent_size). */
static long long traverse(const char *path, int depth,
                           bool cmdline_arg, long long *out_inodes) {
    struct stat st;
    int stat_ret;

    if (opt.deref || (opt.deref_args && cmdline_arg))
        stat_ret = stat(path, &st);
    else
        stat_ret = lstat(path, &st);

    if (stat_ret < 0) {
        warn("cannot access '%s': %s", path, strerror(errno));
        if (out_inodes) *out_inodes = 0;
        return 0;
    }

    /* one-file-system check */
    if (!cmdline_arg && opt.one_fs && st.st_dev != start_dev)
        return 0;

    /* hard-link dedup */
    bool is_dir = S_ISDIR(st.st_mode);
    bool counted = false;
    if (!is_dir && st.st_nlink > 1 && !opt.count_links) {
        if (inode_seen(st.st_dev, st.st_ino)) counted = true;
    }

    if (!is_dir) {
        long long blk = counted ? 0 : file_blocks(&st);
        if (out_inodes) *out_inodes = 1;
        if (opt.all) {
            bool print = (opt.max_depth < 0 || depth <= opt.max_depth);
            if (print) {
                struct stat *stp = opt.show_time ? &st : NULL;
                print_entry(blk, 1, stp, path);
            }
        }
        return blk;
    }

    /* ── directory ── */
    DIR *dir = opendir(path);
    if (!dir) {
        warn("cannot read directory '%s': %s", path, strerror(errno));
        if (out_inodes) *out_inodes = 1;
        return file_blocks(&st);
    }

    long long total_blk    = opt.separate_dirs ? 0 : file_blocks(&st);
    long long total_inodes = 1; /* count the directory itself */
    char child[PATH_MAX_LEN];
    struct dirent *de;

    while ((de = readdir(dir))) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        if (is_excluded(de->d_name))
            continue;

        int n = snprintf(child, sizeof child, "%s/%s", path, de->d_name);
        if (n < 0 || n >= (int)sizeof child) {
            warn("path too long: %s/%s", path, de->d_name);
            continue;
        }

        long long child_inodes = 0;
        long long child_blk = traverse(child, depth + 1, false, &child_inodes);
        total_blk    += child_blk;
        total_inodes += child_inodes;
    }
    closedir(dir);

    if (out_inodes) *out_inodes = total_inodes;

    bool print = (opt.max_depth < 0 || depth <= opt.max_depth);
    if (!opt.summarize || cmdline_arg) {
        if (print) {
            struct stat *stp = opt.show_time ? &st : NULL;
            print_entry(total_blk, total_inodes, stp, path);
        }
    }

    return total_blk;
}

/* ── parse block size string (like 1K, 2M, 1024) ───────────── */
static long long parse_size(const char *s) {
    char *end;
    long long v = strtoll(s, &end, 0);
    if (end == s) { warn("invalid size: %s", s); exit(1); }
    switch (*end) {
        case 'K': case 'k': v *= 1024LL; break;
        case 'M': case 'm': v *= 1024LL * 1024; break;
        case 'G': case 'g': v *= 1024LL * 1024 * 1024; break;
        case 'T': case 't': v *= 1024LL * 1024 * 1024 * 1024; break;
        case 'P': case 'p': v *= 1024LL * 1024 * 1024 * 1024 * 1024; break;
        case '\0': break;
        default: warn("invalid suffix in size: %s", s); exit(1);
    }
    return v;
}

/* ── usage ──────────────────────────────────────────────────── */
static void usage(int code) {
    printf(
"Usage: %s [OPTION]... [FILE]...\n"
"Summarize device usage of the set of FILEs, recursively for directories.\n"
"\n"
"  -a, --all             write counts for all files, not just directories\n"
"      --apparent-size   print apparent sizes rather than disk usage\n"
"  -b, --bytes           equivalent to '--apparent-size --block-size=1'\n"
"  -B, --block-size=SIZE  scale sizes by SIZE before printing\n"
"  -c, --total           produce a grand total\n"
"  -d, --max-depth=N     print total for dir only if within N levels deep\n"
"  -D, --dereference-args  dereference only symlinks on command line\n"
"  -h, --human-readable  print sizes in human readable format (1K 234M 2G)\n"
"  -H                    equivalent to --dereference-args (-D)\n"
"      --inodes          list inode usage information instead of block usage\n"
"  -k                    like --block-size=1K\n"
"  -l, --count-links     count sizes many times if hard linked\n"
"  -L, --dereference     dereference all symbolic links\n"
"  -m                    like --block-size=1M\n"
"  -P, --no-dereference  don't follow any symbolic links (default)\n"
"  -s, --summarize       display only a total for each argument\n"
"  -S, --separate-dirs   do not include size of subdirectories\n"
"  -t, --threshold=SIZE  exclude entries smaller than SIZE if positive,\n"
"                        or entries greater than SIZE if negative\n"
"      --time            show time of the last modification of any file\n"
"      --time=WORD       show time as WORD: atime, access, use, ctime, status\n"
"      --time-style=STYLE  show times using STYLE: full-iso, long-iso, iso\n"
"  -x, --one-file-system  skip dirs on different file systems\n"
"  -X, --exclude-from=FILE  exclude files matching any pattern in FILE\n"
"      --exclude=PATTERN  exclude files matching PATTERN\n"
"  -0, --null            end each output line with NUL, not newline\n"
"      --help            display this help and exit\n"
"      --version         output version information and exit\n"
"\n"
"Display values are in units of the first available SIZE from --block-size,\n"
"and the DU_BLOCK_SIZE, BLOCK_SIZE and BLOCKSIZE environment variables.\n"
"Otherwise, units default to 512 bytes (or 1024 bytes if POSIXLY_CORRECT\n"
"is not set).\n",
    PROGRAM_NAME);
    exit(code);
}

/* ── main ───────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    /* check environment for block size */
    const char *bs_env = getenv("DU_BLOCK_SIZE");
    if (!bs_env) bs_env = getenv("BLOCK_SIZE");
    if (!bs_env) bs_env = getenv("BLOCKSIZE");
    if (!bs_env && !getenv("POSIXLY_CORRECT")) opt.block_size = 1024;
    if (bs_env) opt.block_size = parse_size(bs_env);

    /* long options */
    static struct option longopts[] = {
        {"all",             no_argument,       NULL, 'a'},
        {"apparent-size",   no_argument,       NULL, 256},
        {"bytes",           no_argument,       NULL, 'b'},
        {"block-size",      required_argument, NULL, 'B'},
        {"total",           no_argument,       NULL, 'c'},
        {"max-depth",       required_argument, NULL, 'd'},
        {"dereference-args",no_argument,       NULL, 'D'},
        {"human-readable",  no_argument,       NULL, 'h'},
        {"inodes",          no_argument,       NULL, 257},
        {"count-links",     no_argument,       NULL, 'l'},
        {"dereference",     no_argument,       NULL, 'L'},
        {"no-dereference",  no_argument,       NULL, 'P'},
        {"summarize",       no_argument,       NULL, 's'},
        {"separate-dirs",   no_argument,       NULL, 'S'},
        {"threshold",       required_argument, NULL, 't'},
        {"time",            optional_argument, NULL, 258},
        {"time-style",      required_argument, NULL, 259},
        {"one-file-system", no_argument,       NULL, 'x'},
        {"exclude-from",    required_argument, NULL, 'X'},
        {"exclude",         required_argument, NULL, 260},
        {"null",            no_argument,       NULL, '0'},
        {"help",            no_argument,       NULL, 261},
        {"version",         no_argument,       NULL, 262},
        {NULL, 0, NULL, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "abB:cd:DHhklLmPsS:t:xX:0",
                             longopts, NULL)) != -1) {
        switch (c) {
        case 'a': opt.all = true; break;
        case 256: opt.apparent_size = true; break;
        case 'b': opt.bytes = true; opt.apparent_size = true; opt.block_size = 1; break;
        case 'B': opt.block_size = parse_size(optarg); break;
        case 'c': opt.total = true; break;
        case 'd': opt.max_depth = atoi(optarg); break;
        case 'D': case 'H': opt.deref_args = true; break;
        case 'h': opt.human = true; break;
        case 257: opt.inodes = true; break;
        case 'k': opt.block_size = 1024; break;
        case 'l': opt.count_links = true; break;
        case 'L': opt.deref = true; break;
        case 'm': opt.block_size = 1024 * 1024; break;
        case 'P': opt.no_deref = true; opt.deref = false; break;
        case 's': opt.summarize = true; opt.max_depth = 0; break;
        case 'S': opt.separate_dirs = true; break;
        case 't': opt.threshold = parse_size(optarg); break;
        case 258:
            opt.show_time = true;
            if (optarg) {
                if (!strcmp(optarg,"atime")||!strcmp(optarg,"access")||!strcmp(optarg,"use"))
                    opt.time_field = TIME_ATIME;
                else if (!strcmp(optarg,"ctime")||!strcmp(optarg,"status"))
                    opt.time_field = TIME_CTIME;
                else opt.time_field = TIME_MTIME;
            }
            break;
        case 259:
            if (!strcmp(optarg,"iso"))             opt.time_style = TS_ISO;
            else if (!strcmp(optarg,"long-iso"))   opt.time_style = TS_LONG_ISO;
            else if (!strcmp(optarg,"full-iso"))   opt.time_style = TS_FULL_ISO;
            else                                   opt.time_style = TS_LOCALE;
            break;
        case 'x': opt.one_fs = true; break;
        case 'X': add_excludes_from_file(optarg); break;
        case 260: add_exclude(optarg); break;
        case '0': opt.null_term = true; break;
        case 261: usage(0); break;
        case 262:
            printf("%s 1.0 (scratch implementation)\n", PROGRAM_NAME);
            return 0;
        default: usage(1);
        }
    }

    /* conflict resolution */
    if (opt.summarize && opt.all) {
        warn("cannot both summarize and show all entries");
        exit(1);
    }
    if (opt.summarize) opt.max_depth = 0;

    char **targets = argv + optind;
    int ntargets   = argc - optind;
    if (ntargets == 0) { targets = (char *[]){"."}; ntargets = 1; }

    for (int i = 0; i < ntargets; i++) {
        const char *path = targets[i];

        /* set filesystem root for -x */
        if (opt.one_fs) {
            struct stat rst;
            if (lstat(path, &rst) == 0) start_dev = rst.st_dev;
        }

        /* reset inode table per top-level argument (like GNU du) */
        if (!opt.count_links) inode_table_free();

        long long inodes = 0;
        long long blk = traverse(path, 0, true, &inodes);

        grand_total  += blk;
        grand_inodes += inodes;
    }

    if (opt.total) {
        print_entry(grand_total, grand_inodes, NULL, "total");
    }

    /* free excludes */
    for (int i = 0; i < nexcludes; i++) free(excludes[i]);
    inode_table_free();

    return 0;
}
