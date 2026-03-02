/*
 * mknod - make block or character special files, FIFOs, or sockets
 *
 * Usage:
 *   mknod [OPTION]... NAME TYPE [MAJOR MINOR]
 *
 * TYPE:
 *   b  - block special file
 *   c/u - character special file
 *   p  - FIFO (named pipe)
 *
 * Options:
 *   -m, --mode=MODE   set file permission bits to MODE (as in chmod)
 *   -Z                set SELinux security context
 *       --context[=CTX]  like -Z, or set SELinux context to CTX
 *       --help        display help and exit
 *       --version     output version information and exit
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Symbolic mode parsing (subset of chmod's mode grammar)             */
/* ------------------------------------------------------------------ */

/*
 * Parse an octal or symbolic mode string and return the resulting
 * mode_t, applying to a base of 0666 (the conventional default for
 * mknod -m).
 *
 * Symbolic syntax:  [ugoa]*[+-=][rwxXst]*[,...]
 * Octal syntax:     0755 etc.
 *
 * Returns -1 on parse error.
 */

static int parse_octal(const char *s, mode_t *out)
{
    if (*s == '\0') return -1;
    mode_t val = 0;
    while (*s) {
        if (*s < '0' || *s > '7') return -1;
        val = val * 8 + (*s - '0');
        s++;
    }
    *out = val;
    return 0;
}

/* Apply a single symbolic clause to *mode given umask. */
static void apply_clause(mode_t *mode, mode_t umsk,
                         const char *who, char op, const char *perms)
{
    /* Determine which bits the 'who' selects */
    int do_u = 0, do_g = 0, do_o = 0;
    int all = 0;
    for (const char *w = who; *w; w++) {
        switch (*w) {
            case 'u': do_u = 1; break;
            case 'g': do_g = 1; break;
            case 'o': do_o = 1; break;
            case 'a': do_u = do_g = do_o = 1; all = 1; break;
        }
    }
    /* If no who specified, treat as 'a' but mask with ~umask */
    if (!do_u && !do_g && !do_o) {
        do_u = do_g = do_o = 1;
    }

    /* Build a permission mask from the perm letters */
    mode_t bits = 0;
    for (const char *p = perms; *p; p++) {
        switch (*p) {
            case 'r':
                if (do_u) bits |= S_IRUSR;
                if (do_g) bits |= S_IRGRP;
                if (do_o) bits |= S_IROTH;
                break;
            case 'w':
                if (do_u) bits |= S_IWUSR;
                if (do_g) bits |= S_IWGRP;
                if (do_o) bits |= S_IWOTH;
                break;
            case 'x':
                if (do_u) bits |= S_IXUSR;
                if (do_g) bits |= S_IXGRP;
                if (do_o) bits |= S_IXOTH;
                break;
            case 'X':
                /* X: set execute only if file is directory or already executable */
                if (*mode & (S_IXUSR|S_IXGRP|S_IXOTH)) {
                    if (do_u) bits |= S_IXUSR;
                    if (do_g) bits |= S_IXGRP;
                    if (do_o) bits |= S_IXOTH;
                }
                break;
            case 's':
                if (do_u) bits |= S_ISUID;
                if (do_g) bits |= S_ISGID;
                break;
            case 't':
                bits |= S_ISVTX;
                break;
            case 'u': {
                /* copy user bits */
                mode_t u = (*mode & S_IRWXU);
                if (do_g) bits |= (u >> 3);
                if (do_o) bits |= (u >> 6);
                if (do_u) bits |= u;
                break;
            }
            case 'g': {
                mode_t g = (*mode & S_IRWXG);
                if (do_u) bits |= (g << 3);
                if (do_o) bits |= (g >> 3);
                if (do_g) bits |= g;
                break;
            }
            case 'o': {
                mode_t o = (*mode & S_IRWXO);
                if (do_u) bits |= (o << 6);
                if (do_g) bits |= (o << 3);
                if (do_o) bits |= o;
                break;
            }
        }
    }

    /* When who is not explicitly given, mask with ~umask */
    if (!all && !(who[0])) {
        bits &= ~umsk;
    }

    switch (op) {
        case '+': *mode |= bits;  break;
        case '-': *mode &= ~bits; break;
        case '=':
            /* For =, clear the relevant who-bits then set */
            {
                mode_t clear = 0;
                if (do_u) clear |= (S_IRWXU | S_ISUID);
                if (do_g) clear |= (S_IRWXG | S_ISGID);
                if (do_o) clear |= (S_IRWXO);
                if (do_u || do_g || do_o) clear |= S_ISVTX;
                *mode = (*mode & ~clear) | bits;
            }
            break;
    }
}

/*
 * parse_mode: parse MODE string (octal or symbolic) relative to base_mode.
 * Returns 0 on success, -1 on error.
 */
static int parse_mode(const char *mode_str, mode_t base_mode, mode_t *result)
{
    /* Try octal first */
    if (mode_str[0] >= '0' && mode_str[0] <= '9') {
        return parse_octal(mode_str, result);
    }

    /* Symbolic */
    mode_t umsk = umask(0);
    umask(umsk); /* restore */

    mode_t m = base_mode;
    char buf[256];
    if (strlen(mode_str) >= sizeof(buf)) return -1;
    strncpy(buf, mode_str, sizeof(buf)-1);
    buf[sizeof(buf)-1] = '\0';

    char *clause = strtok(buf, ",");
    while (clause) {
        /* Parse who */
        char who[8] = {0};
        int wi = 0;
        while (*clause == 'u' || *clause == 'g' || *clause == 'o' || *clause == 'a') {
            if (wi < 7) who[wi++] = *clause;
            clause++;
        }
        who[wi] = '\0';

        /* Parse op */
        char op = *clause;
        if (op != '+' && op != '-' && op != '=')
            return -1;
        clause++;

        /* Parse perms */
        char perms[32] = {0};
        int pi = 0;
        while (*clause && *clause != ',') {
            if (pi < 31) perms[pi++] = *clause;
            clause++;
        }
        perms[pi] = '\0';

        apply_clause(&m, umsk, who, op, perms);

        clause = strtok(NULL, ",");
    }

    *result = m;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void usage(const char *prog)
{
    printf(
        "Usage: %s [OPTION]... NAME TYPE [MAJOR MINOR]\n"
        "Create the special file NAME of the given TYPE.\n"
        "\n"
        "Mandatory arguments to long options are mandatory for short options too.\n"
        "  -m, --mode=MODE    set file permission bits to MODE, not a=rw - umask\n"
        "  -Z                 set the SELinux security context to default type\n"
        "      --context[=CTX]  like -Z, or if CTX is specified then set the SELinux\n"
        "                       or SMACK security context to CTX\n"
        "      --help         display this help and exit\n"
        "      --version      output version information and exit\n"
        "\n"
        "Both MAJOR and MINOR must be specified when TYPE is b, c, or u, and they\n"
        "must be omitted when TYPE is p.  If MAJOR or MINOR begins with 0x or 0X,\n"
        "it is interpreted as hexadecimal; otherwise, if it begins with 0, as octal;\n"
        "otherwise, as decimal.  TYPE may be:\n"
        "\n"
        "  b      create a block (buffered) special file\n"
        "  c, u   create a character (unbuffered) special file\n"
        "  p      create a FIFO\n"
        "\n"
        "NOTE: your shell may have its own version of mknod, which usually supersedes\n"
        "the version described here.  Please refer to your shell's documentation\n"
        "for details about the options it supports.\n",
        prog);
}

static void version(void)
{
    printf("mknod (custom implementation) 1.0\n");
}

/*
 * Parse a number that may be decimal, octal (0-prefixed), or hex (0x-prefixed).
 * Returns 0 on success, -1 on error.
 */
static int parse_devnum(const char *s, unsigned long *out)
{
    if (*s == '\0') return -1;
    char *end;
    errno = 0;
    unsigned long val = strtoul(s, &end, 0);
    if (errno || *end != '\0') return -1;
    *out = val;
    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    const char *prog = argv[0];
    const char *mode_str = NULL;
    int opt_Z = 0;
    const char *selinux_ctx = NULL;

    int i;
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (strcmp(argv[i], "--") == 0) { i++; break; }

        /* Long options */
        if (strncmp(argv[i], "--", 2) == 0) {
            if (strncmp(argv[i], "--mode=", 7) == 0) {
                mode_str = argv[i] + 7;
            } else if (strcmp(argv[i], "--mode") == 0) {
                if (i+1 >= argc) {
                    fprintf(stderr, "%s: option '--mode' requires an argument\n", prog);
                    fprintf(stderr, "Try '%s --help' for more information.\n", prog);
                    return 1;
                }
                mode_str = argv[++i];
            } else if (strcmp(argv[i], "--help") == 0) {
                usage(prog);
                return 0;
            } else if (strcmp(argv[i], "--version") == 0) {
                version();
                return 0;
            } else if (strcmp(argv[i], "--context") == 0) {
                opt_Z = 1;
                selinux_ctx = NULL;
            } else if (strncmp(argv[i], "--context=", 10) == 0) {
                opt_Z = 1;
                selinux_ctx = argv[i] + 10;
            } else {
                fprintf(stderr, "%s: unrecognized option '%s'\n", prog, argv[i]);
                fprintf(stderr, "Try '%s --help' for more information.\n", prog);
                return 1;
            }
            continue;
        }

        /* Short options */
        for (int j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
                case 'm':
                    /* -m MODE or -mMODE */
                    if (argv[i][j+1] != '\0') {
                        mode_str = &argv[i][j+1];
                        goto next_arg; /* consume rest of this argv as mode */
                    } else {
                        if (i+1 >= argc) {
                            fprintf(stderr, "%s: option requires an argument -- 'm'\n", prog);
                            fprintf(stderr, "Try '%s --help' for more information.\n", prog);
                            return 1;
                        }
                        mode_str = argv[++i];
                        goto next_arg;
                    }
                    break;
                case 'Z':
                    opt_Z = 1;
                    break;
                default:
                    fprintf(stderr, "%s: invalid option -- '%c'\n", prog, argv[i][j]);
                    fprintf(stderr, "Try '%s --help' for more information.\n", prog);
                    return 1;
            }
        }
        next_arg:;
    }

    /* Warn about -Z / --context: not supported without SELinux */
    if (opt_Z || selinux_ctx) {
        fprintf(stderr, "%s: -Z/--context requires SELinux or SMACK support\n", prog);
        return 1;
    }
    (void)selinux_ctx;

    /* Remaining positional args */
    int npos = argc - i;

    if (npos < 2) {
        fprintf(stderr, "%s: missing operand\n", prog);
        fprintf(stderr, "Try '%s --help' for more information.\n", prog);
        return 1;
    }

    const char *name = argv[i];
    const char *type_str = argv[i+1];

    if (strlen(type_str) != 1 ||
        (type_str[0] != 'b' && type_str[0] != 'c' &&
         type_str[0] != 'u' && type_str[0] != 'p')) {
        fprintf(stderr, "%s: invalid argument '%s'\n", prog, type_str);
        fprintf(stderr, "Try '%s --help' for more information.\n", prog);
        return 1;
    }
    char type = type_str[0];

    unsigned long major_num = 0, minor_num = 0;

    if (type == 'p') {
        /* FIFO: must have NO major/minor */
        if (npos > 2) {
            fprintf(stderr,
                "%s: extra operand '%s'\n"
                "FIFOs do not have major and minor device numbers.\n"
                "Try '%s --help' for more information.\n",
                prog, argv[i+2], prog);
            return 1;
        }
    } else {
        /* Block / character: must have BOTH major and minor */
        if (npos < 4) {
            fprintf(stderr,
                "%s: missing operand after '%s'\n"
                "Try '%s --help' for more information.\n",
                prog, argv[i + (npos >= 3 ? 2 : 1)], prog);
            return 1;
        }
        if (npos > 4) {
            fprintf(stderr,
                "%s: extra operand '%s'\n"
                "Try '%s --help' for more information.\n",
                prog, argv[i+4], prog);
            return 1;
        }

        if (parse_devnum(argv[i+2], &major_num) != 0) {
            fprintf(stderr, "%s: invalid major device number '%s'\n", prog, argv[i+2]);
            return 1;
        }
        if (parse_devnum(argv[i+3], &minor_num) != 0) {
            fprintf(stderr, "%s: invalid minor device number '%s'\n", prog, argv[i+3]);
            return 1;
        }
    }

    /* Determine mode */
    /* Default: 0666 & ~umask  (mknod convention) */
    mode_t umsk = umask(0);
    umask(umsk);
    mode_t file_mode = 0666 & ~umsk;

    if (mode_str) {
        if (parse_mode(mode_str, file_mode, &file_mode) != 0) {
            fprintf(stderr, "%s: invalid mode '%s'\n", prog, mode_str);
            return 1;
        }
        /* When -m is given, override umask entirely (like GNU mknod) */
        umask(0);
    }

    /* Build the full mode including the file type bits */
    mode_t full_mode = file_mode;
    dev_t dev = 0;

    switch (type) {
        case 'b':
            full_mode |= S_IFBLK;
            dev = makedev(major_num, minor_num);
            break;
        case 'c':
        case 'u':
            full_mode |= S_IFCHR;
            dev = makedev(major_num, minor_num);
            break;
        case 'p':
            full_mode |= S_IFIFO;
            dev = 0;
            break;
    }

    if (mknod(name, full_mode, dev) != 0) {
        fprintf(stderr, "%s: '%s': %s\n", prog, name, strerror(errno));
        return 1;
    }

    return 0;
}
