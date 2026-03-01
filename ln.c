/*
 * ln.c - Implementation of the ln (link) command
 *
 * Supports all standard ln options:
 *   -b, --backup[=CONTROL]   make a backup of each existing destination file
 *   -f, --force              remove existing destination files
 *   -i, --interactive        prompt whether to remove destinations
 *   -L, --logical            dereference TARGETs that are symbolic links
 *   -n, --no-dereference     treat LINK_NAME as a normal file if it is a symlink to a dir
 *   -P, --physical           make hard links directly to symbolic links
 *   -r, --relative           create symbolic links relative to link location
 *   -s, --symbolic           make symbolic links instead of hard links
 *   -S, --suffix=SUFFIX      override the usual backup suffix
 *   -t, --target-directory=DIR  specify the DIRECTORY in which to create the links
 *   -T, --no-target-directory   treat LINK_NAME as a normal file always
 *   -v, --verbose            print name of each linked file
 *       --help               display this help and exit
 *       --version            output version information and exit
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <libgen.h>
#include <getopt.h>
#include <stdbool.h>

#define VERSION "1.0.0"
#define PROGRAM_NAME "ln"

/* Backup methods */
typedef enum {
    BACKUP_NONE,
    BACKUP_NUMBERED,    /* t, numbered */
    BACKUP_EXISTING,    /* nil, existing */
    BACKUP_SIMPLE,      /* never, simple */
} BackupType;

/* Global options */
static bool opt_backup = false;
static BackupType backup_type = BACKUP_EXISTING;
static bool opt_force = false;
static bool opt_interactive = false;
static bool opt_logical = false;
static bool opt_no_dereference = false;
static bool opt_physical = false;
static bool opt_relative = false;
static bool opt_symbolic = false;
static char *opt_suffix = "~";
static char *opt_target_dir = NULL;
static bool opt_no_target_dir = false;
static bool opt_verbose = false;

static void usage(int status) {
    if (status != EXIT_SUCCESS) {
        fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
    } else {
        printf("Usage: %s [OPTION]... [-T] TARGET LINK_NAME\n", PROGRAM_NAME);
        printf("  or:  %s [OPTION]... TARGET\n", PROGRAM_NAME);
        printf("  or:  %s [OPTION]... TARGET... DIRECTORY\n", PROGRAM_NAME);
        printf("  or:  %s [OPTION]... -t DIRECTORY TARGET...\n", PROGRAM_NAME);
        printf("\nCreate hard links by default, symbolic links with --symbolic.\n");
        printf("By default, each destination (name of new link) should not already exist.\n");
        printf("When creating hard links, each TARGET must exist. Symbolic links\n");
        printf("can hold arbitrary text; if later resolved, a relative link is\n");
        printf("interpreted in relation to its parent directory.\n\n");
        printf("Mandatory arguments to long options are mandatory for short options too.\n");
        printf("  -b                         like --backup but does not accept an argument\n");
        printf("      --backup[=CONTROL]     make a backup of each existing destination file\n");
        printf("  -f, --force                remove existing destination files\n");
        printf("  -i, --interactive          prompt whether to remove destinations\n");
        printf("  -L, --logical              dereference TARGETs that are symbolic links\n");
        printf("  -n, --no-dereference       treat LINK_NAME as a normal file if\n");
        printf("                             it is a symbolic link to a directory\n");
        printf("  -P, --physical             make hard links directly to symbolic links\n");
        printf("  -r, --relative             create symbolic links relative to link location\n");
        printf("  -s, --symbolic             make symbolic links instead of hard links\n");
        printf("  -S, --suffix=SUFFIX        override the usual backup suffix\n");
        printf("  -t, --target-directory=DIRECTORY  specify the DIRECTORY in which to create\n");
        printf("                               the links\n");
        printf("  -T, --no-target-directory  treat LINK_NAME as a normal file always\n");
        printf("  -v, --verbose              print name of each linked file\n");
        printf("      --help                 display this help and exit\n");
        printf("      --version              output version information and exit\n");
        printf("\nThe backup suffix is '~', unless set with --suffix or SIMPLE_BACKUP_SUFFIX.\n");
        printf("The version control method may be selected via the --backup option or through\n");
        printf("the VERSION_CONTROL environment variable. Here are the values:\n\n");
        printf("  none, off       never make backups (even if --backup is given)\n");
        printf("  numbered, t     make numbered backups\n");
        printf("  existing, nil   numbered if numbered backups exist, simple otherwise\n");
        printf("  simple, never   always make simple backups\n");
    }
    exit(status);
}

static void version(void) {
    printf("%s version %s\n", PROGRAM_NAME, VERSION);
    printf("A from-scratch implementation of the GNU ln command.\n");
    exit(EXIT_SUCCESS);
}

static BackupType parse_backup_type(const char *method) {
    if (!method || strcmp(method, "existing") == 0 || strcmp(method, "nil") == 0)
        return BACKUP_EXISTING;
    if (strcmp(method, "none") == 0 || strcmp(method, "off") == 0)
        return BACKUP_NONE;
    if (strcmp(method, "numbered") == 0 || strcmp(method, "t") == 0)
        return BACKUP_NUMBERED;
    if (strcmp(method, "simple") == 0 || strcmp(method, "never") == 0)
        return BACKUP_SIMPLE;
    fprintf(stderr, "%s: invalid argument '%s' for backup type\n", PROGRAM_NAME, method);
    fprintf(stderr, "Valid arguments are:\n");
    fprintf(stderr, "  - 'none', 'off'\n");
    fprintf(stderr, "  - 'simple', 'never'\n");
    fprintf(stderr, "  - 'existing', 'nil'\n");
    fprintf(stderr, "  - 'numbered', 't'\n");
    exit(EXIT_FAILURE);
}

/* Generate backup filename */
static char *backup_name(const char *path) {
    char *buf = malloc(PATH_MAX);
    if (!buf) { perror("malloc"); exit(EXIT_FAILURE); }

    BackupType effective = backup_type;

    if (effective == BACKUP_EXISTING) {
        /* Check if a numbered backup exists */
        char test[PATH_MAX];
        snprintf(test, sizeof(test), "%s.~1~", path);
        struct stat st;
        if (lstat(test, &st) == 0)
            effective = BACKUP_NUMBERED;
        else
            effective = BACKUP_SIMPLE;
    }

    if (effective == BACKUP_NUMBERED) {
        int n = 1;
        while (1) {
            snprintf(buf, PATH_MAX, "%s.~%d~", path, n);
            struct stat st;
            if (lstat(buf, &st) != 0)
                break;
            n++;
        }
    } else {
        /* BACKUP_SIMPLE */
        snprintf(buf, PATH_MAX, "%s%s", path, opt_suffix);
    }

    return buf;
}

/* Compute relative path from link_dir to target */
static char *make_relative(const char *target, const char *link_path) {
    /* Get absolute paths */
    char abs_target[PATH_MAX];
    char abs_link[PATH_MAX];
    char link_dir[PATH_MAX];

    if (realpath(target, abs_target) == NULL) {
        /* Target doesn't exist yet (for symlinks), use as-is */
        strncpy(abs_target, target, PATH_MAX - 1);
        abs_target[PATH_MAX - 1] = '\0';
    }

    /* Get the directory of the link */
    strncpy(abs_link, link_path, PATH_MAX - 1);
    abs_link[PATH_MAX - 1] = '\0';
    strncpy(link_dir, dirname(abs_link), PATH_MAX - 1);
    link_dir[PATH_MAX - 1] = '\0';

    /* Make link_dir absolute */
    char abs_link_dir[PATH_MAX];
    if (realpath(link_dir, abs_link_dir) == NULL) {
        strncpy(abs_link_dir, link_dir, PATH_MAX - 1);
        abs_link_dir[PATH_MAX - 1] = '\0';
    }

    /* Split into components */
    char target_parts[PATH_MAX][256];
    char link_parts[PATH_MAX][256];
    int t_count = 0, l_count = 0;

    char tmp[PATH_MAX];
    strncpy(tmp, abs_target, PATH_MAX - 1);
    char *tok = strtok(tmp, "/");
    while (tok) { strncpy(target_parts[t_count++], tok, 255); tok = strtok(NULL, "/"); }

    strncpy(tmp, abs_link_dir, PATH_MAX - 1);
    tok = strtok(tmp, "/");
    while (tok) { strncpy(link_parts[l_count++], tok, 255); tok = strtok(NULL, "/"); }

    /* Find common prefix length */
    int common = 0;
    while (common < t_count && common < l_count &&
           strcmp(target_parts[common], link_parts[common]) == 0)
        common++;

    /* Build relative path: go up (l_count - common) levels, then down target remaining */
    char *result = malloc(PATH_MAX);
    if (!result) { perror("malloc"); exit(EXIT_FAILURE); }
    result[0] = '\0';

    int ups = l_count - common;
    for (int i = 0; i < ups; i++) {
        if (strlen(result) > 0) strcat(result, "/");
        strcat(result, "..");
    }

    for (int i = common; i < t_count; i++) {
        if (strlen(result) > 0) strcat(result, "/");
        strcat(result, target_parts[i]);
    }

    if (strlen(result) == 0)
        strcpy(result, ".");

    return result;
}

/* Remove a file, prompting if interactive */
static bool remove_existing(const char *path) {
    if (opt_interactive) {
        fprintf(stderr, "%s: replace '%s'? ", PROGRAM_NAME, path);
        fflush(stderr);
        char response[16];
        if (fgets(response, sizeof(response), stdin) == NULL)
            return false;
        if (response[0] != 'y' && response[0] != 'Y')
            return false;
    }

    if (remove(path) != 0) {
        fprintf(stderr, "%s: cannot remove '%s': %s\n", PROGRAM_NAME, path, strerror(errno));
        return false;
    }
    return true;
}

/* Core link creation function */
static bool do_link(const char *target, const char *link_name) {
    struct stat st;
    bool dest_exists = (lstat(link_name, &st) == 0);

    /* Handle existing destination */
    if (dest_exists) {
        /* If dest is a directory and no_dereference is not set,
         * we should not be here (caller handles dir target) */

        if (opt_backup) {
            char *bname = backup_name(link_name);
            if (rename(link_name, bname) != 0) {
                fprintf(stderr, "%s: cannot backup '%s': %s\n", PROGRAM_NAME, link_name, strerror(errno));
                free(bname);
                return false;
            }
            if (opt_verbose)
                printf("(backup: '%s')\n", bname);
            free(bname);
            dest_exists = false;
        } else if (opt_force) {
            if (!remove_existing(link_name))
                return false;
            dest_exists = false;
        } else if (opt_interactive) {
            if (!remove_existing(link_name))
                return false;
            dest_exists = false;
        } else {
            fprintf(stderr, "%s: failed to create %s '%s': File exists\n",
                    PROGRAM_NAME,
                    opt_symbolic ? "symbolic link" : "hard link",
                    link_name);
            return false;
        }
    }

    /* Resolve target for logical/physical hard links */
    char resolved_target[PATH_MAX];
    const char *effective_target = target;

    if (!opt_symbolic) {
        if (opt_logical) {
            /* Dereference symlinks in target */
            if (realpath(target, resolved_target) != NULL)
                effective_target = resolved_target;
        }
        /* opt_physical: link directly to symlink (default for hard links) */
    }

    /* Make relative symlink if requested */
    char *rel_target = NULL;
    if (opt_symbolic && opt_relative) {
        rel_target = make_relative(target, link_name);
        effective_target = rel_target;
    }

    int ret;
    if (opt_symbolic) {
        ret = symlink(effective_target, link_name);
    } else {
        ret = link(effective_target, link_name);
    }

    if (rel_target) free(rel_target);

    if (ret != 0) {
        fprintf(stderr, "%s: failed to create %s '%s' -> '%s': %s\n",
                PROGRAM_NAME,
                opt_symbolic ? "symbolic link" : "hard link",
                link_name, target, strerror(errno));
        return false;
    }

    if (opt_verbose) {
        printf("'%s' -> '%s'\n", link_name, target);
    }

    return true;
}

/* Check if path is a directory (following symlinks unless no_dereference) */
static bool is_directory(const char *path) {
    struct stat st;
    int ret;
    if (opt_no_dereference)
        ret = lstat(path, &st);
    else
        ret = stat(path, &st);
    return (ret == 0 && S_ISDIR(st.st_mode));
}

int main(int argc, char *argv[]) {
    /* Check environment for backup suffix */
    const char *env_suffix = getenv("SIMPLE_BACKUP_SUFFIX");
    if (env_suffix) opt_suffix = (char *)env_suffix;

    /* Check environment for version control */
    const char *env_vc = getenv("VERSION_CONTROL");
    if (env_vc) backup_type = parse_backup_type(env_vc);

    static struct option long_options[] = {
        {"backup",           optional_argument, 0, 'b'},
        {"force",            no_argument,       0, 'f'},
        {"interactive",      no_argument,       0, 'i'},
        {"logical",          no_argument,       0, 'L'},
        {"no-dereference",   no_argument,       0, 'n'},
        {"physical",         no_argument,       0, 'P'},
        {"relative",         no_argument,       0, 'r'},
        {"symbolic",         no_argument,       0, 's'},
        {"suffix",           required_argument, 0, 'S'},
        {"target-directory", required_argument, 0, 't'},
        {"no-target-directory", no_argument,   0, 'T'},
        {"verbose",          no_argument,       0, 'v'},
        {"help",             no_argument,       0,  1 },
        {"version",          no_argument,       0,  2 },
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "bfiLnPrsS:t:Tv", long_options, NULL)) != -1) {
        switch (c) {
        case 'b':
            opt_backup = true;
            if (optarg)
                backup_type = parse_backup_type(optarg);
            break;
        case 'f':
            opt_force = true;
            opt_interactive = false;
            break;
        case 'i':
            opt_interactive = true;
            opt_force = false;
            break;
        case 'L':
            opt_logical = true;
            opt_physical = false;
            break;
        case 'n':
            opt_no_dereference = true;
            break;
        case 'P':
            opt_physical = true;
            opt_logical = false;
            break;
        case 'r':
            opt_relative = true;
            break;
        case 's':
            opt_symbolic = true;
            break;
        case 'S':
            opt_suffix = optarg;
            opt_backup = true;
            break;
        case 't':
            opt_target_dir = optarg;
            break;
        case 'T':
            opt_no_target_dir = true;
            break;
        case 'v':
            opt_verbose = true;
            break;
        case 1:
            usage(EXIT_SUCCESS);
            break;
        case 2:
            version();
            break;
        case '?':
        default:
            usage(EXIT_FAILURE);
        }
    }

    /* Validate conflicting options */
    if (opt_target_dir && opt_no_target_dir) {
        fprintf(stderr, "%s: cannot combine --target-directory (-t) and --no-target-directory (-T)\n", PROGRAM_NAME);
        exit(EXIT_FAILURE);
    }

    int remaining = argc - optind;
    char **args = argv + optind;

    if (remaining == 0) {
        fprintf(stderr, "%s: missing file operand\n", PROGRAM_NAME);
        fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
        exit(EXIT_FAILURE);
    }

    bool success = true;

    if (opt_target_dir) {
        /* ln [options] -t DIRECTORY TARGET... */
        if (!is_directory(opt_target_dir)) {
            fprintf(stderr, "%s: target '%s' is not a directory\n", PROGRAM_NAME, opt_target_dir);
            exit(EXIT_FAILURE);
        }
        for (int i = 0; i < remaining; i++) {
            char link_name[PATH_MAX];
            char *bname = basename(args[i]);
            snprintf(link_name, sizeof(link_name), "%s/%s", opt_target_dir, bname);
            if (!do_link(args[i], link_name))
                success = false;
        }
    } else if (opt_no_target_dir) {
        /* ln [options] -T TARGET LINK_NAME */
        if (remaining != 2) {
            fprintf(stderr, "%s: extra operand '%s'\n", PROGRAM_NAME, args[2]);
            fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
            exit(EXIT_FAILURE);
        }
        if (!do_link(args[0], args[1]))
            success = false;
    } else if (remaining == 1) {
        /* ln [options] TARGET  (link in current directory) */
        char *bname = basename(args[0]);
        if (!do_link(args[0], bname))
            success = false;
    } else if (remaining == 2 && !is_directory(args[1])) {
        /* ln [options] TARGET LINK_NAME */
        /* But if LINK_NAME is an existing symlink to dir and no_dereference is false,
         * treat as directory */
        struct stat st;
        bool link_is_symlink_to_dir = false;
        if (!opt_no_dereference) {
            if (stat(args[1], &st) == 0 && S_ISDIR(st.st_mode)) {
                link_is_symlink_to_dir = true;
            }
        }

        if (link_is_symlink_to_dir) {
            /* Treat as target directory */
            char link_name[PATH_MAX];
            char *bname = basename(args[0]);
            snprintf(link_name, sizeof(link_name), "%s/%s", args[1], bname);
            if (!do_link(args[0], link_name))
                success = false;
        } else {
            if (!do_link(args[0], args[1]))
                success = false;
        }
    } else {
        /* ln [options] TARGET... DIRECTORY */
        char *dir = args[remaining - 1];
        if (!is_directory(dir)) {
            if (remaining == 2) {
                /* Two args, second is not a dir: treat as TARGET LINK_NAME */
                if (!do_link(args[0], args[1]))
                    success = false;
            } else {
                fprintf(stderr, "%s: target '%s' is not a directory\n", PROGRAM_NAME, dir);
                exit(EXIT_FAILURE);
            }
        } else {
            for (int i = 0; i < remaining - 1; i++) {
                char link_name[PATH_MAX];
                char tmp[PATH_MAX];
                strncpy(tmp, args[i], PATH_MAX - 1);
                char *bname = basename(tmp);
                snprintf(link_name, sizeof(link_name), "%s/%s", dir, bname);
                if (!do_link(args[i], link_name))
                    success = false;
            }
        }
    }

    return success ? EXIT_SUCCESS : EXIT_FAILURE;
}
