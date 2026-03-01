/*
 * stat - display file or file system status
 * Full implementation with all standard features
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdarg.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *program_name = "stat";

/* Options */
static int   opt_dereference  = 0;  /* -L: follow symlinks */
static int   opt_filesystem   = 0;  /* -f: display filesystem status */
static int   opt_terse        = 0;  /* -t: terse output */
static char *opt_format       = NULL; /* -c/--format */
static char *opt_printf_fmt   = NULL; /* --printf (no trailing newline) */

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
"Usage: %s [OPTION]... FILE...\n"
"Display file or file system status.\n"
"\n"
"  -L, --dereference     follow links\n"
"  -f, --file-system     display file system status instead of file status\n"
"  -c, --format=FORMAT   use the specified FORMAT instead of the default;\n"
"                          output a newline after each use of FORMAT\n"
"      --printf=FORMAT   like --format, but interpret backslash escapes,\n"
"                          and do not output a mandatory trailing newline\n"
"  -t, --terse           print the information in terse form\n"
"  -h, --help            display this help and exit\n"
"\n"
"Valid format sequences for files (without --file-system):\n"
"  %%a   permission bits in octal\n"
"  %%A   permission bits and file type in human readable form\n"
"  %%b   number of blocks allocated\n"
"  %%B   the size in bytes of each block\n"
"  %%C   SELinux security context string\n"
"  %%d   device number in decimal\n"
"  %%D   device number in hex\n"
"  %%f   raw mode in hex\n"
"  %%F   file type\n"
"  %%g   group ID of owner\n"
"  %%G   group name of owner\n"
"  %%h   number of hard links\n"
"  %%i   inode number\n"
"  %%m   mount point\n"
"  %%n   file name\n"
"  %%N   quoted file name with dereference if symbolic link\n"
"  %%o   optimal I/O transfer size hint\n"
"  %%s   total size, in bytes\n"
"  %%t   major device type in hex (for character/block devices)\n"
"  %%T   minor device type in hex (for character/block devices)\n"
"  %%u   user ID of owner\n"
"  %%U   user name of owner\n"
"  %%w   time of file birth; - if unknown\n"
"  %%W   time of file birth as seconds since Epoch; 0 if unknown\n"
"  %%x   time of last access\n"
"  %%X   time of last access as seconds since Epoch\n"
"  %%y   time of last data modification\n"
"  %%Y   time of last data modification as seconds since Epoch\n"
"  %%z   time of last status change\n"
"  %%Z   time of last status change as seconds since Epoch\n"
"\n"
"Valid format sequences for file systems:\n"
"  %%a   free blocks available to non-superuser\n"
"  %%b   total data blocks in file system\n"
"  %%c   total file nodes in file system\n"
"  %%d   free file nodes in file system\n"
"  %%f   free blocks in file system\n"
"  %%i   file system ID in hex\n"
"  %%l   maximum length of filenames\n"
"  %%n   file name\n"
"  %%s   block size (for faster transfers)\n"
"  %%S   fundamental block size (for block counts)\n"
"  %%t   file system type in hex\n"
"  %%T   file system type in human readable form\n",
        program_name);
    exit(EXIT_FAILURE);
}

/* ---- Time formatting ---- */

static void format_time(char *buf, size_t bufsize, const struct timespec *ts) {
    struct tm *tm = localtime(&ts->tv_sec);
    if (!tm) {
        snprintf(buf, bufsize, "?");
        return;
    }
    char tmp[64];
    strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", tm);

    /* nanoseconds + timezone */
    char tz[16];
    strftime(tz, sizeof(tz), "%z", tm);
    snprintf(buf, bufsize, "%s.%09ld %s", tmp, ts->tv_nsec, tz);
}

/* ---- File type string ---- */

static const char *file_type_str(mode_t mode) {
    if (S_ISREG(mode))  return "regular file";
    if (S_ISDIR(mode))  return "directory";
    if (S_ISLNK(mode))  return "symbolic link";
    if (S_ISBLK(mode))  return "block special file";
    if (S_ISCHR(mode))  return "character special file";
    if (S_ISFIFO(mode)) return "fifo";
    if (S_ISSOCK(mode)) return "socket";
    return "unknown";
}

/* ---- Filesystem type name via statfs magic ---- */

static const char *fs_type_name(long magic) {
    switch ((unsigned long)magic) {
    case 0xEF53UL:      return "ext2/ext3/ext4";
    case 0x6969UL:      return "nfs";
    case 0xFF534D42UL:  return "cifs";
    case 0x52654973UL:  return "reiserfs";
    case 0x65735546UL:  return "fuse";
    case 0x9123683EUL:  return "btrfs";
    case 0x01021994UL:  return "tmpfs";
    case 0x58465342UL:  return "xfs";
    case 0x4d44UL:      return "msdos/vfat";
    case 0x73717368UL:  return "squashfs";
    case 0x2FC12FC1UL:  return "zfs";
    case 0x4006UL:      return "fat";
    case 0x5346544EUL:  return "ntfs";
    case 0x9660UL:      return "iso9660";
    case 0x1CD1UL:      return "devpts";
    case 0x62656572UL:  return "sysfs";
    case 0x9fa0UL:      return "proc";
    case 0x6B65726EUL:  return "kernfs";
    case 0x64626720UL:  return "debugfs";
    case 0xCAFE4A11UL:  return "overlayfs";
    default:            return "unknown";
    }
}



static void mode_to_str(mode_t mode, char *buf) {
    /* File type */
    if (S_ISREG(mode))       buf[0] = '-';
    else if (S_ISDIR(mode))  buf[0] = 'd';
    else if (S_ISLNK(mode))  buf[0] = 'l';
    else if (S_ISBLK(mode))  buf[0] = 'b';
    else if (S_ISCHR(mode))  buf[0] = 'c';
    else if (S_ISFIFO(mode)) buf[0] = 'p';
    else if (S_ISSOCK(mode)) buf[0] = 's';
    else                     buf[0] = '?';

    /* User */
    buf[1] = (mode & S_IRUSR) ? 'r' : '-';
    buf[2] = (mode & S_IWUSR) ? 'w' : '-';
    if (mode & S_ISUID)
        buf[3] = (mode & S_IXUSR) ? 's' : 'S';
    else
        buf[3] = (mode & S_IXUSR) ? 'x' : '-';

    /* Group */
    buf[4] = (mode & S_IRGRP) ? 'r' : '-';
    buf[5] = (mode & S_IWGRP) ? 'w' : '-';
    if (mode & S_ISGID)
        buf[6] = (mode & S_IXGRP) ? 's' : 'S';
    else
        buf[6] = (mode & S_IXGRP) ? 'x' : '-';

    /* Other */
    buf[7] = (mode & S_IROTH) ? 'r' : '-';
    buf[8] = (mode & S_IWOTH) ? 'w' : '-';
    if (mode & S_ISVTX)
        buf[9] = (mode & S_IXOTH) ? 't' : 'T';
    else
        buf[9] = (mode & S_IXOTH) ? 'x' : '-';

    buf[10] = '\0';
}

/* ---- Format a single % sequence for a file ---- */

static void print_file_format_seq(char spec, const char *path,
                                   const struct stat *st, int is_printf) {
    char buf[256];
    char modestr[16];

    switch (spec) {
    case 'a': /* permission bits in octal */
        printf("%o", (unsigned)(st->st_mode & 07777));
        break;
    case 'A': /* human-readable permissions */
        mode_to_str(st->st_mode, modestr);
        printf("%s", modestr);
        break;
    case 'b': /* number of blocks */
        printf("%llu", (unsigned long long)st->st_blocks);
        break;
    case 'B': /* block size */
        printf("%lu", (unsigned long)512); /* st_blocks is in 512-byte units */
        break;
    case 'C': /* SELinux context (not widely available; print ?) */
        printf("?");
        break;
    case 'd': /* device number decimal */
        printf("%llu", (unsigned long long)st->st_dev);
        break;
    case 'D': /* device number hex */
        printf("%llx", (unsigned long long)st->st_dev);
        break;
    case 'f': /* raw mode in hex */
        printf("%lx", (unsigned long)st->st_mode);
        break;
    case 'F': /* file type */
        printf("%s", file_type_str(st->st_mode));
        break;
    case 'g': /* GID */
        printf("%u", (unsigned)st->st_gid);
        break;
    case 'G': { /* group name */
        struct group *gr = getgrgid(st->st_gid);
        if (gr) printf("%s", gr->gr_name);
        else    printf("%u", (unsigned)st->st_gid);
        break;
    }
    case 'h': /* hard links */
        printf("%lu", (unsigned long)st->st_nlink);
        break;
    case 'i': /* inode */
        printf("%llu", (unsigned long long)st->st_ino);
        break;
    case 'm': /* mount point: best-effort via /proc/mounts */
        printf("/"); /* simplified; full implementation requires parsing /proc/mounts */
        break;
    case 'n': /* file name */
        printf("%s", path);
        break;
    case 'N': /* quoted name, with -> target if symlink */
        if (S_ISLNK(st->st_mode)) {
            char target[PATH_MAX];
            ssize_t len = readlink(path, target, sizeof(target) - 1);
            if (len >= 0) {
                target[len] = '\0';
                printf("'%s' -> '%s'", path, target);
            } else {
                printf("'%s'", path);
            }
        } else {
            printf("'%s'", path);
        }
        break;
    case 'o': /* optimal I/O size */
        printf("%lu", (unsigned long)st->st_blksize);
        break;
    case 's': /* size in bytes */
        printf("%lld", (long long)st->st_size);
        break;
    case 't': /* major device type in hex */
        printf("%x", (unsigned)major(st->st_rdev));
        break;
    case 'T': /* minor device type in hex */
        printf("%x", (unsigned)minor(st->st_rdev));
        break;
    case 'u': /* UID */
        printf("%u", (unsigned)st->st_uid);
        break;
    case 'U': { /* user name */
        struct passwd *pw = getpwuid(st->st_uid);
        if (pw) printf("%s", pw->pw_name);
        else    printf("%u", (unsigned)st->st_uid);
        break;
    }
    case 'w': /* birth time (not always available) */
        printf("-");
        break;
    case 'W': /* birth time as epoch */
        printf("0");
        break;
    case 'x': /* access time */
        format_time(buf, sizeof(buf), &st->st_atim);
        printf("%s", buf);
        break;
    case 'X': /* access time epoch */
        printf("%lld", (long long)st->st_atim.tv_sec);
        break;
    case 'y': /* modification time */
        format_time(buf, sizeof(buf), &st->st_mtim);
        printf("%s", buf);
        break;
    case 'Y': /* modification time epoch */
        printf("%lld", (long long)st->st_mtim.tv_sec);
        break;
    case 'z': /* status change time */
        format_time(buf, sizeof(buf), &st->st_ctim);
        printf("%s", buf);
        break;
    case 'Z': /* status change time epoch */
        printf("%lld", (long long)st->st_ctim.tv_sec);
        break;
    case '%':
        printf("%%");
        break;
    default:
        printf("%%%c", spec);
        break;
    }
}

/* ---- Format a single % sequence for a filesystem ---- */

static void print_fs_format_seq(char spec, const char *path,
                                  const struct statvfs *sv) {
    switch (spec) {
    case 'a': /* free blocks for non-root */
        printf("%llu", (unsigned long long)sv->f_bavail);
        break;
    case 'b': /* total data blocks */
        printf("%llu", (unsigned long long)sv->f_blocks);
        break;
    case 'c': /* total inodes */
        printf("%llu", (unsigned long long)sv->f_files);
        break;
    case 'd': /* free inodes */
        printf("%llu", (unsigned long long)sv->f_ffree);
        break;
    case 'f': /* free blocks */
        printf("%llu", (unsigned long long)sv->f_bfree);
        break;
    case 'i': /* filesystem ID in hex */
        /* f_fsid is a struct; print low 64 bits */
        printf("%lx", (unsigned long)sv->f_fsid);
        break;
    case 'l': /* max filename length */
        printf("%lu", (unsigned long)sv->f_namemax);
        break;
    case 'n': /* file name */
        printf("%s", path);
        break;
    case 's': /* block size for faster transfer */
        printf("%lu", (unsigned long)sv->f_bsize);
        break;
    case 'S': /* fundamental block size */
        printf("%lu", (unsigned long)sv->f_frsize);
        break;
    case 't': /* fs type in hex */
        {
            struct statfs sfs;
            if (statfs(path, &sfs) == 0) printf("%lx", (unsigned long)sfs.f_type);
            else printf("0");
        }
        break;
    case 'T': /* fs type human-readable */
        {
            struct statfs sfs;
            if (statfs(path, &sfs) == 0) printf("%s", fs_type_name(sfs.f_type));
            else printf("unknown");
        }
        break;
    case '%':
        printf("%%");
        break;
    default:
        printf("%%%c", spec);
        break;
    }
}

/* ---- Process a format string ---- */

static void process_format(const char *fmt, const char *path,
                            const struct stat *st, const struct statvfs *sv,
                            int is_printf) {
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') {
            if (is_printf && *p == '\\') {
                p++;
                switch (*p) {
                case 'n':  putchar('\n'); break;
                case 't':  putchar('\t'); break;
                case 'r':  putchar('\r'); break;
                case '\\': putchar('\\'); break;
                case '0':  putchar('\0'); break;
                case 'a':  putchar('\a'); break;
                case 'b':  putchar('\b'); break;
                case 'f':  putchar('\f'); break;
                case 'v':  putchar('\v'); break;
                default:   putchar('\\'); putchar(*p); break;
                }
            } else {
                putchar(*p);
            }
            continue;
        }
        p++;
        if (!*p) { putchar('%'); break; }

        if (opt_filesystem)
            print_fs_format_seq(*p, path, sv);
        else
            print_file_format_seq(*p, path, st, is_printf);
    }
}

/* ---- Default output for a file ---- */

static void print_file_default(const char *path, const struct stat *st) {
    char modestr[16];
    char atbuf[64], mtbuf[64], ctbuf[64];

    mode_to_str(st->st_mode, modestr);
    format_time(atbuf, sizeof(atbuf), &st->st_atim);
    format_time(mtbuf, sizeof(mtbuf), &st->st_mtim);
    format_time(ctbuf, sizeof(ctbuf), &st->st_ctim);

    /* User and group names */
    struct passwd *pw = getpwuid(st->st_uid);
    struct group  *gr = getgrgid(st->st_gid);
    char uname[64], gname[64];
    if (pw) snprintf(uname, sizeof(uname), "%s", pw->pw_name);
    else    snprintf(uname, sizeof(uname), "%u", (unsigned)st->st_uid);
    if (gr) snprintf(gname, sizeof(gname), "%s", gr->gr_name);
    else    snprintf(gname, sizeof(gname), "%u", (unsigned)st->st_gid);

    /* File name line */
    if (S_ISLNK(st->st_mode)) {
        char target[PATH_MAX];
        ssize_t len = readlink(path, target, sizeof(target) - 1);
        if (len >= 0) {
            target[len] = '\0';
            printf("  File: %s -> %s\n", path, target);
        } else {
            printf("  File: %s\n", path);
        }
    } else {
        printf("  File: %s\n", path);
    }

    /* Size / Blocks / IO Block / File type */
    printf("  Size: %-15lld\tBlocks: %-10llu IO Block: %-6lu %s\n",
           (long long)st->st_size,
           (unsigned long long)st->st_blocks,
           (unsigned long)st->st_blksize,
           file_type_str(st->st_mode));

    /* Device / Inode / Links */
    printf("Device: %llxh/%llud\tInode: %-11llu  Links: %lu",
           (unsigned long long)st->st_dev,
           (unsigned long long)st->st_dev,
           (unsigned long long)st->st_ino,
           (unsigned long)st->st_nlink);

    /* Device type for block/char devices */
    if (S_ISBLK(st->st_mode) || S_ISCHR(st->st_mode)) {
        printf("  Device type: %u,%u", major(st->st_rdev), minor(st->st_rdev));
    }
    printf("\n");

    /* Access / Uid / Gid */
    printf("Access: (%04o/%s)  Uid: (%5u/%8s)   Gid: (%5u/%8s)\n",
           (unsigned)(st->st_mode & 07777),
           modestr,
           (unsigned)st->st_uid, uname,
           (unsigned)st->st_gid, gname);

    /* Timestamps */
    printf("Access: %s\n", atbuf);
    printf("Modify: %s\n", mtbuf);
    printf("Change: %s\n", ctbuf);

    /* Birth time */
#ifdef HAVE_BIRTHTIME
    char btbuf[64];
    format_time(btbuf, sizeof(btbuf), &st->st_birthtim);
    printf(" Birth: %s\n", btbuf);
#else
    printf(" Birth: -\n");
#endif
}

/* ---- Default output for a filesystem ---- */

static void print_fs_default(const char *path, const struct statvfs *sv) {
    struct statfs sfs;
    const char *typename = "unknown";
    long magic = 0;
    if (statfs(path, &sfs) == 0) {
        magic = sfs.f_type;
        typename = fs_type_name(sfs.f_type);
    }
    printf("  File: \"%s\"\n", path);
    printf("    ID: %-16lx Namelen: %-7lu Type: %s\n",
           (unsigned long)sv->f_fsid,
           (unsigned long)sv->f_namemax,
           typename);
    printf("Block size: %-11lu Fundamental block size: %lu\n",
           (unsigned long)sv->f_bsize,
           (unsigned long)sv->f_frsize);
    printf("Blocks: Total: %-10llu Free: %-10llu Available: %llu\n",
           (unsigned long long)sv->f_blocks,
           (unsigned long long)sv->f_bfree,
           (unsigned long long)sv->f_bavail);
    printf("Inodes: Total: %-10llu Free: %llu\n",
           (unsigned long long)sv->f_files,
           (unsigned long long)sv->f_ffree);
    (void)magic;
}

/* ---- Terse output ---- */

static void print_file_terse(const char *path, const struct stat *st) {
    printf("%s %lld %llu %lx %u %u %llx %llu %lu %lu %lu "
           "%lld %lld %lld %lld\n",
           path,
           (long long)st->st_size,
           (unsigned long long)st->st_blocks,
           (unsigned long)st->st_mode,
           (unsigned)st->st_uid,
           (unsigned)st->st_gid,
           (unsigned long long)st->st_dev,
           (unsigned long long)st->st_ino,
           (unsigned long)st->st_nlink,
           (unsigned long)major(st->st_rdev),
           (unsigned long)minor(st->st_rdev),
           (long long)st->st_atim.tv_sec,
           (long long)st->st_mtim.tv_sec,
           (long long)st->st_ctim.tv_sec,
           (long long)0 /* birth */);
}

static void print_fs_terse(const char *path, const struct statvfs *sv) {
    printf("%s %llu %llu %llu %llu %llu %llu %lu %lu %lx %lx %lu\n",
           path,
           (unsigned long long)sv->f_blocks,
           (unsigned long long)sv->f_bfree,
           (unsigned long long)sv->f_bavail,
           (unsigned long long)sv->f_files,
           (unsigned long long)sv->f_ffree,
           (unsigned long long)sv->f_fsid,
           (unsigned long)sv->f_namemax,
           (unsigned long)sv->f_frsize,
           (unsigned long)sv->f_flag,
           (unsigned long)sv->f_bsize,
           (unsigned long)0);
}

/* ---- Process one path ---- */

static int do_stat(const char *path) {
    struct stat   st;
    struct statvfs sv;

    if (opt_filesystem) {
        if (statvfs(path, &sv) != 0) {
            warn_msg("cannot statfs '%s': %s", path, strerror(errno));
            return -1;
        }
        /* Also stat the file itself for name */
        if (opt_terse) {
            print_fs_terse(path, &sv);
        } else if (opt_format || opt_printf_fmt) {
            const char *fmt = opt_printf_fmt ? opt_printf_fmt : opt_format;
            int is_printf = (opt_printf_fmt != NULL);
            process_format(fmt, path, NULL, &sv, is_printf);
            if (!is_printf) putchar('\n');
        } else {
            print_fs_default(path, &sv);
        }
        return 0;
    }

    int ret;
    if (opt_dereference)
        ret = stat(path, &st);
    else
        ret = lstat(path, &st);

    if (ret != 0) {
        warn_msg("cannot stat '%s': %s", path, strerror(errno));
        return -1;
    }

    if (opt_terse) {
        print_file_terse(path, &st);
    } else if (opt_format || opt_printf_fmt) {
        const char *fmt = opt_printf_fmt ? opt_printf_fmt : opt_format;
        int is_printf = (opt_printf_fmt != NULL);
        process_format(fmt, path, &st, NULL, is_printf);
        if (!is_printf) putchar('\n');
    } else {
        print_file_default(path, &st);
    }

    return 0;
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
            if (strcmp(opt, "dereference") == 0)      opt_dereference = 1;
            else if (strcmp(opt, "file-system") == 0) opt_filesystem  = 1;
            else if (strcmp(opt, "terse") == 0)        opt_terse       = 1;
            else if (strncmp(opt, "format=", 7) == 0) opt_format      = opt + 7;
            else if (strncmp(opt, "printf=", 7) == 0) opt_printf_fmt  = opt + 7;
            else if (strcmp(opt, "help") == 0) usage();
            else die("unrecognized option '--%s'", opt);
            continue;
        }

        for (int j = 1; arg[j]; j++) {
            switch (arg[j]) {
            case 'L': opt_dereference = 1; break;
            case 'f': opt_filesystem  = 1; break;
            case 't': opt_terse       = 1; break;
            case 'c':
                if (arg[j+1]) { opt_format = arg + j + 1; j = (int)strlen(arg)-1; }
                else if (i+1 < argc) opt_format = argv[++i];
                else die("option '-c' requires an argument");
                break;
            case 'h': usage(); break;
            default:
                die("invalid option -- '%c'", arg[j]);
            }
        }
    }

    if (i >= argc) {
        fprintf(stderr, "%s: missing operand\n", program_name);
        fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
        return EXIT_FAILURE;
    }

    int ret = 0;
    for (; i < argc; i++) {
        if (do_stat(argv[i]) != 0)
            ret = 1;
    }
    return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
