/*
 * mkdir - make directories
 * Full implementation with all standard features
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdarg.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static const char *program_name = "mkdir";

static int opt_parents  = 0;   /* -p: create parent dirs, no error if exists */
static int opt_verbose  = 0;   /* -v: print each created directory */
static mode_t opt_mode  = 0777; /* -m: directory permission mode */
static int opt_mode_set = 0;

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

/* ---- Mode parsing ---- */

/*
 * Parse a symbolic or octal mode string and apply it to base_mode.
 * Supports: octal literals, and symbolic like u+rwx,g-w,o=r,a+x,+x,=775, etc.
 */
static int parse_mode(const char *str, mode_t *out_mode) {
    /* Octal? */
    if (str[0] >= '0' && str[0] <= '7') {
        char *end;
        long val = strtol(str, &end, 8);
        if (*end != '\0' || val < 0 || val > 07777) {
            fprintf(stderr, "%s: invalid mode '%s'\n", program_name, str);
            return -1;
        }
        *out_mode = (mode_t)val;
        return 0;
    }

    /* Symbolic mode: [ugoa]*[+-=][rwxXstugo]* */
    mode_t mode = 0777; /* default base for new directory */
    const char *p = str;

    while (*p) {
        /* Parse who */
        int who = 0; /* 0 means 'a' (all) */
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            switch (*p++) {
                case 'u': who |= 0100; break;
                case 'g': who |= 0010; break;
                case 'o': who |= 0001; break;
                case 'a': who  = 0111; break;
            }
        }
        if (who == 0) who = 0111; /* default: all */

        /* Parse op */
        char op = *p;
        if (op != '+' && op != '-' && op != '=') {
            fprintf(stderr, "%s: invalid mode '%s'\n", program_name, str);
            return -1;
        }
        p++;

        /* Parse perms */
        mode_t perms = 0;
        while (*p && *p != ',') {
            switch (*p++) {
                case 'r': perms |= 0444; break;
                case 'w': perms |= 0222; break;
                case 'x': perms |= 0111; break;
                case 'X':
                    /* X: execute only if dir or already executable */
                    perms |= 0111;
                    break;
                case 's': perms |= 06000; break;
                case 't': perms |= 01000; break;
                case 'u': perms |= (mode >> 6) & 07; perms |= perms << 3 | perms << 6; break;
                case 'g': perms |= (mode >> 3) & 07; break;
                case 'o': perms |= (mode)      & 07; break;
                default:
                    fprintf(stderr, "%s: invalid mode '%s'\n", program_name, str);
                    return -1;
            }
        }

        /* Apply: mask perms to who */
        mode_t mask = 0;
        if (who & 0100) mask |= (perms & 0777) >> 0 & 0700; /* simplified */
        if (who & 0010) mask |= (perms & 0077);
        if (who & 0001) mask |= (perms & 0007);

        /* Better: apply per-group */
        mask = 0;
        /* user bits */
        if (who & 0100) {
            if (perms & 0400) mask |= S_IRUSR;
            if (perms & 0200) mask |= S_IWUSR;
            if (perms & 0100) mask |= S_IXUSR;
        }
        /* group bits */
        if (who & 0010) {
            if (perms & 0040) mask |= S_IRGRP;
            if (perms & 0020) mask |= S_IWGRP;
            if (perms & 0010) mask |= S_IXGRP;
        }
        /* other bits */
        if (who & 0001) {
            if (perms & 0004) mask |= S_IROTH;
            if (perms & 0002) mask |= S_IWOTH;
            if (perms & 0001) mask |= S_IXOTH;
        }
        /* setuid/setgid/sticky — apply regardless of who */
        if (perms & S_ISUID) mask |= S_ISUID;
        if (perms & S_ISGID) mask |= S_ISGID;
        if (perms & S_ISVTX) mask |= S_ISVTX;

        switch (op) {
            case '+': mode |=  mask; break;
            case '-': mode &= ~mask; break;
            case '=': {
                /* Clear all bits for 'who', then set mask */
                mode_t clear = 0;
                if (who & 0100) clear |= S_IRWXU;
                if (who & 0010) clear |= S_IRWXG;
                if (who & 0001) clear |= S_IRWXO;
                mode = (mode & ~clear) | mask;
                break;
            }
        }

        if (*p == ',') p++;
    }

    *out_mode = mode;
    return 0;
}

/* ---- mkdir -p: create path with all parents ---- */

static int mkdir_parents(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= PATH_MAX) {
        warn("path too long or empty: '%s'", path);
        return -1;
    }

    memcpy(tmp, path, len + 1);

    /* Remove trailing slashes (keep at least one char) */
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';

    /* Walk each component */
    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            char saved = tmp[i];
            tmp[i] = '\0';

            if (mkdir(tmp, mode) != 0) {
                if (errno == EEXIST) {
                    /* Check it's actually a directory */
                    struct stat st;
                    if (stat(tmp, &st) == 0 && !S_ISDIR(st.st_mode)) {
                        warn("cannot create directory '%s': Not a directory", path);
                        return -1;
                    }
                    /* Already exists as dir: fine with -p */
                } else {
                    warn("cannot create directory '%s': %s", tmp, strerror(errno));
                    return -1;
                }
            } else {
                if (opt_verbose)
                    printf("%s: created directory '%s'\n", program_name, tmp);
            }

            tmp[i] = saved;
        }
    }
    return 0;
}

/* ---- Usage ---- */

static void usage(void) {
    fprintf(stderr,
"Usage: %s [OPTION]... DIRECTORY...\n"
"Create the DIRECTORY(ies), if they do not already exist.\n"
"\n"
"  -m, --mode=MODE   set file mode (as in chmod), not a=rwx - umask\n"
"  -p, --parents     no error if existing, make parent directories as needed\n"
"  -v, --verbose     print a message for each created directory\n"
"  -h, --help        display this help and exit\n",
        program_name);
    exit(EXIT_FAILURE);
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
            if (strcmp(opt, "parents") == 0) {
                opt_parents = 1;
            } else if (strcmp(opt, "verbose") == 0) {
                opt_verbose = 1;
            } else if (strncmp(opt, "mode=", 5) == 0) {
                if (parse_mode(opt + 5, &opt_mode) != 0)
                    exit(EXIT_FAILURE);
                opt_mode_set = 1;
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
            case 'p': opt_parents = 1; break;
            case 'v': opt_verbose = 1; break;
            case 'm':
                /* -mMODE or -m MODE */
                if (arg[j + 1]) {
                    if (parse_mode(arg + j + 1, &opt_mode) != 0)
                        exit(EXIT_FAILURE);
                    opt_mode_set = 1;
                    j = (int)strlen(arg) - 1; /* consume rest */
                } else if (i + 1 < argc) {
                    if (parse_mode(argv[++i], &opt_mode) != 0)
                        exit(EXIT_FAILURE);
                    opt_mode_set = 1;
                } else {
                    die("option '-m' requires an argument");
                }
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

    /*
     * If -m was set, we need to temporarily bypass the umask so the exact
     * mode is applied. We set the umask to 0 and apply mode directly.
     * For the default case (no -m), we let the umask apply normally.
     */
    mode_t create_mode = opt_mode_set ? opt_mode : 0777;

    mode_t old_umask = 0;
    if (opt_mode_set) {
        old_umask = umask(0);
    }

    int ret = 0;

    for (; i < argc; i++) {
        const char *path = argv[i];

        if (opt_parents) {
            if (mkdir_parents(path, create_mode) != 0)
                ret = 1;
            else if (opt_verbose) {
                /* verbose already printed per-component inside mkdir_parents */
            }
        } else {
            if (mkdir(path, create_mode) != 0) {
                warn("cannot create directory '%s': %s", path, strerror(errno));
                ret = 1;
            } else {
                if (opt_verbose)
                    printf("%s: created directory '%s'\n", program_name, path);
            }
        }
    }

    if (opt_mode_set)
        umask(old_umask);

    return ret ? EXIT_FAILURE : EXIT_SUCCESS;
}
