/*
 * dd.c — Advanced dd implementation from scratch
 *
 * Supports:
 *   if=, of=, bs=, ibs=, obs=, count=, skip=, seek=, conv=, iflag=, oflag=,
 *   status=none|noxfer|progress, count_bytes, skip_bytes, seek_bytes,
 *   SIGUSR1 / SIGINFO progress reporting, partial/full block tracking,
 *   all standard conv= flags (swab, sync, noerror, notrunc, excl, fdatasync,
 *   fsync, lcase, ucase, ascii, ebcdic, ibm, block, unblock),
 *   all standard iflag/oflag= flags (append, direct, dsync, sync, nonblock,
 *   noatime, noctty, nofollow, fullblock, count_bytes, skip_bytes, seek_bytes),
 *   real-time throughput display, human-readable sizes, and proper POSIX
 *   exit codes.
 *
 * Build: gcc -O2 -Wall -Wextra -o dd dd.c
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>

/* =========================================================
 * EBCDIC / ASCII conversion tables (standard IBM)
 * ========================================================= */

static const unsigned char ascii_to_ebcdic[256] = {
    0x00,0x01,0x02,0x03,0x37,0x2D,0x2E,0x2F,0x16,0x05,0x25,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x3C,0x3D,0x32,0x26,0x18,0x19,0x3F,0x27,0x1C,0x1D,0x1E,0x1F,
    0x40,0x5A,0x7F,0x7B,0x5B,0x6C,0x50,0x7D,0x4D,0x5D,0x5C,0x4E,0x6B,0x60,0x4B,0x61,
    0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0x7A,0x5E,0x4C,0x7E,0x6E,0x6F,
    0x7C,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,
    0xD7,0xD8,0xD9,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xAD,0xE0,0xBD,0x9A,0x6D,
    0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
    0x97,0x98,0x99,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xC0,0x4F,0xD0,0xA1,0x07,
    0x20,0x21,0x22,0x23,0x24,0x15,0x06,0x17,0x28,0x29,0x2A,0x2B,0x2C,0x09,0x0A,0x1B,
    0x30,0x31,0x1A,0x33,0x34,0x35,0x36,0x08,0x38,0x39,0x3A,0x3B,0x04,0x14,0x3E,0xFF,
    0x41,0xAA,0x4A,0xB1,0x9F,0xB2,0x6A,0xB5,0xBB,0xB4,0x9A,0x8A,0xB0,0xCA,0xAF,0xBC,
    0x90,0x8F,0xEA,0xFA,0xBE,0xA0,0xB6,0xB3,0x9D,0xDA,0x9B,0x8B,0xB7,0xB8,0xB9,0xAB,
    0x64,0x65,0x62,0x66,0x63,0x67,0x9E,0x68,0x74,0x71,0x72,0x73,0x78,0x75,0x76,0x77,
    0xAC,0x69,0xED,0xEE,0xEB,0xEF,0xEC,0xBF,0x80,0xFD,0xFE,0xFB,0xFC,0xBA,0xAE,0x59,
    0x44,0x45,0x42,0x46,0x43,0x47,0x9C,0x48,0x54,0x51,0x52,0x53,0x58,0x55,0x56,0x57,
    0x8C,0x49,0xCD,0xCE,0xCB,0xCF,0xCC,0xE1,0x70,0xDD,0xDE,0xDB,0xDC,0x8D,0x8E,0xDF
};

static const unsigned char ebcdic_to_ascii[256] = {
    0x00,0x01,0x02,0x03,0x9C,0x09,0x86,0x7F,0x97,0x8D,0x8E,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x9D,0x85,0x08,0x87,0x18,0x19,0x92,0x8F,0x1C,0x1D,0x1E,0x1F,
    0x80,0x81,0x82,0x83,0x84,0x0A,0x17,0x1B,0x88,0x89,0x8A,0x8B,0x8C,0x05,0x06,0x07,
    0x90,0x91,0x16,0x93,0x94,0x95,0x96,0x04,0x98,0x99,0x9A,0x9B,0x14,0x15,0x9E,0x1A,
    0x20,0xA0,0xE2,0xE4,0xE0,0xE1,0xE3,0xE5,0xE7,0xF1,0xA2,0x2E,0x3C,0x28,0x2B,0x7C,
    0x26,0xE9,0xEA,0xEB,0xE8,0xED,0xEE,0xEF,0xEC,0xDF,0x21,0x24,0x2A,0x29,0x3B,0x5E,
    0x2D,0x2F,0xC2,0xC4,0xC0,0xC1,0xC3,0xC5,0xC7,0xD1,0xA6,0x2C,0x25,0x5F,0x3E,0x3F,
    0xF8,0xC9,0xCA,0xCB,0xC8,0xCD,0xCE,0xCF,0xCC,0x60,0x3A,0x23,0x40,0x27,0x3D,0x22,
    0xD8,0x61,0x62,0x63,0x64,0x65,0x66,0x67,0x68,0x69,0xAB,0xBB,0xF0,0xFD,0xFE,0xB1,
    0xB0,0x6A,0x6B,0x6C,0x6D,0x6E,0x6F,0x70,0x71,0x72,0xAA,0xBA,0xE6,0xB8,0xC6,0xA4,
    0xB5,0x7E,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7A,0xA1,0xBF,0xD0,0x5B,0xDE,0xAE,
    0xAC,0xA3,0xA5,0xB7,0xA9,0xA7,0xB6,0xBC,0xBD,0xBE,0xDD,0xA8,0xAF,0x5D,0xB4,0xD7,
    0x7B,0x41,0x42,0x43,0x44,0x45,0x46,0x47,0x48,0x49,0xAD,0xF4,0xF6,0xF2,0xF3,0xF5,
    0x7D,0x4A,0x4B,0x4C,0x4D,0x4E,0x4F,0x50,0x51,0x52,0xB9,0xFB,0xFC,0xF9,0xFA,0xFF,
    0x5C,0xF7,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5A,0xB2,0xD4,0xD6,0xD2,0xD3,0xD5,
    0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0xB3,0xDB,0xDC,0xD9,0xDA,0x9F
};

/* IBM variant EBCDIC (slightly different from standard) */
static const unsigned char ascii_to_ibm[256] = {
    0x00,0x01,0x02,0x03,0x37,0x2D,0x2E,0x2F,0x16,0x05,0x25,0x0B,0x0C,0x0D,0x0E,0x0F,
    0x10,0x11,0x12,0x13,0x3C,0x3D,0x32,0x26,0x18,0x19,0x3F,0x27,0x1C,0x1D,0x1E,0x1F,
    0x40,0x5A,0x7F,0x7B,0x5B,0x6C,0x50,0x7D,0x4D,0x5D,0x5C,0x4E,0x6B,0x60,0x4B,0x61,
    0xF0,0xF1,0xF2,0xF3,0xF4,0xF5,0xF6,0xF7,0xF8,0xF9,0x7A,0x5E,0x4C,0x7E,0x6E,0x6F,
    0x7C,0xC1,0xC2,0xC3,0xC4,0xC5,0xC6,0xC7,0xC8,0xC9,0xD1,0xD2,0xD3,0xD4,0xD5,0xD6,
    0xD7,0xD8,0xD9,0xE2,0xE3,0xE4,0xE5,0xE6,0xE7,0xE8,0xE9,0xBA,0xE0,0xBB,0xB0,0x6D,
    0x79,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x91,0x92,0x93,0x94,0x95,0x96,
    0x97,0x98,0x99,0xA2,0xA3,0xA4,0xA5,0xA6,0xA7,0xA8,0xA9,0xC0,0x4F,0xD0,0xA1,0x07,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,
    0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F,0x3F
};

/* =========================================================
 * Constants and flags
 * ========================================================= */

/* conv= flags */
#define CONV_SWAB       (1u <<  0)
#define CONV_SYNC       (1u <<  1)
#define CONV_NOERROR    (1u <<  2)
#define CONV_NOTRUNC    (1u <<  3)
#define CONV_EXCL       (1u <<  4)
#define CONV_LCASE      (1u <<  5)
#define CONV_UCASE      (1u <<  6)
#define CONV_ASCII      (1u <<  7)  /* ebcdic->ascii */
#define CONV_EBCDIC     (1u <<  8)  /* ascii->ebcdic */
#define CONV_IBM        (1u <<  9)  /* ascii->ibm */
#define CONV_BLOCK      (1u << 10)  /* newline -> spaces (cbs) */
#define CONV_UNBLOCK    (1u << 11)  /* spaces -> newline (cbs) */
#define CONV_FDATASYNC  (1u << 12)
#define CONV_FSYNC      (1u << 13)
#define CONV_SPARSE     (1u << 14)  /* skip writing zero blocks to output */

/* iflag= / oflag= flags */
#define FLAG_APPEND     (1u <<  0)
#define FLAG_DIRECT     (1u <<  1)
#define FLAG_DSYNC      (1u <<  2)
#define FLAG_SYNC       (1u <<  3)
#define FLAG_NONBLOCK   (1u <<  4)
#define FLAG_NOATIME    (1u <<  5)
#define FLAG_NOCTTY     (1u <<  6)
#define FLAG_NOFOLLOW   (1u <<  7)
#define FLAG_FULLBLOCK  (1u <<  8)  /* iflag: retry partial reads */
#define FLAG_COUNT_BYTES (1u << 9)
#define FLAG_SKIP_BYTES  (1u << 10)
#define FLAG_SEEK_BYTES  (1u << 11)

/* status= */
#define STATUS_DEFAULT  0
#define STATUS_NONE     1
#define STATUS_NOXFER   2
#define STATUS_PROGRESS 3

/* =========================================================
 * Global state
 * ========================================================= */

typedef struct {
    /* I/O operands */
    const char *input_file;
    const char *output_file;

    /* Block sizes */
    size_t      ibs;          /* input block size  */
    size_t      obs;          /* output block size */
    size_t      cbs;          /* conversion block size (block/unblock) */

    /* Counts / offsets */
    uintmax_t   count;        /* max blocks (or bytes if count_bytes) */
    uintmax_t   skip;         /* input skip (blocks or bytes) */
    uintmax_t   seek;         /* output seek (blocks or bytes) */
    int         count_given;
    int         skip_given;
    int         seek_given;

    /* Flags */
    unsigned    conv;
    unsigned    iflag;
    unsigned    oflag;
    int         status;       /* STATUS_* */
} DDOpts;

typedef struct {
    uintmax_t   bytes_in;
    uintmax_t   bytes_out;
    uintmax_t   full_blocks_in;
    uintmax_t   partial_blocks_in;
    uintmax_t   full_blocks_out;
    uintmax_t   partial_blocks_out;
    uintmax_t   sparse_blocks;
    uintmax_t   truncated_blocks;  /* for unblock: truncated records */
    struct timeval start;
} DDStats;

/* Global so signal handler can access */
static DDStats   g_stats;
static DDOpts    g_opts;
static volatile sig_atomic_t g_print_stats = 0;
static volatile sig_atomic_t g_stop        = 0;

/* =========================================================
 * Error helpers
 * ========================================================= */

static void die(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "dd: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static void warn_msg(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "dd: warning: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

/* =========================================================
 * Size / suffix parsing
 * ========================================================= */

/*
 * Parse a size with optional suffix. Supports:
 *   c=1, w=2, b=512, kB=1000, K=1024, MB=10^6, M=2^20,
 *   GB=10^9, G=2^30, TB, T, PB, P, EB, E, ZB, Z, YB, Y
 *   and xN multiplicative form (e.g. 2x512)
 */
static uintmax_t parse_size(const char *s) {
    char *end;
    errno = 0;
    uintmax_t val = (uintmax_t)strtoull(s, &end, 0);
    if (errno) die("invalid size: '%s'", s);

    /* Multiplicative form: NxM */
    if (*end == 'x' || *end == 'X') {
        uintmax_t rhs = parse_size(end + 1);
        return val * rhs;
    }

    switch (*end) {
        case '\0': break;
        case 'c': val *= 1ULL;             end++; break;
        case 'w': val *= 2ULL;             end++; break;
        case 'b': val *= 512ULL;           end++; break;
        case 'K':
            if (end[1] == 'B') { val *= 1000ULL;           end += 2; }
            else               { val *= 1024ULL;            end++;    }
            break;
        case 'M':
            if (end[1] == 'B') { val *= 1000ULL*1000;      end += 2; }
            else               { val *= 1024ULL*1024;       end++;    }
            break;
        case 'G':
            if (end[1] == 'B') { val *= 1000ULL*1000*1000;      end += 2; }
            else               { val *= 1024ULL*1024*1024;       end++;    }
            break;
        case 'T':
            if (end[1] == 'B') { val *= 1000ULL*1000*1000*1000;      end += 2; }
            else               { val *= 1024ULL*1024*1024*1024;       end++;    }
            break;
        case 'P':
            if (end[1] == 'B') { val *= 1000ULL*1000*1000*1000*1000;      end += 2; }
            else               { val *= 1024ULL*1024*1024*1024*1024;       end++;    }
            break;
        case 'E':
            if (end[1] == 'B') { val *= 1000ULL*1000*1000*1000*1000*1000;      end += 2; }
            else               { val *= 1024ULL*1024*1024*1024*1024*1024;       end++;    }
            break;
        default:
            die("invalid suffix in size '%s'", s);
    }

    if (*end != '\0') die("invalid trailing characters in size '%s'", s);
    return val;
}

/* =========================================================
 * conv= / iflag= / oflag= parsing
 * ========================================================= */

static unsigned parse_conv(const char *s) {
    unsigned flags = 0;
    char buf[256];
    if (strlen(s) >= sizeof(buf)) die("conv= value too long");
    strncpy(buf, s, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    char *tok = strtok(buf, ",");
    while (tok) {
        if      (!strcmp(tok,"swab"))      flags |= CONV_SWAB;
        else if (!strcmp(tok,"sync"))      flags |= CONV_SYNC;
        else if (!strcmp(tok,"noerror"))   flags |= CONV_NOERROR;
        else if (!strcmp(tok,"notrunc"))   flags |= CONV_NOTRUNC;
        else if (!strcmp(tok,"excl"))      flags |= CONV_EXCL;
        else if (!strcmp(tok,"lcase"))     flags |= CONV_LCASE;
        else if (!strcmp(tok,"ucase"))     flags |= CONV_UCASE;
        else if (!strcmp(tok,"ascii"))     flags |= CONV_ASCII;
        else if (!strcmp(tok,"ebcdic"))    flags |= CONV_EBCDIC;
        else if (!strcmp(tok,"ibm"))       flags |= CONV_IBM;
        else if (!strcmp(tok,"block"))     flags |= CONV_BLOCK;
        else if (!strcmp(tok,"unblock"))   flags |= CONV_UNBLOCK;
        else if (!strcmp(tok,"fdatasync")) flags |= CONV_FDATASYNC;
        else if (!strcmp(tok,"fsync"))     flags |= CONV_FSYNC;
        else if (!strcmp(tok,"sparse"))    flags |= CONV_SPARSE;
        else die("invalid conv flag: '%s'", tok);
        tok = strtok(NULL, ",");
    }

    /* Mutual exclusion checks */
    if ((flags & CONV_ASCII) && (flags & (CONV_EBCDIC|CONV_IBM)))
        die("conv=ascii is mutually exclusive with ebcdic/ibm");
    if ((flags & CONV_LCASE) && (flags & CONV_UCASE))
        die("conv=lcase and conv=ucase are mutually exclusive");
    if ((flags & CONV_BLOCK) && (flags & CONV_UNBLOCK))
        die("conv=block and conv=unblock are mutually exclusive");
    if ((flags & CONV_EXCL)  && (flags & CONV_NOTRUNC))
        die("conv=excl and conv=notrunc are mutually exclusive");

    return flags;
}

static unsigned parse_flags(const char *s) {
    unsigned flags = 0;
    char buf[256];
    if (strlen(s) >= sizeof(buf)) die("flag value too long");
    strncpy(buf, s, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    char *tok = strtok(buf, ",");
    while (tok) {
        if      (!strcmp(tok,"append"))      flags |= FLAG_APPEND;
        else if (!strcmp(tok,"direct"))      flags |= FLAG_DIRECT;
        else if (!strcmp(tok,"dsync"))       flags |= FLAG_DSYNC;
        else if (!strcmp(tok,"sync"))        flags |= FLAG_SYNC;
        else if (!strcmp(tok,"nonblock"))    flags |= FLAG_NONBLOCK;
        else if (!strcmp(tok,"noatime"))     flags |= FLAG_NOATIME;
        else if (!strcmp(tok,"noctty"))      flags |= FLAG_NOCTTY;
        else if (!strcmp(tok,"nofollow"))    flags |= FLAG_NOFOLLOW;
        else if (!strcmp(tok,"fullblock"))   flags |= FLAG_FULLBLOCK;
        else if (!strcmp(tok,"count_bytes")) flags |= FLAG_COUNT_BYTES;
        else if (!strcmp(tok,"skip_bytes"))  flags |= FLAG_SKIP_BYTES;
        else if (!strcmp(tok,"seek_bytes"))  flags |= FLAG_SEEK_BYTES;
        else die("invalid flag: '%s'", tok);
        tok = strtok(NULL, ",");
    }
    return flags;
}

/* =========================================================
 * Argument parsing
 * ========================================================= */

static void usage(void) {
    fputs(
"Usage: dd [OPERAND]...\n"
"  if=FILE         read from FILE instead of stdin\n"
"  of=FILE         write to FILE instead of stdout\n"
"  bs=BYTES        read and write up to BYTES bytes at a time\n"
"  ibs=BYTES       read up to BYTES bytes at a time (default: 512)\n"
"  obs=BYTES       write BYTES bytes at a time (default: 512)\n"
"  cbs=BYTES       conversion block size\n"
"  count=N         copy only N input blocks\n"
"  skip=N          skip N ibs-sized input blocks\n"
"  seek=N          skip N obs-sized output blocks\n"
"  conv=CONVS      convert the file as per the comma-separated list:\n"
"                  ascii, ebcdic, ibm, lcase, ucase, swab, sync,\n"
"                  noerror, notrunc, excl, block, unblock,\n"
"                  fdatasync, fsync, sparse\n"
"  iflag=FLAGS     read flags: append,direct,dsync,sync,nonblock,\n"
"                  noatime,noctty,nofollow,fullblock,\n"
"                  count_bytes,skip_bytes\n"
"  oflag=FLAGS     write flags: append,direct,dsync,sync,nonblock,\n"
"                  noatime,noctty,nofollow,seek_bytes\n"
"  status=LEVEL    none|noxfer|progress\n"
"\n"
"N and BYTES may be followed by a suffix: c=1, w=2, b=512,\n"
"  K=1024, M=1024^2, G=1024^3, T, P, E  (or kB=1000, MB=1000^2 …)\n"
"\n"
"Sending SIGUSR1 to dd prints I/O statistics to stderr.\n",
    stderr);
    exit(1);
}

static void parse_args(int argc, char **argv) {
    /* Set defaults */
    memset(&g_opts, 0, sizeof(g_opts));
    g_opts.ibs    = 512;
    g_opts.obs    = 512;
    g_opts.status = STATUS_DEFAULT;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];

        if (!strcmp(arg, "--help") || !strcmp(arg, "-h")) usage();


        #define SW(p) (strncmp(arg,(p),strlen(p))==0 ? (arg+strlen(p)) : NULL)
        const char *val;
        if      ((val = SW("if=")))     g_opts.input_file  = val;
        else if ((val = SW("of=")))     g_opts.output_file = val;
        else if ((val = SW("conv=")))   g_opts.conv        = parse_conv(val);
        else if ((val = SW("iflag=")))  g_opts.iflag       = parse_flags(val);
        else if ((val = SW("oflag=")))  g_opts.oflag       = parse_flags(val);
        else if ((val = SW("bs=")))     { g_opts.ibs = g_opts.obs = (size_t)parse_size(val); }
        else if ((val = SW("ibs=")))    g_opts.ibs   = (size_t)parse_size(val);
        else if ((val = SW("obs=")))    g_opts.obs   = (size_t)parse_size(val);
        else if ((val = SW("cbs=")))    g_opts.cbs   = (size_t)parse_size(val);
        else if ((val = SW("count=")))  { g_opts.count = parse_size(val); g_opts.count_given = 1; }
        else if ((val = SW("skip=")))   { g_opts.skip  = parse_size(val); g_opts.skip_given  = 1; }
        else if ((val = SW("seek=")))   { g_opts.seek  = parse_size(val); g_opts.seek_given  = 1; }
        else if ((val = SW("status="))) {
            if      (!strcmp(val,"none"))     g_opts.status = STATUS_NONE;
            else if (!strcmp(val,"noxfer"))   g_opts.status = STATUS_NOXFER;
            else if (!strcmp(val,"progress")) g_opts.status = STATUS_PROGRESS;
            else die("invalid status level: '%s'", val);
        }
        else die("unrecognized operand '%s'\nTry 'dd --help' for more information.", arg);
        #undef SW
    }

    /* Propagate count_bytes / skip_bytes / seek_bytes from flags to opts */
    if (g_opts.iflag & FLAG_COUNT_BYTES) { /* count= is in bytes */ }
    if (g_opts.iflag & FLAG_SKIP_BYTES)  { /* skip=  is in bytes */ }
    if (g_opts.oflag & FLAG_SEEK_BYTES)  { /* seek=  is in bytes */ }

    /* Validate */
    if (g_opts.ibs == 0) die("ibs=0 is not allowed");
    if (g_opts.obs == 0) die("obs=0 is not allowed");
    if ((g_opts.conv & (CONV_BLOCK|CONV_UNBLOCK)) && g_opts.cbs == 0)
        die("cbs= required for conv=block or conv=unblock");
}

/* =========================================================
 * Open file descriptors
 * ========================================================= */

static int build_oflags(unsigned flags, int is_input) {
    int ofl = 0;
    if (flags & FLAG_APPEND)   ofl |= O_APPEND;
    if (flags & FLAG_NONBLOCK) ofl |= O_NONBLOCK;
    if (flags & FLAG_NOCTTY)   ofl |= O_NOCTTY;
#ifdef O_NOATIME
    if (flags & FLAG_NOATIME)  ofl |= O_NOATIME;
#endif
#ifdef O_NOFOLLOW
    if (flags & FLAG_NOFOLLOW) ofl |= O_NOFOLLOW;
#endif
#ifdef O_DIRECT
    if (flags & FLAG_DIRECT)   ofl |= O_DIRECT;
#endif
    if (!is_input) {
#ifdef O_DSYNC
        if (flags & FLAG_DSYNC) ofl |= O_DSYNC;
#endif
#ifdef O_SYNC
        if (flags & FLAG_SYNC)  ofl |= O_SYNC;
#endif
    }
    return ofl;
}

static int open_input(void) {
    if (!g_opts.input_file || !strcmp(g_opts.input_file, "-"))
        return STDIN_FILENO;

    int ofl = O_RDONLY | build_oflags(g_opts.iflag, 1);
    int fd = open(g_opts.input_file, ofl);
    if (fd < 0) die("failed to open '%s': %s", g_opts.input_file, strerror(errno));
    return fd;
}

static int open_output(void) {
    if (!g_opts.output_file || !strcmp(g_opts.output_file, "-"))
        return STDOUT_FILENO;

    int ofl = O_WRONLY | O_CREAT | build_oflags(g_opts.oflag, 0);

    if (g_opts.conv & CONV_EXCL)   ofl |= O_EXCL;
    if (g_opts.conv & CONV_NOTRUNC) { /* don't truncate */ }
    else                             ofl |= O_TRUNC;

    if (g_opts.oflag & FLAG_APPEND) ofl |= O_APPEND;

    int fd = open(g_opts.output_file, ofl, 0666);
    if (fd < 0) die("failed to open '%s': %s", g_opts.output_file, strerror(errno));
    return fd;
}

/* =========================================================
 * Statistics / progress display
 * ========================================================= */

static double elapsed_sec(void) {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (double)(now.tv_sec  - g_stats.start.tv_sec)
         + (double)(now.tv_usec - g_stats.start.tv_usec) / 1e6;
}

/* Format a byte count as human-readable (e.g. 1.5 MB) */
static void fmt_bytes(char *buf, size_t bufsz, uintmax_t bytes) {
    const char *units[] = { "B", "KB", "MB", "GB", "TB", "PB", "EB" };
    int u = 0;
    double v = (double)bytes;
    while (v >= 1024.0 && u < 6) { v /= 1024.0; u++; }
    if (u == 0)
        snprintf(buf, bufsz, "%ju B", bytes);
    else
        snprintf(buf, bufsz, "%.1f %s (%ju bytes)", v, units[u], bytes);
}

/* Format a transfer rate */
static void fmt_rate(char *buf, size_t bufsz, double bytes_per_sec) {
    const char *units[] = { "B/s", "KB/s", "MB/s", "GB/s", "TB/s" };
    int u = 0;
    double v = bytes_per_sec;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
    snprintf(buf, bufsz, "%.1f %s", v, units[u]);
}

static void print_stats(void) {
    double secs = elapsed_sec();
    if (secs < 1e-9) secs = 1e-9;

    char sz_in[64], sz_out[64], rate[64];
    fmt_bytes(sz_in,  sizeof(sz_in),  g_stats.bytes_in);
    fmt_bytes(sz_out, sizeof(sz_out), g_stats.bytes_out);
    fmt_rate (rate,   sizeof(rate),   (double)g_stats.bytes_out / secs);

    fprintf(stderr,
        "%ju+%ju records in\n"
        "%ju+%ju records out\n",
        g_stats.full_blocks_in,  g_stats.partial_blocks_in,
        g_stats.full_blocks_out, g_stats.partial_blocks_out);

    if (g_stats.truncated_blocks)
        fprintf(stderr, "%ju truncated block%s\n",
            g_stats.truncated_blocks,
            g_stats.truncated_blocks == 1 ? "" : "s");

    if (g_stats.sparse_blocks)
        fprintf(stderr, "%ju sparse block%s\n",
            g_stats.sparse_blocks,
            g_stats.sparse_blocks == 1 ? "" : "s");

    fprintf(stderr, "%s copied, %.6f s, %s\n", sz_out, secs, rate);
}

/* Inline progress (STATUS_PROGRESS) — overwrites same line */
static void print_progress(void) {
    double secs = elapsed_sec();
    if (secs < 1e-9) secs = 1e-9;
    char sz[64], rate[64];
    fmt_bytes(sz,   sizeof(sz),   g_stats.bytes_out);
    fmt_rate (rate, sizeof(rate), (double)g_stats.bytes_out / secs);
    fprintf(stderr, "\r%s copied, %.1f s, %s   ", sz, secs, rate);
    fflush(stderr);
}

/* =========================================================
 * Signal handlers
 * ========================================================= */

static void handle_usr1(int sig) {
    (void)sig;
    g_print_stats = 1;
}

static void handle_int(int sig) {
    (void)sig;
    g_stop = 1;
}

/* =========================================================
 * Reliable read / write helpers
 * ========================================================= */

/*
 * Read exactly 'count' bytes (with fullblock semantics).
 * Returns bytes read, 0 on EOF, -1 on error.
 */
static ssize_t read_full(int fd, void *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t n = read(fd, (char*)buf + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) break; /* EOF */
        total += (size_t)n;
        /* Without FULLBLOCK, stop after first partial */
        if (!(g_opts.iflag & FLAG_FULLBLOCK)) break;
    }
    return (ssize_t)total;
}

/*
 * Write exactly 'count' bytes, retrying on EINTR.
 * Returns bytes written, -1 on error.
 */
static ssize_t write_all(int fd, const void *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t n = write(fd, (const char*)buf + total, count - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

/* =========================================================
 * Conversion: apply conv= transformations to a buffer
 * ========================================================= */

/* Swap adjacent bytes (CONV_SWAB) */
static void do_swab(unsigned char *buf, size_t len) {
    for (size_t i = 0; i + 1 < len; i += 2) {
        unsigned char tmp = buf[i];
        buf[i]   = buf[i+1];
        buf[i+1] = tmp;
    }
    /* If odd length, last byte is unchanged (standard behaviour) */
}

/* Apply per-byte table conversion */
static void do_table(unsigned char *buf, size_t len, const unsigned char *table) {
    for (size_t i = 0; i < len; i++)
        buf[i] = table[(unsigned char)buf[i]];
}

/* lcase */
static void do_lcase(unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (isupper(buf[i])) buf[i] = (unsigned char)tolower(buf[i]);
}

/* ucase */
static void do_ucase(unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (islower(buf[i])) buf[i] = (unsigned char)toupper(buf[i]);
}

/*
 * conv=block:
 *   Replace newlines with spaces, pad records to cbs with spaces.
 *   Input is treated as variable-length lines.
 *   *out_len receives the actual number of bytes written to 'out'.
 */
static void do_block(const unsigned char *in, size_t in_len,
                     unsigned char *out, size_t *out_len, size_t cbs) {
    size_t col = 0;
    size_t oi  = 0;

    for (size_t i = 0; i < in_len; i++) {
        if (in[i] == '\n') {
            /* Pad with spaces to cbs */
            while (col < cbs) { out[oi++] = ' '; col++; }
            col = 0;
        } else {
            if (col < cbs) {
                out[oi++] = in[i];
                col++;
            } else {
                /* Truncate: record is longer than cbs */
                g_stats.truncated_blocks++;
                /* Skip until next newline */
                while (i + 1 < in_len && in[i+1] != '\n') i++;
            }
        }
    }
    /* Pad the final (possibly incomplete) record */
    if (col > 0) {
        while (col < cbs) { out[oi++] = ' '; col++; }
    }
    *out_len = oi;
}

/*
 * conv=unblock:
 *   Convert cbs-sized fixed records back to newline-terminated lines
 *   by stripping trailing spaces and appending '\n'.
 */
static void do_unblock(const unsigned char *in, size_t in_len,
                       unsigned char *out, size_t *out_len, size_t cbs) {
    size_t oi = 0;
    for (size_t base = 0; base < in_len; base += cbs) {
        size_t rec_len = (base + cbs <= in_len) ? cbs : (in_len - base);
        /* Strip trailing spaces */
        size_t end = rec_len;
        while (end > 0 && in[base + end - 1] == ' ') end--;
        for (size_t j = 0; j < end; j++)
            out[oi++] = in[base + j];
        out[oi++] = '\n';
    }
    *out_len = oi;
}

/* Check if a block is all-zero (for sparse output) */
static int is_zero_block(const unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len; i++)
        if (buf[i]) return 0;
    return 1;
}

/*
 * Apply all requested conv= transformations in-place.
 * For block/unblock a scratch buffer is needed so we pass one in.
 * Returns new length (may differ for block/unblock).
 */
static size_t apply_conv(unsigned char *buf, size_t len,
                         unsigned char *scratch, size_t scratch_sz) {
    unsigned c = g_opts.conv;

    /* Character set conversions first */
    if (c & CONV_ASCII)  do_table(buf, len, ebcdic_to_ascii);
    if (c & CONV_EBCDIC) do_table(buf, len, ascii_to_ebcdic);
    if (c & CONV_IBM)    do_table(buf, len, ascii_to_ibm);

    /* Case conversion */
    if (c & CONV_LCASE) do_lcase(buf, len);
    if (c & CONV_UCASE) do_ucase(buf, len);

    /* Byte swap */
    if (c & CONV_SWAB) do_swab(buf, len);

    /* Block / unblock — needs scratch */
    if ((c & CONV_BLOCK) || (c & CONV_UNBLOCK)) {
        size_t new_len = 0;
        /* Estimate worst-case output size */
        size_t need = (c & CONV_BLOCK)
                    ? (((len / g_opts.cbs) + 2) * g_opts.cbs)
                    : (len + len / g_opts.cbs + 2);
        if (need > scratch_sz)
            die("conversion buffer overflow (cbs too large?)");

        if (c & CONV_BLOCK)
            do_block(buf, len, scratch, &new_len, g_opts.cbs);
        else
            do_unblock(buf, len, scratch, &new_len, g_opts.cbs);

        if (new_len > scratch_sz)
            die("conversion output overflow");
        memcpy(buf, scratch, new_len);
        len = new_len;
    }

    return len;
}

/* =========================================================
 * Output buffering
 * ========================================================= */

typedef struct {
    unsigned char *buf;
    size_t         cap;
    size_t         len;
    int            fd;
    int            is_sparse;
} OutBuf;

static void outbuf_init(OutBuf *ob, size_t obs, int fd) {
    ob->cap = obs;
    ob->buf = malloc(obs);
    if (!ob->buf) die("out of memory");
    ob->len      = 0;
    ob->fd       = fd;
    ob->is_sparse = (g_opts.conv & CONV_SPARSE) ? 1 : 0;
}

/* Flush exactly one obs-sized chunk (or remaining bytes) */
static int outbuf_flush_block(OutBuf *ob, size_t amount, int is_eof) {
    if (amount == 0) return 0;

    unsigned char *data = ob->buf;
    int full = (amount == ob->cap);

    /* Sparse: seek over zero blocks instead of writing */
    if (ob->is_sparse && is_zero_block(data, amount)) {
        if (lseek(ob->fd, (off_t)amount, SEEK_CUR) == (off_t)-1) {
            /* Fall through to write if not seekable */
            goto do_write;
        }
        g_stats.sparse_blocks++;
        if (is_eof) {
            /* Sparse: write a single zero byte at end to set file size */
            unsigned char zero = 0;
            if (write(ob->fd, &zero, 1) != 1)
                die("write error: %s", strerror(errno));
            if (lseek(ob->fd, -1, SEEK_CUR) == (off_t)-1) { /* best effort */ }
        }
        g_stats.bytes_out += amount;
        if (full) g_stats.full_blocks_out++;
        else      g_stats.partial_blocks_out++;
        return 0;
    }

do_write:;
    ssize_t written = write_all(ob->fd, data, amount);
    if (written < 0)
        die("write error: %s", strerror(errno));

    g_stats.bytes_out += (uintmax_t)written;
    if ((size_t)written == ob->cap) g_stats.full_blocks_out++;
    else                            g_stats.partial_blocks_out++;
    return 0;
}

/* Append data to output buffer, flushing obs-sized blocks as needed */
static int outbuf_write(OutBuf *ob, const unsigned char *data, size_t len) {
    while (len > 0) {
        size_t space = ob->cap - ob->len;
        size_t copy  = (len < space) ? len : space;
        memcpy(ob->buf + ob->len, data, copy);
        ob->len += copy;
        data    += copy;
        len     -= copy;

        if (ob->len == ob->cap) {
            if (outbuf_flush_block(ob, ob->cap, 0) != 0) return -1;
            ob->len = 0;
        }
    }
    return 0;
}

/* Flush remaining bytes at end of input */
static int outbuf_finish(OutBuf *ob) {
    if (ob->len == 0) return 0;

    /* conv=sync: pad the last input block if needed (handled in main loop),
       here we just flush whatever remains. */
    return outbuf_flush_block(ob, ob->len, 1);
}

static void outbuf_free(OutBuf *ob) {
    free(ob->buf);
    ob->buf = NULL;
}

/* =========================================================
 * Skip / seek helpers
 * ========================================================= */

/* Try to lseek; if not seekable, read-and-discard */
static void skip_input(int fd, uintmax_t skip_bytes) {
    if (skip_bytes == 0) return;

    /* Try lseek first */
    struct stat st;
    int can_seek = (fstat(fd, &st) == 0 && (S_ISREG(st.st_mode) || S_ISBLK(st.st_mode)));

    if (can_seek) {
        if (lseek(fd, (off_t)skip_bytes, SEEK_SET) == (off_t)-1) {
            /* Not seekable after all */
            can_seek = 0;
        }
    }

    if (!can_seek) {
        /* Read and discard */
        size_t buf_sz = 65536;
        unsigned char *buf = malloc(buf_sz);
        if (!buf) die("out of memory");
        uintmax_t remaining = skip_bytes;
        while (remaining > 0) {
            size_t want = (remaining < buf_sz) ? (size_t)remaining : buf_sz;
            ssize_t n   = read(fd, buf, want);
            if (n < 0) { if (errno==EINTR) continue; die("skip read error: %s", strerror(errno)); }
            if (n == 0) break; /* EOF during skip */
            remaining -= (uintmax_t)n;
        }
        free(buf);
    }
}

static void seek_output(int fd, uintmax_t seek_bytes) {
    if (seek_bytes == 0) return;
    if (lseek(fd, (off_t)seek_bytes, SEEK_SET) == (off_t)-1)
        die("output seek failed: %s", strerror(errno));
}

/* =========================================================
 * Main copy loop
 * ========================================================= */

static void do_copy(int ifd, int ofd) {
    size_t ibs = g_opts.ibs;
    size_t obs = g_opts.obs;

    /* Allocate input buffer; extra space for conv=block/unblock expansion */
    size_t conv_extra = (g_opts.cbs > 0) ? (g_opts.cbs * 4 + ibs) : ibs;
    size_t ibuf_sz    = ibs + conv_extra + 16;
    unsigned char *ibuf    = malloc(ibuf_sz);
    unsigned char *scratch = malloc(ibuf_sz * 4);
    if (!ibuf || !scratch) die("out of memory");

    OutBuf ob;
    outbuf_init(&ob, obs, ofd);

    /* Determine count in bytes */
    uintmax_t count_limit = UINTMAX_MAX;
    if (g_opts.count_given) {
        if (g_opts.iflag & FLAG_COUNT_BYTES)
            count_limit = g_opts.count;
        else
            count_limit = g_opts.count * (uintmax_t)ibs;
    }

    uintmax_t bytes_read_total = 0;

    /* Progress timer */
    struct timeval last_progress;
    gettimeofday(&last_progress, NULL);

    for (;;) {
        /* Check signals */
        if (g_stop) break;
        if (g_print_stats) {
            print_stats();
            g_print_stats = 0;
        }

        /* Check count limit */
        if (bytes_read_total >= count_limit) break;

        /* How many bytes to read this iteration? */
        size_t want = ibs;
        if (count_limit != UINTMAX_MAX) {
            uintmax_t remaining = count_limit - bytes_read_total;
            if (remaining < (uintmax_t)want)
                want = (size_t)remaining;
        }

        ssize_t nr = read_full(ifd, ibuf, want);
        if (nr < 0) {
            if (g_opts.conv & CONV_NOERROR) {
                warn_msg("read error: %s", strerror(errno));
                /* conv=sync: fill with NULs */
                if (g_opts.conv & CONV_SYNC) {
                    memset(ibuf, 0, want);
                    nr = (ssize_t)want;
                    g_stats.partial_blocks_in++;
                } else {
                    g_stats.partial_blocks_in++;
                    continue;
                }
            } else {
                die("read error: %s", strerror(errno));
            }
        }

        if (nr == 0) break; /* EOF */

        size_t len = (size_t)nr;
        bytes_read_total += len;
        g_stats.bytes_in += len;

        if (len == ibs)  g_stats.full_blocks_in++;
        else             g_stats.partial_blocks_in++;

        /* conv=sync: pad short blocks with NUL (or space for block/unblock) */
        if ((g_opts.conv & CONV_SYNC) && len < ibs) {
            unsigned char pad = (g_opts.conv & (CONV_BLOCK|CONV_UNBLOCK)) ? ' ' : '\0';
            memset(ibuf + len, pad, ibs - len);
            len = ibs;
        }

        /* Apply conversions */
        if (g_opts.conv & ~(CONV_NOERROR|CONV_SYNC|CONV_NOTRUNC|CONV_EXCL|
                            CONV_FDATASYNC|CONV_FSYNC|CONV_SPARSE)) {
            len = apply_conv(ibuf, len, scratch, ibuf_sz * 4);
        }

        /* Write to output buffer */
        if (outbuf_write(&ob, ibuf, len) != 0)
            die("output error");

        /* Progress display */
        if (g_opts.status == STATUS_PROGRESS) {
            struct timeval now;
            gettimeofday(&now, NULL);
            double diff = (double)(now.tv_sec  - last_progress.tv_sec)
                        + (double)(now.tv_usec - last_progress.tv_usec) / 1e6;
            if (diff >= 1.0) {
                print_progress();
                last_progress = now;
            }
        }
    }

    /* Flush remaining output */
    outbuf_finish(&ob);

    /* Clear progress line */
    if (g_opts.status == STATUS_PROGRESS)
        fprintf(stderr, "\n");

    /* fsync / fdatasync */
    if (g_opts.conv & CONV_FSYNC) {
        if (fsync(ofd) != 0)
            die("fsync failed: %s", strerror(errno));
    } else if (g_opts.conv & CONV_FDATASYNC) {
        if (fdatasync(ofd) != 0)
            die("fdatasync failed: %s", strerror(errno));
    }

    outbuf_free(&ob);
    free(ibuf);
    free(scratch);
}

/* =========================================================
 * Entry point
 * ========================================================= */

int main(int argc, char **argv) {
    parse_args(argc, argv);

    /* Install signal handlers */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_usr1;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    /* SIGINT: allow clean exit with stats */
    sa.sa_handler = handle_int;
    sigaction(SIGINT, &sa, NULL);

#ifdef SIGINFO
    /* BSD-style SIGINFO (like ctrl-T) */
    sa.sa_handler = handle_usr1;
    sigaction(SIGINFO, &sa, NULL);
#endif

    /* Open files */
    int ifd = open_input();
    int ofd = open_output();

    /* Initialize stats */
    memset(&g_stats, 0, sizeof(g_stats));
    gettimeofday(&g_stats.start, NULL);

    /* Compute skip bytes */
    uintmax_t skip_bytes = 0;
    if (g_opts.skip_given) {
        if (g_opts.iflag & FLAG_SKIP_BYTES)
            skip_bytes = g_opts.skip;
        else
            skip_bytes = g_opts.skip * (uintmax_t)g_opts.ibs;
    }

    /* Compute seek bytes */
    uintmax_t seek_bytes = 0;
    if (g_opts.seek_given) {
        if (g_opts.oflag & FLAG_SEEK_BYTES)
            seek_bytes = g_opts.seek;
        else
            seek_bytes = g_opts.seek * (uintmax_t)g_opts.obs;
    }

    skip_input (ifd, skip_bytes);
    seek_output(ofd, seek_bytes);

    /* Run the copy */
    do_copy(ifd, ofd);

    /* Close files */
    if (ifd != STDIN_FILENO)  close(ifd);
    if (ofd != STDOUT_FILENO) close(ofd);

    /* Print final statistics */
    if (g_opts.status != STATUS_NONE) {
        if (g_opts.status != STATUS_NOXFER || g_stop)
            print_stats();
        else {
            /* status=noxfer: print record counts but not xfer line */
            fprintf(stderr,
                "%ju+%ju records in\n"
                "%ju+%ju records out\n",
                g_stats.full_blocks_in,  g_stats.partial_blocks_in,
                g_stats.full_blocks_out, g_stats.partial_blocks_out);
        }
    }

    return g_stop ? 1 : 0;
}
