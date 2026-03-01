/*
 * rm - remove files or directories
 * Full implementation with all standard features
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *program_name = "rm";

/* Options */
static int opt_force        = 0;  /* -f: ignore nonexistent, no prompts */
static int opt_interactive  = 0;  /* -i: prompt before every removal */
static int opt_interactive_once = 0; /* -I: prompt once if >3 files or recursive */
static int opt_recursive    = 0;  /* -r/-R: remove directories recursively */
static int opt_dir          = 0;  /* -d: remove empty directories */
static int opt_verbose      = 0;  /* -v: explain what is being done */
static int opt_no_preserve_root = 0; /* --no-preserve-root */
static int opt_one_fs       = 0;  /* --one-file-system */

/* Track if we've given the once-interactive prompt */
static int once_prompted    = 0;

/* Exit status */
static int exit_status      = 0;

/* ---- Utility ---- */

static void err_msg(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", program_name);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}

static void warn_msg(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", program_name);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit_status = 1;
}

static void usage(void) {
    fprintf(stderr,
"Usage: %s [OPTION]... [FILE]...\n"
"Remove (unlink) the FILE(s).\n"
"\n"
"  -f, --force              ignore nonexistent files and arguments, never prompt\n"
"  -i                       prompt before every removal\n"
"  -I                       prompt once before removing more than three files,\n"
"                             or when removing recursively\n"
"  -r, -R, --recursive      remove directories and their contents recursively\n"
"  -d, --dir                remove empty directories\n"
"  -v, --verbose            explain what is being done\n"
"      --no-preserve-root   do not treat '/' specially\n"
"      --preserve-root      do not remove '/' (default)\n"
"      --one-file-system    when removing a hierarchy recursively, skip any\n"
"                             directory that is on a file system different from\n"
"                             that of the corresponding command line argument\n"
"  -h, --help               display this help and exit\n",
        program_name);
    exit(EXIT_FAILURE);
}

/* ---- Prompt helpers ---- */

static int yesno(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fflush(stderr);

    int c = getchar();
    int yes = (c == 'y' || c == 'Y');
    while (c != '\n' && c != EOF)
        c = getchar();
    return yes;
}

/* Determine if stdin is a terminal (for write-protected file prompting) */
static int stdin_is_tty(void) {
    return isatty(STDIN_FILENO);
}

/* ---- Resolve path to check for root ---- */

static int is_root(const char *path) {
    /* Simple check: resolved path == "/" */
    char resolved[PATH_MAX];
    if (realpath(path, resolved) == NULL)
        return 0;
    return strcmp(resolved, "/") == 0;
}

/* ---- Core removal ---- */

/* Remove a single file/symlink/special file */
static int remove_file(const char *path, const struct stat *st) {
    /* -i: always prompt */
    if (opt_interactive) {
        if (!yesno("%s: remove %s '%s'? ",
                   program_name,
                   S_ISLNK(st->st_mode) ? "symbolic link" : "file",
                   path))
            return 0;
    } else if (!opt_force) {
        /* Prompt if write-protected and stdin is a tty */
        if (stdin_is_tty() && access(path, W_OK) != 0 && errno == EACCES) {
            if (!yesno("%s: remove write-protected file '%s'? ",
                       program_name, path))
                return 0;
        }
    }

    if (unlink(path) != 0) {
        if (!opt_force || errno != ENOENT) {
            warn_msg("cannot remove '%s': %s", path, strerror(errno));
            return -1;
        }
    } else {
        if (opt_verbose)
            printf("removed '%s'\n", path);
    }
    return 0;
}

/* Forward declaration */
static int remove_entry(const char *path, dev_t root_dev);

/* Remove a directory recursively */
static int remove_dir_recursive(const char *path, dev_t root_dev) {
    /* -i: prompt before descending */
    if (opt_interactive) {
        if (!yesno("%s: descend into directory '%s'? ", program_name, path))
            return 0;
    }

    DIR *dir = opendir(path);
    if (!dir) {
        warn_msg("cannot open directory '%s': %s", path, strerror(errno));
        return -1;
    }

    struct dirent *ent;
    int ret = 0;
    while ((ent = readdir(dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        /* Build full path */
        size_t plen = strlen(path);
        size_t nlen = strlen(ent->d_name);
        char *child = malloc(plen + nlen + 2);
        if (!child) {
            err_msg("out of memory");
            closedir(dir);
            return -1;
        }
        memcpy(child, path, plen);
        if (plen > 0 && path[plen - 1] != '/')
            child[plen++] = '/';
        memcpy(child + plen, ent->d_name, nlen + 1);

        if (remove_entry(child, root_dev) != 0)
            ret = -1;

        free(child);
    }
    closedir(dir);

    /* Remove the (now empty) directory itself */
    if (opt_interactive) {
        if (!yesno("%s: remove directory '%s'? ", program_name, path))
            return ret;
    }

    if (rmdir(path) != 0) {
        warn_msg("cannot remove directory '%s': %s", path, strerror(errno));
        return -1;
    }
    if (opt_verbose)
        printf("removed directory '%s'\n", path);

    return ret;
}

/* Remove one entry (file, symlink, or directory) */
static int remove_entry(const char *path, dev_t root_dev) {
    struct stat st;

    if (lstat(path, &st) != 0) {
        if (opt_force && errno == ENOENT)
            return 0;
        warn_msg("cannot remove '%s': %s", path, strerror(errno));
        return -1;
    }

    /* --one-file-system: skip if different device than root arg */
    if (opt_one_fs && root_dev != 0 && S_ISDIR(st.st_mode)) {
        if (st.st_dev != root_dev) {
            warn_msg("skipping '%s', since it's on a different device", path);
            return 0;
        }
    }

    if (S_ISDIR(st.st_mode)) {
        if (opt_recursive) {
            return remove_dir_recursive(path, root_dev ? root_dev : st.st_dev);
        } else if (opt_dir) {
            /* -d: remove empty directory */
            if (opt_interactive) {
                if (!yesno("%s: remove directory '%s'? ", program_name, path))
                    return 0;
            }
            if (rmdir(path) != 0) {
                warn_msg("cannot remove '%s': %s", path, strerror(errno));
                return -1;
            }
            if (opt_verbose)
                printf("removed directory '%s'\n", path);
            return 0;
        } else {
            warn_msg("cannot remove '%s': Is a directory", path);
            return -1;
        }
    }

    return remove_file(path, &st);
}

/* ---- Main ---- */

int main(int argc, char **argv) {
    program_name = argv[0];
    int preserve_root = 1; /* default */

    int i;
    for (i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (arg[0] != '-' || strcmp(arg, "-") == 0) break;
        if (strcmp(arg, "--") == 0) { i++; break; }

        /* Long options */
        if (strncmp(arg, "--", 2) == 0) {
            char *opt = arg + 2;
            if (strcmp(opt, "force") == 0) {
                opt_force = 1; opt_interactive = 0; opt_interactive_once = 0;
            } else if (strcmp(opt, "interactive") == 0 ||
                       strcmp(opt, "interactive=always") == 0) {
                opt_interactive = 1; opt_force = 0;
            } else if (strcmp(opt, "interactive=once") == 0) {
                opt_interactive_once = 1; opt_force = 0;
            } else if (strcmp(opt, "interactive=never") == 0) {
                opt_interactive = 0; opt_interactive_once = 0;
            } else if (strcmp(opt, "recursive") == 0) {
                opt_recursive = 1;
            } else if (strcmp(opt, "dir") == 0) {
                opt_dir = 1;
            } else if (strcmp(opt, "verbose") == 0) {
                opt_verbose = 1;
            } else if (strcmp(opt, "no-preserve-root") == 0) {
                opt_no_preserve_root = 1; preserve_root = 0;
            } else if (strcmp(opt, "preserve-root") == 0) {
                preserve_root = 1; opt_no_preserve_root = 0;
            } else if (strcmp(opt, "one-file-system") == 0) {
                opt_one_fs = 1;
            } else if (strcmp(opt, "help") == 0) {
                usage();
            } else {
                err_msg("unrecognized option '--%s'", opt);
                exit(EXIT_FAILURE);
            }
            continue;
        }

        /* Short options */
        for (int j = 1; arg[j]; j++) {
            switch (arg[j]) {
            case 'f':
                opt_force = 1;
                opt_interactive = 0;
                opt_interactive_once = 0;
                break;
            case 'i':
                opt_interactive = 1;
                opt_interactive_once = 0;
                opt_force = 0;
                break;
            case 'I':
                opt_interactive_once = 1;
                opt_interactive = 0;
                opt_force = 0;
                break;
            case 'r': case 'R':
                opt_recursive = 1;
                break;
            case 'd':
                opt_dir = 1;
                break;
            case 'v':
                opt_verbose = 1;
                break;
            case 'h':
                usage();
                break;
            default:
                err_msg("invalid option -- '%c'", arg[j]);
                exit(EXIT_FAILURE);
            }
        }
    }

    int nfiles = argc - i;
    char **files = argv + i;

    /* No files given */
    if (nfiles == 0) {
        if (!opt_force) {
            fprintf(stderr, "%s: missing operand\n", program_name);
            fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    /* -I: prompt once if >3 files or recursive */
    if (opt_interactive_once && !once_prompted) {
        if (nfiles > 3 || opt_recursive) {
            char prompt[128];
            if (opt_recursive && nfiles > 3)
                snprintf(prompt, sizeof(prompt),
                    "%s: remove %d arguments recursively? ", program_name, nfiles);
            else if (opt_recursive)
                snprintf(prompt, sizeof(prompt),
                    "%s: remove %d %s recursively? ", program_name, nfiles,
                    nfiles == 1 ? "argument" : "arguments");
            else
                snprintf(prompt, sizeof(prompt),
                    "%s: remove %d arguments? ", program_name, nfiles);

            if (!yesno("%s", prompt))
                return EXIT_SUCCESS;
            once_prompted = 1;
        }
    }

    for (int j = 0; j < nfiles; j++) {
        const char *path = files[j];

        /* Protect root */
        if (preserve_root && is_root(path)) {
            err_msg("it is dangerous to operate recursively on '%s'", path);
            err_msg("use --no-preserve-root to override this failsafe");
            exit_status = 1;
            continue;
        }

        /* Get dev for --one-file-system */
        dev_t root_dev = 0;
        if (opt_one_fs) {
            struct stat st;
            if (lstat(path, &st) == 0)
                root_dev = st.st_dev;
        }

        if (remove_entry(path, root_dev) != 0)
            exit_status = 1;
    }

    return exit_status ? EXIT_FAILURE : EXIT_SUCCESS;
}
