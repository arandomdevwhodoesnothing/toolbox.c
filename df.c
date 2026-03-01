/*
 * df.c - Implementation of the df (disk free) command
 *
 * Supports all standard df options:
 *   -a, --all                include dummy file systems
 *   -B, --block-size=SIZE    scale sizes by SIZE before printing
 *   -h, --human-readable     print sizes in human readable format (e.g., 1K 234M 2G)
 *   -H, --si                 likewise, but use powers of 1000 not 1024
 *   -i, --inodes             list inode information instead of block usage
 *   -k                       like --block-size=1K
 *   -l, --local              limit listing to local file systems
 *   -m                       like --block-size=1M
 *       --no-sync            do not invoke sync before getting usage info (default)
 *       --output[=FIELD_LIST] use the output format defined by FIELD_LIST
 *   -P, --portability        use the POSIX output format
 *       --sync               invoke sync before getting usage info
 *       --total              elide all entries insignificant to available space,
 *                            and produce a grand total
 *   -t, --type=TYPE          limit listing to file systems of type TYPE
 *   -T, --print-type         print file system type
 *   -x, --exclude-type=TYPE  limit listing to file systems not of type TYPE
 *       --help               display this help and exit
 *       --version            output version information and exit
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <getopt.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>
#include <mntent.h>
#include <math.h>
#include <limits.h>

#define VERSION      "1.0.0"
#define PROGRAM      "df"
#define MAX_TYPES    64
#define MAX_FIELDS   16

/* Output fields for --output */
typedef enum {
    FIELD_SOURCE,
    FIELD_FSTYPE,
    FIELD_SIZE,
    FIELD_USED,
    FIELD_AVAIL,
    FIELD_PCENT,
    FIELD_ITOTAL,
    FIELD_IUSED,
    FIELD_IAVAIL,
    FIELD_IPCENT,
    FIELD_TARGET,
    FIELD_FILE,
    FIELD_COUNT
} Field;

static const char *field_names[] = {
    "source", "fstype", "size", "used", "avail", "pcent",
    "itotal", "iused", "iavail", "ipcent", "target", "file"
};

/* Options */
static bool opt_all         = false;
static bool opt_human       = false;   /* -h: powers of 1024 */
static bool opt_si          = false;   /* -H: powers of 1000 */
static bool opt_inodes      = false;
static bool opt_local       = false;
static bool opt_portability = false;
static bool opt_print_type  = false;
static bool opt_sync        = false;
static bool opt_total       = false;
static bool opt_output      = false;
static long long opt_block_size = 1024; /* default 1K blocks */

static char *include_types[MAX_TYPES];
static int   include_type_count = 0;
static char *exclude_types[MAX_TYPES];
static int   exclude_type_count = 0;

static Field output_fields[MAX_FIELDS];
static int   output_field_count = 0;

/* ------------------------------------------------------------------ */
/* Size formatting                                                      */
/* ------------------------------------------------------------------ */

static char *fmt_size_human(unsigned long long bytes, bool si) {
    static char buf[32];
    const char *units_si[]   = { "", "K", "M", "G", "T", "P", "E" };
    const char *units_iec[]  = { "", "K", "M", "G", "T", "P", "E" };
    double base = si ? 1000.0 : 1024.0;
    double val  = (double)bytes;
    int idx = 0;
    while (val >= base && idx < 6) { val /= base; idx++; }
    if (idx == 0)
        snprintf(buf, sizeof(buf), "%llu", bytes);
    else if (val < 10.0)
        snprintf(buf, sizeof(buf), "%.1f%s", val, si ? units_si[idx] : units_iec[idx]);
    else
        snprintf(buf, sizeof(buf), "%.0f%s", val, si ? units_si[idx] : units_iec[idx]);
    return buf;
}

static char *fmt_size_blocks(unsigned long long bytes, long long block_size) {
    static char buf[32];
    unsigned long long blocks = (bytes + block_size - 1) / block_size;
    snprintf(buf, sizeof(buf), "%llu", blocks);
    return buf;
}

static char *fmt_size(unsigned long long bytes) {
    if (opt_human) return fmt_size_human(bytes, false);
    if (opt_si)    return fmt_size_human(bytes, true);
    return fmt_size_blocks(bytes, opt_block_size);
}

/* ------------------------------------------------------------------ */
/* Block-size parsing                                                   */
/* ------------------------------------------------------------------ */

static long long parse_block_size(const char *s) {
    char *end;
    long long val = strtoll(s, &end, 10);
    if (val <= 0) {
        fprintf(stderr, "%s: invalid block size '%s'\n", PROGRAM, s);
        exit(EXIT_FAILURE);
    }
    switch (*end) {
    case 'K': case 'k': val *= 1024LL;                      break;
    case 'M': case 'm': val *= 1024LL * 1024;               break;
    case 'G': case 'g': val *= 1024LL * 1024 * 1024;        break;
    case 'T': case 't': val *= 1024LL * 1024 * 1024 * 1024; break;
    case '\0': break;
    default:
        fprintf(stderr, "%s: invalid block size '%s'\n", PROGRAM, s);
        exit(EXIT_FAILURE);
    }
    return val;
}

/* ------------------------------------------------------------------ */
/* Field list parsing for --output                                      */
/* ------------------------------------------------------------------ */

static void parse_output_fields(const char *list) {
    char *copy = strdup(list);
    char *tok  = strtok(copy, ",");
    while (tok) {
        bool found = false;
        for (int i = 0; i < FIELD_COUNT; i++) {
            if (strcmp(tok, field_names[i]) == 0) {
                if (output_field_count >= MAX_FIELDS) {
                    fprintf(stderr, "%s: too many output fields\n", PROGRAM);
                    exit(EXIT_FAILURE);
                }
                output_fields[output_field_count++] = (Field)i;
                found = true;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "%s: unknown field '%s'\n", PROGRAM, tok);
            exit(EXIT_FAILURE);
        }
        tok = strtok(NULL, ",");
    }
    free(copy);
}

/* ------------------------------------------------------------------ */
/* Filesystem entry                                                     */
/* ------------------------------------------------------------------ */

typedef struct {
    char source[256];
    char target[PATH_MAX];
    char fstype[64];
    char file[PATH_MAX];   /* if queried by path */

    unsigned long long blk_total;
    unsigned long long blk_used;
    unsigned long long blk_avail;
    unsigned long long blk_size;   /* fundamental block size in bytes */

    unsigned long long ino_total;
    unsigned long long ino_used;
    unsigned long long ino_avail;

    bool valid;
    bool is_dummy;  /* 0-block pseudo fs */
} FSEntry;

static bool type_in_list(const char *fstype, char **list, int count) {
    for (int i = 0; i < count; i++)
        if (strcmp(fstype, list[i]) == 0) return true;
    return false;
}

/* Pseudo / dummy filesystems (no real storage) */
static bool is_dummy_fs(const char *fstype) {
    static const char *dummies[] = {
        "proc", "sysfs", "devpts", "devtmpfs", "tmpfs", "cgroup",
        "cgroup2", "pstore", "bpf", "tracefs", "securityfs", "debugfs",
        "hugetlbfs", "mqueue", "fusectl", "configfs", "efivarfs",
        "autofs", "overlay", "squashfs", "ramfs",
        NULL
    };
    for (int i = 0; dummies[i]; i++)
        if (strcmp(fstype, dummies[i]) == 0) return true;
    return false;
}

static bool is_local_fs(const char *source) {
    /* Remote fs sources typically contain ':' (NFS, CIFS) or '//' */
    return !(strchr(source, ':') || strncmp(source, "//", 2) == 0);
}

static FSEntry make_entry(const char *source, const char *target,
                           const char *fstype, const char *file) {
    FSEntry e;
    memset(&e, 0, sizeof(e));
    strncpy(e.source, source,  sizeof(e.source)  - 1);
    strncpy(e.target, target,  sizeof(e.target)  - 1);
    strncpy(e.fstype, fstype,  sizeof(e.fstype)  - 1);
    strncpy(e.file,   file ? file : "", sizeof(e.file) - 1);

    struct statvfs sv;
    if (statvfs(target, &sv) != 0) {
        e.valid = false;
        return e;
    }

    e.blk_size  = sv.f_frsize ? sv.f_frsize : sv.f_bsize;
    e.blk_total = sv.f_blocks * e.blk_size;
    e.blk_avail = sv.f_bavail * e.blk_size;
    /* Used = total - free (f_bfree includes reserved blocks) */
    unsigned long long blk_free_total = (unsigned long long)sv.f_bfree * e.blk_size;
    e.blk_used  = e.blk_total > blk_free_total ? e.blk_total - blk_free_total : 0;

    e.ino_total = sv.f_files;
    e.ino_avail = sv.f_favail;
    e.ino_used  = sv.f_files > sv.f_ffree ? sv.f_files - sv.f_ffree : 0;

    e.is_dummy  = (sv.f_blocks == 0);
    e.valid     = true;
    return e;
}

/* ------------------------------------------------------------------ */
/* Printing                                                             */
/* ------------------------------------------------------------------ */

static void print_header_default(bool show_type) {
    if (show_type)
        printf("%-20s %-10s %12s %12s %12s %5s %s\n",
               "Filesystem", "Type", "1K-blocks", "Used", "Available", "Use%", "Mounted on");
    else
        printf("%-20s %12s %12s %12s %5s %s\n",
               "Filesystem", "1K-blocks", "Used", "Available", "Use%", "Mounted on");
}

static void print_header_posix(void) {
    printf("%-20s %12s %12s %12s %5s %s\n",
           "Filesystem", "512-blocks", "Used", "Available", "Capacity", "Mounted on");
}

static void print_header_inodes(bool show_type) {
    if (show_type)
        printf("%-20s %-10s %12s %12s %12s %5s %s\n",
               "Filesystem", "Type", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on");
    else
        printf("%-20s %12s %12s %12s %5s %s\n",
               "Filesystem", "Inodes", "IUsed", "IFree", "IUse%", "Mounted on");
}

static void print_header_output(void) {
    for (int i = 0; i < output_field_count; i++) {
        if (i) printf(" ");
        printf("%-*s", (int)strlen(field_names[output_fields[i]]), field_names[output_fields[i]]);
    }
    printf("\n");
}

static char pct_buf[16];
static const char *pct(unsigned long long used, unsigned long long total) {
    if (total == 0) { snprintf(pct_buf, sizeof(pct_buf), "-"); return pct_buf; }
    snprintf(pct_buf, sizeof(pct_buf), "%llu%%", (unsigned long long)(100.0 * used / total + 0.5));
    return pct_buf;
}

static void print_entry(const FSEntry *e, bool show_type) {
    if (opt_output) {
        for (int i = 0; i < output_field_count; i++) {
            if (i) printf(" ");
            switch (output_fields[i]) {
            case FIELD_SOURCE:  printf("%-20s", e->source); break;
            case FIELD_FSTYPE:  printf("%-10s", e->fstype); break;
            case FIELD_SIZE:    printf("%12s",  fmt_size(e->blk_total)); break;
            case FIELD_USED:    printf("%12s",  fmt_size(e->blk_used));  break;
            case FIELD_AVAIL:   printf("%12s",  fmt_size(e->blk_avail)); break;
            case FIELD_PCENT:   printf("%5s",   pct(e->blk_used, e->blk_total)); break;
            case FIELD_ITOTAL:  printf("%12llu", e->ino_total); break;
            case FIELD_IUSED:   printf("%12llu", e->ino_used);  break;
            case FIELD_IAVAIL:  printf("%12llu", e->ino_avail); break;
            case FIELD_IPCENT:  printf("%5s",   pct(e->ino_used, e->ino_total)); break;
            case FIELD_TARGET:  printf("%s",    e->target); break;
            case FIELD_FILE:    printf("%s",    e->file[0] ? e->file : "-"); break;
            default: break;
            }
        }
        printf("\n");
        return;
    }

    if (opt_portability) {
        /* POSIX: 512-byte blocks */
        unsigned long long b512 = (e->blk_total + 511) / 512;
        unsigned long long u512 = (e->blk_used  + 511) / 512;
        unsigned long long a512 = (e->blk_avail + 511) / 512;
        printf("%-20s %12llu %12llu %12llu %5s %s\n",
               e->source, b512, u512, a512,
               pct(e->blk_used, e->blk_total), e->target);
        return;
    }

    if (opt_inodes) {
        if (show_type)
            printf("%-20s %-10s %12llu %12llu %12llu %5s %s\n",
                   e->source, e->fstype,
                   e->ino_total, e->ino_used, e->ino_avail,
                   pct(e->ino_used, e->ino_total), e->target);
        else
            printf("%-20s %12llu %12llu %12llu %5s %s\n",
                   e->source,
                   e->ino_total, e->ino_used, e->ino_avail,
                   pct(e->ino_used, e->ino_total), e->target);
        return;
    }

    /* Default block output */
    if (show_type)
        printf("%-20s %-10s %12s %12s %12s %5s %s\n",
               e->source, e->fstype,
               fmt_size(e->blk_total), fmt_size(e->blk_used), fmt_size(e->blk_avail),
               pct(e->blk_used, e->blk_total), e->target);
    else
        printf("%-20s %12s %12s %12s %5s %s\n",
               e->source,
               fmt_size(e->blk_total), fmt_size(e->blk_used), fmt_size(e->blk_avail),
               pct(e->blk_used, e->blk_total), e->target);
}

/* ------------------------------------------------------------------ */
/* Help / version                                                       */
/* ------------------------------------------------------------------ */

static void usage_msg(int status) {
    if (status != EXIT_SUCCESS) {
        fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM);
        exit(status);
    }
    printf("Usage: %s [OPTION]... [FILE]...\n", PROGRAM);
    printf("\nShow information about the file system on which each FILE resides,\n");
    printf("or all file systems by default.\n\n");
    printf("  -a, --all               include pseudo, duplicate, inaccessible file systems\n");
    printf("  -B, --block-size=SIZE   scale sizes by SIZE before printing\n");
    printf("  -h, --human-readable    print sizes in human readable format (e.g., 1K 234M 2G)\n");
    printf("  -H, --si                likewise, but use powers of 1000 not 1024\n");
    printf("  -i, --inodes            list inode information instead of block usage\n");
    printf("  -k                      like --block-size=1K\n");
    printf("  -l, --local             limit listing to local file systems\n");
    printf("  -m                      like --block-size=1M\n");
    printf("      --no-sync           do not invoke sync before getting usage info (default)\n");
    printf("      --output[=FIELD_LIST]\n");
    printf("                          use the output format defined by FIELD_LIST,\n");
    printf("                          or print all fields if FIELD_LIST is omitted\n");
    printf("  -P, --portability       use the POSIX output format\n");
    printf("      --sync              invoke sync before getting usage info\n");
    printf("      --total             elide all entries insignificant to available space,\n");
    printf("                          and produce a grand total\n");
    printf("  -t, --type=TYPE         limit listing to file systems of type TYPE\n");
    printf("  -T, --print-type        print file system type\n");
    printf("  -x, --exclude-type=TYPE limit listing to file systems not of type TYPE\n");
    printf("  -v                      (ignored)\n");
    printf("      --help              display this help and exit\n");
    printf("      --version           output version information and exit\n");
    printf("\nFIELD_LIST is a comma-separated list of columns to be included:\n");
    printf("  source fstype size used avail pcent itotal iused iavail ipcent target file\n");
    printf("\nSIZE is an integer and optional unit (e.g. 10M is 10*1024*1024):\n");
    printf("  K,M,G,T (powers of 1024)\n");
    exit(EXIT_SUCCESS);
}

static void version_msg(void) {
    printf("%s version %s\n", PROGRAM, VERSION);
    printf("A from-scratch implementation of the GNU df command.\n");
    exit(EXIT_SUCCESS);
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    static struct option long_opts[] = {
        {"all",            no_argument,       0, 'a'},
        {"block-size",     required_argument, 0, 'B'},
        {"human-readable", no_argument,       0, 'h'},
        {"si",             no_argument,       0, 'H'},
        {"inodes",         no_argument,       0, 'i'},
        {"local",          no_argument,       0, 'l'},
        {"portability",    no_argument,       0, 'P'},
        {"print-type",     no_argument,       0, 'T'},
        {"sync",           no_argument,       0,  1 },
        {"no-sync",        no_argument,       0,  2 },
        {"total",          no_argument,       0,  3 },
        {"output",         optional_argument, 0,  4 },
        {"type",           required_argument, 0, 't'},
        {"exclude-type",   required_argument, 0, 'x'},
        {"help",           no_argument,       0,  5 },
        {"version",        no_argument,       0,  6 },
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "aB:hHiklmPTt:vx:", long_opts, NULL)) != -1) {
        switch (c) {
        case 'a': opt_all         = true;                          break;
        case 'B': opt_block_size  = parse_block_size(optarg);      break;
        case 'h': opt_human       = true;                          break;
        case 'H': opt_si          = true;                          break;
        case 'i': opt_inodes      = true;                          break;
        case 'k': opt_block_size  = 1024;                          break;
        case 'l': opt_local       = true;                          break;
        case 'm': opt_block_size  = 1024 * 1024;                   break;
        case 'P': opt_portability = true; opt_block_size = 512;    break;
        case 'T': opt_print_type  = true;                          break;
        case 't':
            if (include_type_count < MAX_TYPES)
                include_types[include_type_count++] = optarg;
            break;
        case 'x':
            if (exclude_type_count < MAX_TYPES)
                exclude_types[exclude_type_count++] = optarg;
            break;
        case 'v': break; /* ignored for compatibility */
        case  1:  opt_sync        = true;                          break;
        case  2:  opt_sync        = false;                         break;
        case  3:  opt_total       = true;                          break;
        case  4:
            opt_output = true;
            if (optarg) parse_output_fields(optarg);
            else {
                /* all fields */
                for (int i = 0; i < FIELD_COUNT; i++)
                    output_fields[output_field_count++] = (Field)i;
            }
            break;
        case  5:  usage_msg(EXIT_SUCCESS);                         break;
        case  6:  version_msg();                                   break;
        case '?': usage_msg(EXIT_FAILURE);                         break;
        }
    }

    if (opt_sync) sync();

    /* Totals accumulator */
    FSEntry total_entry;
    memset(&total_entry, 0, sizeof(total_entry));
    strncpy(total_entry.source, "total", sizeof(total_entry.source) - 1);
    strncpy(total_entry.target, "-",     sizeof(total_entry.target) - 1);
    strncpy(total_entry.fstype, "-",     sizeof(total_entry.fstype) - 1);
    total_entry.valid = true;

    bool show_type = opt_print_type || opt_output;

    /* Print header */
    if (!opt_output) {
        if (opt_portability)        print_header_posix();
        else if (opt_inodes)        print_header_inodes(show_type);
        else                        print_header_default(show_type);
    } else {
        print_header_output();
    }

    int exit_status = EXIT_SUCCESS;

    /* --- Mode 1: specific files given on command line --- */
    if (optind < argc) {
        for (int i = optind; i < argc; i++) {
            const char *file = argv[i];
            struct stat st;
            if (stat(file, &st) != 0) {
                fprintf(stderr, "%s: %s: %s\n", PROGRAM, file, strerror(errno));
                exit_status = EXIT_FAILURE;
                continue;
            }

            /* Find which mounted fs this file lives on */
            FILE *fp = setmntent("/proc/mounts", "r");
            if (!fp) { perror("setmntent"); exit(EXIT_FAILURE); }

            struct mntent *me;
            char best_target[PATH_MAX] = "";
            char best_source[256]      = "";
            char best_fstype[64]       = "";

            while ((me = getmntent(fp)) != NULL) {
                /* Find longest matching mount point */
                size_t tlen = strlen(me->mnt_dir);
                if (strncmp(file, me->mnt_dir, tlen) == 0 &&
                    (file[tlen] == '/' || file[tlen] == '\0' || tlen == 1)) {
                    if (tlen > strlen(best_target)) {
                        strncpy(best_target, me->mnt_dir,  sizeof(best_target) - 1);
                        strncpy(best_source, me->mnt_fsname, sizeof(best_source) - 1);
                        strncpy(best_fstype, me->mnt_type,  sizeof(best_fstype) - 1);
                    }
                }
            }
            endmntent(fp);

            if (best_target[0] == '\0') {
                fprintf(stderr, "%s: %s: can't find mount point\n", PROGRAM, file);
                exit_status = EXIT_FAILURE;
                continue;
            }

            FSEntry e = make_entry(best_source, best_target, best_fstype, file);
            if (!e.valid) {
                fprintf(stderr, "%s: %s: %s\n", PROGRAM, file, strerror(errno));
                exit_status = EXIT_FAILURE;
                continue;
            }

            print_entry(&e, show_type);

            if (opt_total) {
                total_entry.blk_total += e.blk_total;
                total_entry.blk_used  += e.blk_used;
                total_entry.blk_avail += e.blk_avail;
                total_entry.ino_total += e.ino_total;
                total_entry.ino_used  += e.ino_used;
                total_entry.ino_avail += e.ino_avail;
            }
        }
    } else {
        /* --- Mode 2: scan /proc/mounts --- */
        FILE *fp = setmntent("/proc/mounts", "r");
        if (!fp) {
            /* fallback to /etc/mtab */
            fp = setmntent("/etc/mtab", "r");
            if (!fp) { perror("setmntent"); exit(EXIT_FAILURE); }
        }

        /* Track seen device numbers to skip duplicates */
        dev_t seen_devs[4096];
        int   seen_count = 0;

        struct mntent *me;
        while ((me = getmntent(fp)) != NULL) {
            const char *src    = me->mnt_fsname;
            const char *target = me->mnt_dir;
            const char *fstype = me->mnt_type;

            /* Type filters */
            if (include_type_count > 0 && !type_in_list(fstype, include_types, include_type_count))
                continue;
            if (exclude_type_count > 0 &&  type_in_list(fstype, exclude_types, exclude_type_count))
                continue;

            /* Local filter */
            if (opt_local && !is_local_fs(src))
                continue;

            FSEntry e = make_entry(src, target, fstype, NULL);

            /* Skip dummy/pseudo filesystems unless -a */
            if (!opt_all && (e.is_dummy || is_dummy_fs(fstype)))
                continue;

            if (!e.valid) continue;

            /* Deduplicate by device number (same block dev mounted multiple times) */
            if (!opt_all) {
                struct stat st;
                if (stat(target, &st) == 0) {
                    bool dup = false;
                    for (int j = 0; j < seen_count; j++) {
                        if (seen_devs[j] == st.st_dev) { dup = true; break; }
                    }
                    if (dup) continue;
                    if (seen_count < 4096)
                        seen_devs[seen_count++] = st.st_dev;
                }
            }

            print_entry(&e, show_type);

            if (opt_total) {
                total_entry.blk_total += e.blk_total;
                total_entry.blk_used  += e.blk_used;
                total_entry.blk_avail += e.blk_avail;
                total_entry.ino_total += e.ino_total;
                total_entry.ino_used  += e.ino_used;
                total_entry.ino_avail += e.ino_avail;
            }
        }
        endmntent(fp);
    }

    if (opt_total)
        print_entry(&total_entry, show_type);

    return exit_status;
}
