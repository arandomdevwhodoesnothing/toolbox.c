/*
 * install - copy files and set attributes
 * Implements GNU coreutils install functionality
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <grp.h>
#include <pwd.h>
#include <utime.h>
#include <getopt.h>
#include <libgen.h>
#include <limits.h>

#define VERSION "1.0"
#define DEFAULT_MODE 0755
#define BACKUP_SUFFIX "~"

/* Backup types */
typedef enum {
    BACKUP_NONE,
    BACKUP_NUMBERED,
    BACKUP_EXISTING,
    BACKUP_SIMPLE
} BackupType;

/* Global options */
static int opt_compare = 0;
static int opt_directory = 0;
static int opt_create_leading = 0;
static int opt_preserve_timestamps = 0;
static int opt_strip = 0;
static int opt_verbose = 0;
static int opt_no_target_dir = 0;
static int opt_debug = 0;
static int opt_selinux_default = 0;
static mode_t opt_mode = DEFAULT_MODE;
static int opt_mode_set = 0;
static uid_t opt_owner = (uid_t)-1;
static gid_t opt_group = (gid_t)-1;
static char *opt_target_dir = NULL;
static char *opt_strip_program = "strip";
static char *opt_suffix = NULL;
static BackupType opt_backup = BACKUP_NONE;
static int opt_backup_set = 0;

static void usage(int status) {
    if (status != 0) {
        fprintf(stderr, "Try 'install --help' for more information.\n");
    } else {
        printf(
"Usage: install [OPTION]... [-T] SOURCE DEST\n"
"  or:  install [OPTION]... SOURCE... DIRECTORY\n"
"  or:  install [OPTION]... -t DIRECTORY SOURCE...\n"
"  or:  install [OPTION]... -d DIRECTORY...\n"
"\n"
"This install program copies files (often just compiled) into destination\n"
"locations you choose.  If you want to download and install a ready-to-use\n"
"package on a GNU/Linux system, you should instead be using a package manager\n"
"like yum(1) or apt-get(1).\n"
"\n"
"In the first three forms, copy SOURCE to DEST or multiple SOURCE(s) to\n"
"the existing DIRECTORY, while setting permission modes and owner/group.\n"
"In the 4th form, create all components of the given DIRECTORY(ies).\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"      --backup[=CONTROL]  make a backup of each existing destination file\n"
"  -b                  like --backup but does not accept an argument\n"
"  -c                  (ignored)\n"
"  -C, --compare       compare content of source and destination files, and\n"
"                        if no change to content, ownership, and permissions,\n"
"                        do not modify the destination at all\n"
"  -d, --directory     treat all arguments as directory names; create all\n"
"                        components of the specified directories\n"
"  -D                  create all leading components of DEST except the last,\n"
"                        or all components of --target-directory,\n"
"                        then copy SOURCE to DEST\n"
"      --debug         explain how a file is copied.  Implies -v\n"
"  -g, --group=GROUP   set group ownership, instead of process' current group\n"
"  -m, --mode=MODE     set permission mode (as in chmod), instead of rwxr-xr-x\n"
"  -o, --owner=OWNER   set ownership (super-user only)\n"
"  -p, --preserve-timestamps   apply access/modification times of SOURCE files\n"
"                        to corresponding destination files\n"
"  -s, --strip         strip symbol tables\n"
"      --strip-program=PROGRAM  program used to strip binaries\n"
"  -S, --suffix=SUFFIX  override the usual backup suffix\n"
"  -t, --target-directory=DIRECTORY  copy all SOURCE arguments into DIRECTORY\n"
"  -T, --no-target-directory  treat DEST as a normal file\n"
"  -v, --verbose       print the name of each created file or directory\n"
"      --preserve-context  preserve SELinux security context\n"
"  -Z                      set SELinux security context of destination\n"
"                            file and each created directory to default type\n"
"      --context[=CTX]     like -Z, or if CTX is specified then set the\n"
"                            SELinux or SMACK security context to CTX\n"
"      --help        display this help and exit\n"
"      --version     output version information and exit\n"
"\n"
"The backup suffix is '~', unless set with --suffix or SIMPLE_BACKUP_SUFFIX.\n"
"The version control method may be selected via the --backup option or through\n"
"the VERSION_CONTROL environment variable.  Here are the values:\n"
"\n"
"  none, off       never make backups (even if --backup is given)\n"
"  numbered, t     make numbered backups\n"
"  existing, nil   numbered if numbered backups exist, simple otherwise\n"
"  simple, never   always make simple backups\n"
        );
    }
    exit(status);
}

static BackupType parse_backup_type(const char *s) {
    if (!s || strcmp(s, "existing") == 0 || strcmp(s, "nil") == 0)
        return BACKUP_EXISTING;
    if (strcmp(s, "none") == 0 || strcmp(s, "off") == 0)
        return BACKUP_NONE;
    if (strcmp(s, "numbered") == 0 || strcmp(s, "t") == 0)
        return BACKUP_NUMBERED;
    if (strcmp(s, "simple") == 0 || strcmp(s, "never") == 0)
        return BACKUP_SIMPLE;
    fprintf(stderr, "install: invalid backup type '%s'\n", s);
    exit(1);
}

static mode_t parse_mode(const char *s) {
    char *end;
    long val = strtol(s, &end, 8);
    if (*end != '\0' || val < 0 || val > 07777) {
        fprintf(stderr, "install: invalid mode '%s'\n", s);
        exit(1);
    }
    return (mode_t)val;
}

/* Make directory and all parents */
static int make_dir_parents(const char *path, mode_t mode, int verbose) {
    char tmp[PATH_MAX];
    char *p;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len > 0 && tmp[len-1] == '/')
        tmp[len-1] = '\0';

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                fprintf(stderr, "install: cannot create directory '%s': %s\n",
                        tmp, strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        fprintf(stderr, "install: cannot create directory '%s': %s\n",
                tmp, strerror(errno));
        return -1;
    }
    if (verbose) {
        printf("install: creating directory '%s'\n", path);
    }
    return 0;
}

/* Get backup path */
static char *backup_path(const char *dest) {
    const char *suffix = opt_suffix ? opt_suffix : 
        (getenv("SIMPLE_BACKUP_SUFFIX") ? getenv("SIMPLE_BACKUP_SUFFIX") : BACKUP_SUFFIX);
    
    BackupType bt = opt_backup;
    
    if (bt == BACKUP_EXISTING || bt == BACKUP_NUMBERED) {
        /* Check if numbered backup exists */
        char numbuf[PATH_MAX];
        int n = 1;
        while (1) {
            snprintf(numbuf, sizeof(numbuf), "%s.~%d~", dest, n);
            struct stat st;
            if (stat(numbuf, &st) != 0) break;
            n++;
        }
        if (bt == BACKUP_NUMBERED) {
            return strdup(numbuf);
        }
        /* EXISTING: use numbered if numbered backups exist */
        char check[PATH_MAX];
        snprintf(check, sizeof(check), "%s.~1~", dest);
        struct stat st;
        if (stat(check, &st) == 0) {
            return strdup(numbuf);
        }
        /* else fall through to simple */
    }
    
    /* Simple backup */
    char *result = malloc(strlen(dest) + strlen(suffix) + 1);
    sprintf(result, "%s%s", dest, suffix);
    return result;
}

/* Compare two files: returns 1 if identical content+mode+owner */
static int files_identical(const char *src, const char *dest,
                            uid_t owner, gid_t group, mode_t mode) {
    struct stat ss, ds;
    if (stat(src, &ss) != 0 || stat(dest, &ds) != 0)
        return 0;

    /* Compare mode */
    if ((ds.st_mode & 07777) != (mode & 07777))
        return 0;
    /* Compare owner */
    uid_t eff_owner = (owner != (uid_t)-1) ? owner : getuid();
    gid_t eff_group = (group != (gid_t)-1) ? group : getgid();
    if (ds.st_uid != eff_owner || ds.st_gid != eff_group)
        return 0;
    /* Compare size */
    if (ss.st_size != ds.st_size)
        return 0;

    /* Compare content */
    FILE *f1 = fopen(src, "rb");
    FILE *f2 = fopen(dest, "rb");
    if (!f1 || !f2) {
        if (f1) fclose(f1);
        if (f2) fclose(f2);
        return 0;
    }
    char buf1[4096], buf2[4096];
    int same = 1;
    while (1) {
        size_t r1 = fread(buf1, 1, sizeof(buf1), f1);
        size_t r2 = fread(buf2, 1, sizeof(buf2), f2);
        if (r1 != r2 || memcmp(buf1, buf2, r1) != 0) {
            same = 0;
            break;
        }
        if (r1 == 0) break;
    }
    fclose(f1);
    fclose(f2);
    return same;
}

/* Copy src to dest */
static int copy_file(const char *src, const char *dest) {
    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0) {
        fprintf(stderr, "install: cannot open '%s': %s\n", src, strerror(errno));
        return -1;
    }

    /* Write to temp file in same dir, then rename atomically */
    char tmppath[PATH_MAX];
    char *ddup = strdup(dest);
    char *ddir = dirname(ddup);
    snprintf(tmppath, sizeof(tmppath), "%s/install.XXXXXX", ddir);
    free(ddup);

    int fd_dst = mkstemp(tmppath);
    if (fd_dst < 0) {
        fprintf(stderr, "install: cannot create temp file in '%s': %s\n",
                ddir, strerror(errno));
        close(fd_src);
        return -1;
    }

    char buf[65536];
    ssize_t nr;
    int ret = 0;
    while ((nr = read(fd_src, buf, sizeof(buf))) > 0) {
        char *p = buf;
        while (nr > 0) {
            ssize_t nw = write(fd_dst, p, nr);
            if (nw < 0) {
                fprintf(stderr, "install: write error: %s\n", strerror(errno));
                ret = -1;
                goto done;
            }
            p += nw;
            nr -= nw;
        }
    }
    if (nr < 0) {
        fprintf(stderr, "install: read error: %s\n", strerror(errno));
        ret = -1;
    }

done:
    close(fd_src);
    close(fd_dst);
    if (ret != 0) {
        unlink(tmppath);
        return ret;
    }

    /* Rename temp to dest */
    if (rename(tmppath, dest) != 0) {
        fprintf(stderr, "install: cannot rename to '%s': %s\n", dest, strerror(errno));
        unlink(tmppath);
        return -1;
    }

    return 0;
}

/* Strip binary */
static int strip_file(const char *path) {
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "install: fork failed: %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        execlp(opt_strip_program, opt_strip_program, path, (char*)NULL);
        fprintf(stderr, "install: cannot run '%s': %s\n", opt_strip_program, strerror(errno));
        _exit(1);
    }
    int status;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "install: strip failed for '%s'\n", path);
        return -1;
    }
    return 0;
}

/* Install one file src -> dest (dest is full path to file) */
static int install_file(const char *src, const char *dest) {
    struct stat src_st;
    if (stat(src, &src_st) != 0) {
        fprintf(stderr, "install: cannot stat '%s': %s\n", src, strerror(errno));
        return -1;
    }
    if (S_ISDIR(src_st.st_mode)) {
        fprintf(stderr, "install: omitting directory '%s'\n", src);
        return -1;
    }

    mode_t mode = opt_mode_set ? opt_mode : DEFAULT_MODE;
    uid_t owner = opt_owner;
    gid_t group = opt_group;

    /* Compare mode */
    if (opt_compare && files_identical(src, dest, owner, group, mode)) {
        if (opt_debug || opt_verbose)
            printf("'%s' -> '%s' (unchanged)\n", src, dest);
        return 0;
    }

    /* Backup if needed */
    if (opt_backup_set && opt_backup != BACKUP_NONE) {
        struct stat dst_st;
        if (stat(dest, &dst_st) == 0) {
            char *bpath = backup_path(dest);
            if (rename(dest, bpath) != 0) {
                fprintf(stderr, "install: cannot backup '%s': %s\n", dest, strerror(errno));
                free(bpath);
                return -1;
            }
            if (opt_verbose || opt_debug)
                printf("install: backing up '%s' to '%s'\n", dest, bpath);
            free(bpath);
        }
    }

    /* Copy */
    if (opt_debug)
        printf("install: copying '%s' to '%s'\n", src, dest);
    if (copy_file(src, dest) != 0)
        return -1;

    /* Strip */
    if (opt_strip) {
        if (strip_file(dest) != 0)
            return -1;
    }

    /* Set mode */
    if (chmod(dest, mode) != 0) {
        fprintf(stderr, "install: cannot change permissions of '%s': %s\n",
                dest, strerror(errno));
        return -1;
    }

    /* Set owner/group */
    if (owner != (uid_t)-1 || group != (gid_t)-1) {
        if (chown(dest, owner, group) != 0) {
            fprintf(stderr, "install: cannot change ownership of '%s': %s\n",
                    dest, strerror(errno));
            return -1;
        }
    }

    /* Preserve timestamps */
    if (opt_preserve_timestamps) {
        struct utimbuf ut;
        ut.actime = src_st.st_atime;
        ut.modtime = src_st.st_mtime;
        if (utime(dest, &ut) != 0) {
            fprintf(stderr, "install: cannot preserve timestamps for '%s': %s\n",
                    dest, strerror(errno));
            return -1;
        }
    }

    if ((opt_verbose || opt_debug) && !opt_debug)
        printf("'%s' -> '%s'\n", src, dest);

    return 0;
}

int main(int argc, char *argv[]) {
    static struct option long_opts[] = {
        {"backup",              optional_argument, NULL, 'B'},
        {"compare",             no_argument,       NULL, 'C'},
        {"directory",           no_argument,       NULL, 'd'},
        {"debug",               no_argument,       NULL, 256},
        {"group",               required_argument, NULL, 'g'},
        {"mode",                required_argument, NULL, 'm'},
        {"owner",               required_argument, NULL, 'o'},
        {"preserve-timestamps", no_argument,       NULL, 'p'},
        {"strip",               no_argument,       NULL, 's'},
        {"strip-program",       required_argument, NULL, 257},
        {"suffix",              required_argument, NULL, 'S'},
        {"target-directory",    required_argument, NULL, 't'},
        {"no-target-directory", no_argument,       NULL, 'T'},
        {"verbose",             no_argument,       NULL, 'v'},
        {"preserve-context",    no_argument,       NULL, 258},
        {"context",             optional_argument, NULL, 'Z'},
        {"help",                no_argument,       NULL, 259},
        {"version",             no_argument,       NULL, 260},
        {NULL, 0, NULL, 0}
    };

    /* Check VERSION_CONTROL env */
    const char *vc = getenv("VERSION_CONTROL");
    if (vc) {
        opt_backup = parse_backup_type(vc);
        opt_backup_set = 1;
    }

    int opt;
    while ((opt = getopt_long(argc, argv, "bBcCdDg:m:o:psS:t:TvZ::", long_opts, NULL)) != -1) {
        switch (opt) {
        case 'b':
            opt_backup = BACKUP_EXISTING;
            opt_backup_set = 1;
            break;
        case 'B':
            opt_backup = parse_backup_type(optarg);
            opt_backup_set = 1;
            break;
        case 'c':
            /* ignored for compatibility */
            break;
        case 'C':
            opt_compare = 1;
            break;
        case 'd':
            opt_directory = 1;
            break;
        case 'D':
            opt_create_leading = 1;
            break;
        case 256: /* --debug */
            opt_debug = 1;
            opt_verbose = 1;
            break;
        case 'g': {
            struct group *gr = getgrnam(optarg);
            if (gr) {
                opt_group = gr->gr_gid;
            } else {
                char *end;
                long gid = strtol(optarg, &end, 10);
                if (*end != '\0') {
                    fprintf(stderr, "install: invalid group '%s'\n", optarg);
                    exit(1);
                }
                opt_group = (gid_t)gid;
            }
            break;
        }
        case 'm':
            opt_mode = parse_mode(optarg);
            opt_mode_set = 1;
            break;
        case 'o': {
            struct passwd *pw = getpwnam(optarg);
            if (pw) {
                opt_owner = pw->pw_uid;
            } else {
                char *end;
                long uid = strtol(optarg, &end, 10);
                if (*end != '\0') {
                    fprintf(stderr, "install: invalid user '%s'\n", optarg);
                    exit(1);
                }
                opt_owner = (uid_t)uid;
            }
            break;
        }
        case 'p':
            opt_preserve_timestamps = 1;
            break;
        case 's':
            opt_strip = 1;
            break;
        case 257: /* --strip-program */
            opt_strip_program = optarg;
            break;
        case 'S':
            opt_suffix = optarg;
            break;
        case 't':
            opt_target_dir = optarg;
            break;
        case 'T':
            opt_no_target_dir = 1;
            break;
        case 'v':
            opt_verbose = 1;
            break;
        case 258: /* --preserve-context */
            /* SELinux: silently ignore if not supported */
            break;
        case 'Z':
            opt_selinux_default = 1;
            /* SELinux: silently ignore if not supported */
            break;
        case 259:
            usage(0);
            break;
        case 260:
            printf("install (custom implementation) %s\n", VERSION);
            exit(0);
        default:
            usage(1);
        }
    }

    int remaining = argc - optind;
    char **args = argv + optind;
    mode_t dir_mode = opt_mode_set ? opt_mode : 0755;

    /* -d mode: create directories */
    if (opt_directory) {
        if (remaining == 0) {
            fprintf(stderr, "install: missing operand\n");
            usage(1);
        }
        int ret = 0;
        for (int i = 0; i < remaining; i++) {
            if (make_dir_parents(args[i], dir_mode, opt_verbose || opt_debug) != 0)
                ret = 1;
            /* Set ownership on leaf */
            if (opt_owner != (uid_t)-1 || opt_group != (gid_t)-1) {
                if (chown(args[i], opt_owner, opt_group) != 0) {
                    fprintf(stderr, "install: cannot change ownership of '%s': %s\n",
                            args[i], strerror(errno));
                    ret = 1;
                }
            }
            if (chmod(args[i], dir_mode) != 0 && errno != ENOENT) {
                /* best effort */
            }
        }
        return ret;
    }

    /* -t mode: explicit target directory */
    if (opt_target_dir) {
        if (remaining == 0) {
            fprintf(stderr, "install: missing file operand after '%s'\n", opt_target_dir);
            usage(1);
        }
        /* Optionally create target dir */
        if (opt_create_leading) {
            make_dir_parents(opt_target_dir, dir_mode, opt_verbose || opt_debug);
        }
        struct stat st;
        if (stat(opt_target_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "install: target '%s' is not a directory\n", opt_target_dir);
            return 1;
        }
        int ret = 0;
        for (int i = 0; i < remaining; i++) {
            char *bname = basename(strdup(args[i]));
            char dest[PATH_MAX];
            /* Avoid double slash */
            size_t tlen = strlen(opt_target_dir);
            if (tlen > 0 && opt_target_dir[tlen-1] == '/')
                snprintf(dest, sizeof(dest), "%s%s", opt_target_dir, bname);
            else
                snprintf(dest, sizeof(dest), "%s/%s", opt_target_dir, bname);
            if (install_file(args[i], dest) != 0)
                ret = 1;
        }
        return ret;
    }

    /* Need at least 2 args: source(s) + dest */
    if (remaining < 2) {
        fprintf(stderr, "install: missing destination file operand after '%s'\n",
                remaining > 0 ? args[0] : "");
        usage(1);
    }

    char *dest = args[remaining - 1];

    /* -T: treat dest as regular file, must have exactly 1 source */
    if (opt_no_target_dir) {
        if (remaining != 2) {
            fprintf(stderr, "install: extra operand '%s'\n", args[2]);
            usage(1);
        }
        /* Create leading dirs if -D */
        if (opt_create_leading) {
            char *tmp = strdup(dest);
            char *dir = dirname(tmp);
            make_dir_parents(dir, dir_mode, opt_verbose || opt_debug);
            free(tmp);
        }
        return install_file(args[0], dest) != 0 ? 1 : 0;
    }

    /* Check if dest is a directory */
    struct stat dst_st;
    int dest_is_dir = (stat(dest, &dst_st) == 0 && S_ISDIR(dst_st.st_mode));

    if (remaining == 2 && !dest_is_dir) {
        /* Single source -> dest file */
        if (opt_create_leading) {
            char *tmp = strdup(dest);
            char *dir = dirname(tmp);
            make_dir_parents(dir, dir_mode, opt_verbose || opt_debug);
            free(tmp);
        }
        return install_file(args[0], dest) != 0 ? 1 : 0;
    }

    /* Multiple sources -> dest directory */
    if (!dest_is_dir) {
        if (opt_create_leading) {
            make_dir_parents(dest, dir_mode, opt_verbose || opt_debug);
            dest_is_dir = 1;
        } else {
            fprintf(stderr, "install: target '%s' is not a directory\n", dest);
            return 1;
        }
    }

    int ret = 0;
    for (int i = 0; i < remaining - 1; i++) {
        char *bname = basename(strdup(args[i]));
        char dst_path[PATH_MAX];
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dest, bname);
        if (install_file(args[i], dst_path) != 0)
            ret = 1;
    }
    return ret;
}
