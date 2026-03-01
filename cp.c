/*
 * cp - copy files and directories
 * Full implementation with all standard features
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <utime.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include <libgen.h>
#include <ftw.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Options */
static int opt_recursive      = 0;  /* -r, -R */
static int opt_force          = 0;  /* -f */
static int opt_interactive    = 0;  /* -i */
static int opt_noclobber      = 0;  /* -n */
static int opt_preserve       = 0;  /* -p: mode,ownership,timestamps */
static int opt_preserve_all   = 0;  /* -a: same as -dRp */
static int opt_no_dereference = 0;  /* -d / -P: don't follow symlinks */
static int opt_dereference    = 0;  /* -L: always follow symlinks */
static int opt_verbose        = 0;  /* -v */
static int opt_update         = 0;  /* -u */
static int opt_strip_trailing = 0;  /* --strip-trailing-slashes */
static int opt_one_fs         = 0;  /* -x */
static int opt_backup         = 0;  /* --backup / -b */
static char *opt_suffix       = NULL; /* --suffix, -S */
static int opt_target_dir     = 0;  /* -t */
static char *target_dir_arg   = NULL;
static int opt_no_target_dir  = 0;  /* -T */

static const char *program_name = "cp";
static dev_t src_dev; /* for -x: restrict to same filesystem */

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

static char *xstrdup(const char *s) {
    char *r = strdup(s);
    if (!r) die("out of memory");
    return r;
}

static void *xmalloc(size_t n) {
    void *r = malloc(n);
    if (!r) die("out of memory");
    return r;
}

/* Build a path: dir + "/" + name */
static char *path_join(const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    char *result = xmalloc(dlen + nlen + 2);
    memcpy(result, dir, dlen);
    if (dlen > 0 && dir[dlen-1] != '/')
        result[dlen++] = '/';
    memcpy(result + dlen, name, nlen + 1);
    return result;
}

/* Strip trailing slashes */
static char *strip_trailing_slashes(const char *path) {
    char *s = xstrdup(path);
    size_t len = strlen(s);
    while (len > 1 && s[len-1] == '/')
        s[--len] = '\0';
    return s;
}

/* Ask the user a yes/no question */
static int ask_yesno(const char *question) {
    fprintf(stderr, "%s", question);
    fflush(stderr);
    int c = getchar();
    int ret = (c == 'y' || c == 'Y');
    while (c != '\n' && c != EOF) c = getchar();
    return ret;
}

/* Backup suffix */
static const char *get_suffix(void) {
    if (opt_suffix) return opt_suffix;
    const char *env = getenv("SIMPLE_BACKUP_SUFFIX");
    if (env) return env;
    return "~";
}

/* Make a backup of the destination */
static int make_backup(const char *dest) {
    const char *suf = get_suffix();
    char *bak = xmalloc(strlen(dest) + strlen(suf) + 1);
    sprintf(bak, "%s%s", dest, suf);
    if (rename(dest, bak) != 0) {
        warn("cannot backup '%s' to '%s': %s", dest, bak, strerror(errno));
        free(bak);
        return -1;
    }
    free(bak);
    return 0;
}

/* ---- Core copy routines ---- */

/* Copy a regular file src_fd -> dst_fd */
static int copy_data(int src_fd, int dst_fd, const char *src, const char *dst) {
    char buf[65536];
    ssize_t nr, nw;
    while ((nr = read(src_fd, buf, sizeof(buf))) > 0) {
        char *p = buf;
        ssize_t rem = nr;
        while (rem > 0) {
            nw = write(dst_fd, p, rem);
            if (nw < 0) {
                warn("error writing '%s': %s", dst, strerror(errno));
                return -1;
            }
            p   += nw;
            rem -= nw;
        }
    }
    if (nr < 0) {
        warn("error reading '%s': %s", src, strerror(errno));
        return -1;
    }
    return 0;
}

/* Preserve timestamps */
static void preserve_timestamps(const struct stat *st, const char *dst) {
    struct timeval tv[2];
    tv[0].tv_sec  = st->st_atim.tv_sec;
    tv[0].tv_usec = st->st_atim.tv_nsec / 1000;
    tv[1].tv_sec  = st->st_mtim.tv_sec;
    tv[1].tv_usec = st->st_mtim.tv_nsec / 1000;
    /* Use lutimes if available to handle symlinks */
    lutimes(dst, tv);
}

/* Preserve ownership */
static void preserve_ownership(const struct stat *st, const char *dst) {
    if (lchown(dst, st->st_uid, st->st_gid) != 0) {
        /* Non-fatal: may not have permission */
    }
}

/* Preserve mode */
static void preserve_mode(const struct stat *st, const char *dst) {
    chmod(dst, st->st_mode & 07777);
}

/* Forward declaration */
static int copy_entry(const char *src, const char *dst);

/* Copy directory recursively */
static int copy_dir(const char *src, const char *dst, const struct stat *src_st) {
    /* Create destination directory */
    struct stat dst_st;
    int dst_exists = (lstat(dst, &dst_st) == 0);

    if (!dst_exists) {
        /* Create with same permissions as source (will be fixed later) */
        if (mkdir(dst, src_st->st_mode & 07777 | 0700) != 0) {
            warn("cannot create directory '%s': %s", dst, strerror(errno));
            return -1;
        }
    } else if (!S_ISDIR(dst_st.st_mode)) {
        warn("cannot overwrite non-directory '%s' with directory '%s'", dst, src);
        return -1;
    }

    DIR *dir = opendir(src);
    if (!dir) {
        warn("cannot open directory '%s': %s", src, strerror(errno));
        return -1;
    }

    int ret = 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char *s = path_join(src, ent->d_name);
        char *d = path_join(dst, ent->d_name);
        if (copy_entry(s, d) != 0)
            ret = -1;
        free(s);
        free(d);
    }
    closedir(dir);

    /* Fix up destination directory permissions/timestamps */
    if (opt_preserve || opt_preserve_all) {
        preserve_ownership(src_st, dst);
        preserve_mode(src_st, dst);
        preserve_timestamps(src_st, dst);
    } else {
        chmod(dst, src_st->st_mode & 07777);
    }

    return ret;
}

/* Copy one filesystem entry src -> dst */
static int copy_entry(const char *src, const char *dst) {
    struct stat src_st;

    /* Stat the source (follow symlinks unless -P/-d) */
    int stat_ret;
    if (opt_no_dereference && !opt_dereference)
        stat_ret = lstat(src, &src_st);
    else
        stat_ret = stat(src, &src_st);

    if (stat_ret != 0) {
        warn("cannot stat '%s': %s", src, strerror(errno));
        return -1;
    }

    /* -x: stay on same filesystem */
    if (opt_one_fs && src_st.st_dev != src_dev) {
        warn("skipping '%s' (different filesystem)", src);
        return 0;
    }

    /* Handle destination existence */
    struct stat dst_st;
    int dst_exists = (lstat(dst, &dst_st) == 0);

    if (dst_exists) {
        /* Refuse to copy file to itself */
        if (src_st.st_dev == dst_st.st_dev && src_st.st_ino == dst_st.st_ino) {
            warn("'%s' and '%s' are the same file", src, dst);
            return -1;
        }

        /* -n: no clobber */
        if (opt_noclobber)
            return 0;

        /* -u: update only if src is newer */
        if (opt_update) {
            if (src_st.st_mtim.tv_sec < dst_st.st_mtim.tv_sec ||
                (src_st.st_mtim.tv_sec == dst_st.st_mtim.tv_sec &&
                 src_st.st_mtim.tv_nsec <= dst_st.st_mtim.tv_nsec))
                return 0;
        }

        /* -i: interactive */
        if (opt_interactive && !opt_force) {
            char q[PATH_MAX + 64];
            snprintf(q, sizeof(q), "%s: overwrite '%s'? ", program_name, dst);
            if (!ask_yesno(q))
                return 0;
        }

        /* Backup */
        if (opt_backup)
            make_backup(dst);
    }

    /* Symlink handling */
    if (S_ISLNK(src_st.st_mode)) {
        char lnkbuf[PATH_MAX + 1];
        ssize_t len = readlink(src, lnkbuf, sizeof(lnkbuf) - 1);
        if (len < 0) {
            warn("cannot readlink '%s': %s", src, strerror(errno));
            return -1;
        }
        lnkbuf[len] = '\0';

        if (dst_exists) {
            if (unlink(dst) != 0 && !S_ISDIR(dst_st.st_mode)) {
                warn("cannot remove '%s': %s", dst, strerror(errno));
                return -1;
            }
        }
        if (symlink(lnkbuf, dst) != 0) {
            warn("cannot create symlink '%s': %s", dst, strerror(errno));
            return -1;
        }
        if (opt_preserve || opt_preserve_all) {
            preserve_ownership(&src_st, dst);
            preserve_timestamps(&src_st, dst);
        }
        if (opt_verbose)
            printf("'%s' -> '%s'\n", src, dst);
        return 0;
    }

    /* Directory */
    if (S_ISDIR(src_st.st_mode)) {
        if (!opt_recursive) {
            warn("-r not specified; omitting directory '%s'", src);
            return -1;
        }
        if (opt_verbose)
            printf("'%s' -> '%s'\n", src, dst);
        return copy_dir(src, dst, &src_st);
    }

    /* Special files: block/char device, fifo, socket */
    if (S_ISBLK(src_st.st_mode) || S_ISCHR(src_st.st_mode) ||
        S_ISFIFO(src_st.st_mode) || S_ISSOCK(src_st.st_mode)) {
        if (dst_exists) {
            if (unlink(dst) != 0) {
                warn("cannot remove '%s': %s", dst, strerror(errno));
                return -1;
            }
        }
        if (S_ISFIFO(src_st.st_mode)) {
            if (mkfifo(dst, src_st.st_mode & 07777) != 0) {
                warn("cannot create fifo '%s': %s", dst, strerror(errno));
                return -1;
            }
        } else {
            if (mknod(dst, src_st.st_mode & ~0777 | 0600, src_st.st_rdev) != 0) {
                warn("cannot create special file '%s': %s", dst, strerror(errno));
                return -1;
            }
        }
        if (opt_preserve || opt_preserve_all) {
            preserve_ownership(&src_st, dst);
            preserve_mode(&src_st, dst);
            preserve_timestamps(&src_st, dst);
        }
        if (opt_verbose)
            printf("'%s' -> '%s'\n", src, dst);
        return 0;
    }

    /* Regular file */
    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        warn("cannot open '%s': %s", src, strerror(errno));
        return -1;
    }

    int flags = O_WRONLY | O_CREAT | O_TRUNC;
    /* If dst is a symlink pointing somewhere, follow it unless -P */
    int dst_fd = open(dst, flags, src_st.st_mode & 07777);
    if (dst_fd < 0) {
        /* Try removing and retrying */
        if (dst_exists && (opt_force)) {
            unlink(dst);
            dst_fd = open(dst, flags, src_st.st_mode & 07777);
        }
        if (dst_fd < 0) {
            warn("cannot create '%s': %s", dst, strerror(errno));
            close(src_fd);
            return -1;
        }
    }

    int ret = copy_data(src_fd, dst_fd, src, dst);
    close(src_fd);
    close(dst_fd);

    if (ret != 0) {
        unlink(dst);
        return -1;
    }

    if (opt_preserve || opt_preserve_all) {
        preserve_ownership(&src_st, dst);
        preserve_mode(&src_st, dst);
        preserve_timestamps(&src_st, dst);
    }

    if (opt_verbose)
        printf("'%s' -> '%s'\n", src, dst);

    return 0;
}

/* ---- Argument parsing ---- */

static void usage(void) {
    fprintf(stderr,
"Usage: %s [OPTION]... [-T] SOURCE DEST\n"
"  or:  %s [OPTION]... SOURCE... DIRECTORY\n"
"  or:  %s [OPTION]... -t DIRECTORY SOURCE...\n"
"\n"
"Copy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n"
"\n"
"  -a, --archive                same as -dRp\n"
"  -b                           make a backup of each existing destination file\n"
"      --backup[=CONTROL]       make a backup of each existing destination file\n"
"  -d                           same as --no-dereference --preserve=links\n"
"  -f, --force                  if an existing destination file cannot be\n"
"                                 opened, remove it and try again\n"
"  -i, --interactive            prompt before overwrite (overrides -n)\n"
"  -l, --link                   hard link files instead of copying\n"
"  -L, --dereference            always follow symbolic links in SOURCE\n"
"  -n, --no-clobber             do not overwrite an existing file\n"
"  -P, --no-dereference         never follow symbolic links in SOURCE\n"
"  -p                           same as --preserve=mode,ownership,timestamps\n"
"      --preserve[=ATTR_LIST]   preserve the specified attributes\n"
"  -R, -r, --recursive          copy directories recursively\n"
"  -s, --symbolic-link          make symbolic links instead of copying\n"
"  -S, --suffix=SUFFIX          override the usual backup suffix\n"
"  -t, --target-directory=DIRECTORY  copy all SOURCE arguments into DIRECTORY\n"
"  -T, --no-target-directory    treat DEST as a normal file\n"
"  -u, --update                 copy only when SOURCE is newer than DEST\n"
"  -v, --verbose                explain what is being done\n"
"  -x, --one-file-system        stay on this file system\n"
"      --strip-trailing-slashes remove any trailing slashes from each SOURCE\n"
"  -h, --help                   display this help and exit\n",
        program_name, program_name, program_name);
    exit(EXIT_FAILURE);
}

static int opt_hardlink   = 0; /* -l */
static int opt_symlink    = 0; /* -s */

static void parse_args(int argc, char **argv, int *optind_out) {
    int i;
    for (i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] != '-' || strcmp(arg, "-") == 0)
            break;
        if (strcmp(arg, "--") == 0) { i++; break; }

        /* Long options */
        if (strncmp(arg, "--", 2) == 0) {
            char *opt = arg + 2;
            if (strcmp(opt, "archive") == 0) {
                opt_preserve_all = opt_recursive = opt_no_dereference = 1;
            } else if (strcmp(opt, "backup") == 0 || strncmp(opt, "backup=", 7) == 0) {
                opt_backup = 1;
            } else if (strcmp(opt, "force") == 0) {
                opt_force = 1;
            } else if (strcmp(opt, "interactive") == 0) {
                opt_interactive = 1;
            } else if (strcmp(opt, "link") == 0) {
                opt_hardlink = 1;
            } else if (strcmp(opt, "dereference") == 0) {
                opt_dereference = 1; opt_no_dereference = 0;
            } else if (strcmp(opt, "no-clobber") == 0) {
                opt_noclobber = 1;
            } else if (strcmp(opt, "no-dereference") == 0) {
                opt_no_dereference = 1; opt_dereference = 0;
            } else if (strcmp(opt, "preserve") == 0) {
                opt_preserve = 1;
            } else if (strncmp(opt, "preserve=", 9) == 0) {
                opt_preserve = 1; /* simplified: treat all attrs */
            } else if (strcmp(opt, "recursive") == 0) {
                opt_recursive = 1;
            } else if (strcmp(opt, "symbolic-link") == 0) {
                opt_symlink = 1;
            } else if (strncmp(opt, "suffix=", 7) == 0) {
                opt_suffix = opt + 7;
            } else if (strncmp(opt, "target-directory=", 17) == 0) {
                opt_target_dir = 1;
                target_dir_arg = opt + 17;
            } else if (strcmp(opt, "no-target-directory") == 0) {
                opt_no_target_dir = 1;
            } else if (strcmp(opt, "update") == 0) {
                opt_update = 1;
            } else if (strcmp(opt, "verbose") == 0) {
                opt_verbose = 1;
            } else if (strcmp(opt, "one-file-system") == 0) {
                opt_one_fs = 1;
            } else if (strcmp(opt, "strip-trailing-slashes") == 0) {
                opt_strip_trailing = 1;
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
            case 'a': opt_preserve_all = opt_recursive = opt_no_dereference = 1; break;
            case 'b': opt_backup = 1; break;
            case 'd': opt_no_dereference = 1; opt_preserve = 1; break;
            case 'f': opt_force = 1; break;
            case 'i': opt_interactive = 1; opt_noclobber = 0; break;
            case 'l': opt_hardlink = 1; break;
            case 'L': opt_dereference = 1; opt_no_dereference = 0; break;
            case 'n': opt_noclobber = 1; opt_interactive = 0; break;
            case 'p': opt_preserve = 1; break;
            case 'P': opt_no_dereference = 1; opt_dereference = 0; break;
            case 'r': case 'R': opt_recursive = 1; break;
            case 's': opt_symlink = 1; break;
            case 'S':
                if (arg[j+1]) { opt_suffix = arg + j + 1; j = (int)strlen(arg) - 1; }
                else if (i+1 < argc) { opt_suffix = argv[++i]; }
                else die("option '-S' requires an argument");
                break;
            case 't':
                if (arg[j+1]) { opt_target_dir = 1; target_dir_arg = arg + j + 1; j = (int)strlen(arg) - 1; }
                else if (i+1 < argc) { opt_target_dir = 1; target_dir_arg = argv[++i]; }
                else die("option '-t' requires an argument");
                break;
            case 'T': opt_no_target_dir = 1; break;
            case 'u': opt_update = 1; break;
            case 'v': opt_verbose = 1; break;
            case 'x': opt_one_fs = 1; break;
            case 'h': usage(); break;
            default:
                die("invalid option -- '%c'", arg[j]);
            }
        }
    }
    *optind_out = i;
}

/* ---- Main ---- */

int main(int argc, char **argv) {
    program_name = argv[0];

    int optind;
    parse_args(argc, argv, &optind);

    int nargs = argc - optind;
    char **args = argv + optind;

    /* Determine sources and destination */
    char **srcs;
    int nsrcs;
    char *dst;

    if (opt_target_dir) {
        dst  = target_dir_arg;
        srcs = args;
        nsrcs = nargs;
        if (nsrcs < 1)
            die("missing file operand");
    } else if (opt_no_target_dir) {
        if (nargs != 2)
            die("requires exactly 2 arguments with -T");
        srcs  = args;
        nsrcs = 1;
        dst   = args[1];
    } else {
        if (nargs < 2) {
            if (nargs == 0)
                die("missing file operand");
            else
                die("missing destination file operand after '%s'", args[0]);
        }
        srcs  = args;
        nsrcs = nargs - 1;
        dst   = args[nargs - 1];
    }

    /* Is destination a directory? */
    struct stat dst_st;
    int dst_is_dir = 0;
    if (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode))
        dst_is_dir = 1;

    if (nsrcs > 1 && !dst_is_dir) {
        if (opt_no_target_dir)
            die("cannot overwrite non-directory '%s' with multiple sources", dst);
        die("target '%s' is not a directory", dst);
    }

    /* For -x, record the source filesystem */
    if (opt_one_fs && nsrcs > 0) {
        struct stat s;
        if (stat(srcs[0], &s) == 0)
            src_dev = s.st_dev;
    }

    int ret = 0;

    for (int i = 0; i < nsrcs; i++) {
        char *src = srcs[i];

        if (opt_strip_trailing) {
            src = strip_trailing_slashes(src);
        }

        /* Determine actual destination path */
        char *actual_dst;
        if (dst_is_dir && !opt_no_target_dir) {
            /* dest is a directory: dst/basename(src) */
            char *src_copy = xstrdup(src);
            char *base = basename(src_copy);
            actual_dst = path_join(dst, base);
            free(src_copy);
        } else {
            actual_dst = xstrdup(dst);
        }

        /* -l: hard link */
        if (opt_hardlink) {
            struct stat s_st, d_st;
            int d_exists = (lstat(actual_dst, &d_st) == 0);
            if (d_exists) {
                if (stat(src, &s_st) == 0 &&
                    s_st.st_dev == d_st.st_dev && s_st.st_ino == d_st.st_ino) {
                    /* same file */
                } else {
                    if (opt_noclobber) { free(actual_dst); continue; }
                    unlink(actual_dst);
                }
            }
            if (link(src, actual_dst) != 0) {
                warn("cannot create hard link '%s' -> '%s': %s",
                     actual_dst, src, strerror(errno));
                ret = 1;
            } else if (opt_verbose) {
                printf("'%s' -> '%s'\n", src, actual_dst);
            }
            free(actual_dst);
            continue;
        }

        /* -s: symbolic link */
        if (opt_symlink) {
            struct stat d_st;
            int d_exists = (lstat(actual_dst, &d_st) == 0);
            if (d_exists) {
                if (opt_noclobber) { free(actual_dst); continue; }
                unlink(actual_dst);
            }
            /* Use absolute or relative path for symlink target */
            char real_src[PATH_MAX];
            const char *lnk_target = src;
            if (realpath(src, real_src))
                lnk_target = real_src;
            if (symlink(lnk_target, actual_dst) != 0) {
                warn("cannot create symbolic link '%s' -> '%s': %s",
                     actual_dst, lnk_target, strerror(errno));
                ret = 1;
            } else if (opt_verbose) {
                printf("'%s' -> '%s'\n", src, actual_dst);
            }
            free(actual_dst);
            continue;
        }

        if (copy_entry(src, actual_dst) != 0)
            ret = 1;

        if (opt_strip_trailing && src != srcs[i])
            free(src);
        free(actual_dst);
    }

    return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
