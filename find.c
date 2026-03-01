/*
 * find - A reimplementation of the Unix find command in C
 *
 * Supported options:
 *   -name <pattern>     Match filename using glob pattern (*, ?)
 *   -type <f|d|l>       Match file type: f=file, d=directory, l=symlink
 *   -size <n>[ckMG]     Match file size (c=bytes, k=KB, M=MB, G=GB)
 *                       Prefix with + (greater than) or - (less than)
 *   -mtime <n>          Match modification time in days
 *                       Prefix with + (older than) or - (newer than)
 *   -maxdepth <n>       Limit recursion depth
 *   -mindepth <n>       Skip entries shallower than depth
 *   -empty              Match empty files or directories
 *   -exec <cmd> {} \;   Execute command on each match
 *   -print              Print matching path (default action)
 *   -delete             Delete matching files
 *   -not / !            Negate the next expression
 *   -and / -a           Logical AND (default between expressions)
 *   -or  / -o           Logical OR
 *
 * Usage: find [path...] [expression]
 *
 * Examples:
 *   find . -name "*.c"
 *   find /tmp -type f -size +100k
 *   find . -name "*.log" -mtime +7 -delete
 *   find . -not -name "*.o" -type f
 *   find . -name "*.c" -exec wc -l {} \;
 */

#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <time.h>
#include <fnmatch.h>
#include <errno.h>
#include <limits.h>

/* ── expression node types ───────────────────────────────────────────────── */

typedef enum {
    EXPR_TRUE,
    EXPR_NAME,
    EXPR_INAME,
    EXPR_TYPE,
    EXPR_SIZE,
    EXPR_MTIME,
    EXPR_EMPTY,
    EXPR_PRINT,
    EXPR_DELETE,
    EXPR_EXEC,
    EXPR_NOT,
    EXPR_AND,
    EXPR_OR,
} ExprType;

typedef struct Expr Expr;

struct Expr {
    ExprType type;

    /* EXPR_NAME / EXPR_INAME */
    char *pattern;

    /* EXPR_TYPE */
    char filetype;          /* 'f', 'd', 'l', 'b', 'c', 'p', 's' */

    /* EXPR_SIZE / EXPR_MTIME */
    long   num;             /* magnitude */
    char   cmp;             /* '+', '-', or '=' */
    char   unit;            /* size unit: 'c','k','M','G' */

    /* EXPR_EXEC */
    char **argv;            /* NULL-terminated; "{}" placeholder kept */
    int    argc;

    /* EXPR_NOT / EXPR_AND / EXPR_OR */
    Expr  *left;
    Expr  *right;
};

/* ── globals ─────────────────────────────────────────────────────────────── */

static int g_maxdepth = INT_MAX;
static int g_mindepth = 0;
static int g_had_action = 0;   /* did the user specify -print/-exec/-delete? */

/* ── expression memory ───────────────────────────────────────────────────── */

static Expr *expr_new(ExprType t) {
    Expr *e = calloc(1, sizeof *e);
    if (!e) { perror("calloc"); exit(1); }
    e->type = t;
    return e;
}

/* ── glob helper ─────────────────────────────────────────────────────────── */

static int glob_match(const char *pattern, const char *string, int nocase) {
    int flags = nocase ? FNM_CASEFOLD : 0;
    return fnmatch(pattern, string, flags) == 0;
}

/* ── size parsing ────────────────────────────────────────────────────────── */

/* Parse strings like "+100k", "-2M", "512c", "1G"
 * Default unit (no suffix) is 512-byte blocks (POSIX), but we treat as bytes
 * for simplicity (common GNU find behaviour).
 */
static void parse_size(const char *s, long *out_num, char *out_cmp, char *out_unit) {
    *out_cmp  = '=';
    *out_unit = 'c';   /* bytes */

    if (*s == '+') { *out_cmp = '+'; s++; }
    else if (*s == '-') { *out_cmp = '-'; s++; }

    char *end;
    *out_num = strtol(s, &end, 10);

    if (*end == 'c' || *end == 'k' || *end == 'M' || *end == 'G')
        *out_unit = *end;
}

static long size_to_bytes(long n, char unit) {
    switch (unit) {
        case 'k': return n * 1024L;
        case 'M': return n * 1024L * 1024L;
        case 'G': return n * 1024L * 1024L * 1024L;
        default : return n;  /* 'c' = bytes */
    }
}

/* ── parser ──────────────────────────────────────────────────────────────── */

/* Forward declarations */
static Expr *parse_expr(char **argv, int *pos, int argc);
static Expr *parse_and (char **argv, int *pos, int argc);
static Expr *parse_primary(char **argv, int *pos, int argc);

static Expr *parse_expr(char **argv, int *pos, int argc) {
    Expr *left = parse_and(argv, pos, argc);

    while (*pos < argc &&
           (strcmp(argv[*pos], "-or") == 0 || strcmp(argv[*pos], "-o") == 0)) {
        (*pos)++;
        Expr *right = parse_and(argv, pos, argc);
        Expr *e = expr_new(EXPR_OR);
        e->left  = left;
        e->right = right;
        left = e;
    }
    return left;
}

static Expr *parse_and(char **argv, int *pos, int argc) {
    Expr *left = parse_primary(argv, pos, argc);
    if (!left) return NULL;

    while (*pos < argc) {
        /* explicit -and/-a */
        int explicit_and = 0;
        if (strcmp(argv[*pos], "-and") == 0 || strcmp(argv[*pos], "-a") == 0) {
            explicit_and = 1;
            (*pos)++;
        }
        /* stop if we hit -or/-o or closing paren */
        if (*pos >= argc) break;
        if (strcmp(argv[*pos], "-or") == 0 || strcmp(argv[*pos], "-o") == 0) break;
        if (strcmp(argv[*pos], ")") == 0) break;

        /* try to parse next primary; if it returns NULL and we didn't see
           explicit -and, just stop */
        int saved_pos = *pos;
        Expr *right = parse_primary(argv, pos, argc);
        if (!right) {
            *pos = saved_pos;
            break;
        }
        Expr *e = expr_new(EXPR_AND);
        e->left  = left;
        e->right = right;
        left = e;
        (void)explicit_and;
    }
    return left;
}

static Expr *parse_primary(char **argv, int *pos, int argc) {
    if (*pos >= argc) return NULL;

    const char *tok = argv[*pos];

    /* grouping */
    if (strcmp(tok, "(") == 0) {
        (*pos)++;
        Expr *e = parse_expr(argv, pos, argc);
        if (*pos < argc && strcmp(argv[*pos], ")") == 0) (*pos)++;
        return e;
    }

    /* negation */
    if (strcmp(tok, "-not") == 0 || strcmp(tok, "!") == 0) {
        (*pos)++;
        Expr *child = parse_primary(argv, pos, argc);
        Expr *e = expr_new(EXPR_NOT);
        e->left = child;
        return e;
    }

    /* -name */
    if (strcmp(tok, "-name") == 0 || strcmp(tok, "-iname") == 0) {
        int nocase = (strcmp(tok, "-iname") == 0);
        (*pos)++;
        if (*pos >= argc) { fprintf(stderr, "find: %s needs argument\n", tok); exit(1); }
        Expr *e = expr_new(nocase ? EXPR_INAME : EXPR_NAME);
        e->pattern = argv[(*pos)++];
        return e;
    }

    /* -type */
    if (strcmp(tok, "-type") == 0) {
        (*pos)++;
        if (*pos >= argc) { fprintf(stderr, "find: -type needs argument\n"); exit(1); }
        Expr *e = expr_new(EXPR_TYPE);
        e->filetype = argv[(*pos)++][0];
        return e;
    }

    /* -size */
    if (strcmp(tok, "-size") == 0) {
        (*pos)++;
        if (*pos >= argc) { fprintf(stderr, "find: -size needs argument\n"); exit(1); }
        Expr *e = expr_new(EXPR_SIZE);
        parse_size(argv[(*pos)++], &e->num, &e->cmp, &e->unit);
        return e;
    }

    /* -mtime */
    if (strcmp(tok, "-mtime") == 0) {
        (*pos)++;
        if (*pos >= argc) { fprintf(stderr, "find: -mtime needs argument\n"); exit(1); }
        Expr *e = expr_new(EXPR_MTIME);
        char *s = argv[(*pos)++];
        e->cmp = '=';
        if (*s == '+') { e->cmp = '+'; s++; }
        else if (*s == '-') { e->cmp = '-'; s++; }
        e->num = atol(s);
        return e;
    }

    /* -maxdepth / -mindepth */
    if (strcmp(tok, "-maxdepth") == 0) {
        (*pos)++;
        if (*pos >= argc) { fprintf(stderr, "find: -maxdepth needs argument\n"); exit(1); }
        g_maxdepth = atoi(argv[(*pos)++]);
        return expr_new(EXPR_TRUE);
    }
    if (strcmp(tok, "-mindepth") == 0) {
        (*pos)++;
        if (*pos >= argc) { fprintf(stderr, "find: -mindepth needs argument\n"); exit(1); }
        g_mindepth = atoi(argv[(*pos)++]);
        return expr_new(EXPR_TRUE);
    }

    /* -empty */
    if (strcmp(tok, "-empty") == 0) {
        (*pos)++;
        return expr_new(EXPR_EMPTY);
    }

    /* -print */
    if (strcmp(tok, "-print") == 0) {
        (*pos)++;
        g_had_action = 1;
        return expr_new(EXPR_PRINT);
    }

    /* -delete */
    if (strcmp(tok, "-delete") == 0) {
        (*pos)++;
        g_had_action = 1;
        return expr_new(EXPR_DELETE);
    }

    /* -exec cmd [args] {} \; */
    if (strcmp(tok, "-exec") == 0) {
        (*pos)++;
        g_had_action = 1;
        Expr *e = expr_new(EXPR_EXEC);
        int start = *pos;
        /* collect until \; */
        while (*pos < argc && strcmp(argv[*pos], ";") != 0 &&
               !(argv[*pos][0] == '\\' && argv[*pos][1] == ';'))
            (*pos)++;
        int count = *pos - start;
        if (*pos < argc) (*pos)++;  /* skip ; */
        e->argc = count;
        e->argv = malloc((count + 1) * sizeof(char *));
        for (int i = 0; i < count; i++) e->argv[i] = argv[start + i];
        e->argv[count] = NULL;
        return e;
    }

    /* Unknown token — could be a path or unrecognised option */
    return NULL;
}

/* ── expression evaluator ────────────────────────────────────────────────── */

static int eval_expr(Expr *e, const char *path, const char *name,
                     struct stat *st, int depth);

static int do_exec(Expr *e, const char *path) {
    /* Build argv replacing {} with path */
    char **av = malloc((e->argc + 1) * sizeof(char *));
    for (int i = 0; i < e->argc; i++) {
        if (strcmp(e->argv[i], "{}") == 0)
            av[i] = (char *)path;
        else
            av[i] = e->argv[i];
    }
    av[e->argc] = NULL;

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); free(av); return 0; }
    if (pid == 0) {
        execvp(av[0], av);
        perror(av[0]);
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    free(av);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int eval_expr(Expr *e, const char *path, const char *name,
                     struct stat *st, int depth) {
    if (!e) return 1;

    switch (e->type) {
    case EXPR_TRUE:
        return 1;

    case EXPR_NAME:
        return glob_match(e->pattern, name, 0);

    case EXPR_INAME:
        return glob_match(e->pattern, name, 1);

    case EXPR_TYPE: {
        switch (e->filetype) {
        case 'f': return S_ISREG(st->st_mode);
        case 'd': return S_ISDIR(st->st_mode);
        case 'l': return S_ISLNK(st->st_mode);
        case 'b': return S_ISBLK(st->st_mode);
        case 'c': return S_ISCHR(st->st_mode);
        case 'p': return S_ISFIFO(st->st_mode);
        case 's': return S_ISSOCK(st->st_mode);
        }
        return 0;
    }

    case EXPR_SIZE: {
        long fsz = (long)st->st_size;
        long threshold = size_to_bytes(e->num, e->unit);
        if (e->cmp == '+') return fsz > threshold;
        if (e->cmp == '-') return fsz < threshold;
        return fsz == threshold;
    }

    case EXPR_MTIME: {
        time_t now = time(NULL);
        long days = (long)((now - st->st_mtime) / 86400L);
        if (e->cmp == '+') return days > e->num;
        if (e->cmp == '-') return days < e->num;
        return days == e->num;
    }

    case EXPR_EMPTY:
        if (S_ISREG(st->st_mode)) return st->st_size == 0;
        if (S_ISDIR(st->st_mode)) {
            DIR *d = opendir(path);
            if (!d) return 0;
            struct dirent *ent;
            int empty = 1;
            while ((ent = readdir(d))) {
                if (strcmp(ent->d_name, ".") && strcmp(ent->d_name, "..")) {
                    empty = 0; break;
                }
            }
            closedir(d);
            return empty;
        }
        return 0;

    case EXPR_PRINT:
        puts(path);
        return 1;

    case EXPR_DELETE:
        if (S_ISDIR(st->st_mode)) {
            if (rmdir(path) != 0) { perror(path); return 0; }
        } else {
            if (unlink(path) != 0) { perror(path); return 0; }
        }
        return 1;

    case EXPR_EXEC:
        return do_exec(e, path);

    case EXPR_NOT:
        return !eval_expr(e->left, path, name, st, depth);

    case EXPR_AND:
        return eval_expr(e->left, path, name, st, depth) &&
               eval_expr(e->right, path, name, st, depth);

    case EXPR_OR:
        return eval_expr(e->left, path, name, st, depth) ||
               eval_expr(e->right, path, name, st, depth);
    }
    return 0;
}

/* ── traversal ───────────────────────────────────────────────────────────── */

static void traverse(const char *path, Expr *expr, int depth) {
    struct stat st;

    if (lstat(path, &st) != 0) {
        fprintf(stderr, "find: '%s': %s\n", path, strerror(errno));
        return;
    }

    /* Basename */
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    /* Apply expression at this depth */
    if (depth >= g_mindepth) {
        int matched = eval_expr(expr, path, name, &st, depth);
        /* If no action was specified, default to -print on match */
        if (!g_had_action && matched)
            puts(path);
    }

    /* Recurse into directories */
    if (S_ISDIR(st.st_mode) && depth < g_maxdepth) {
        DIR *d = opendir(path);
        if (!d) {
            fprintf(stderr, "find: '%s': %s\n", path, strerror(errno));
            return;
        }

        struct dirent *ent;
        while ((ent = readdir(d))) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;

            /* Build child path */
            char child[PATH_MAX];
            int n = snprintf(child, sizeof child, "%s/%s", path, ent->d_name);
            if (n < 0 || n >= (int)sizeof child) {
                fprintf(stderr, "find: path too long: %s/%s\n", path, ent->d_name);
                continue;
            }

            traverse(child, expr, depth + 1);
        }
        closedir(d);
    }
}

/* ── main ────────────────────────────────────────────────────────────────── */

static void usage(void) {
    fprintf(stderr,
        "Usage: find [path...] [expression]\n"
        "\n"
        "Expressions:\n"
        "  -name <pattern>     Filename glob match\n"
        "  -iname <pattern>    Case-insensitive filename match\n"
        "  -type f|d|l|b|c|p|s  File type\n"
        "  -size [+-]n[ckMG]   File size\n"
        "  -mtime [+-]n        Modification time in days\n"
        "  -maxdepth n         Max recursion depth\n"
        "  -mindepth n         Min recursion depth\n"
        "  -empty              Empty files/directories\n"
        "  -print              Print path (default)\n"
        "  -delete             Delete matching files\n"
        "  -exec cmd {} \\;    Execute command\n"
        "  -not / !            Negate\n"
        "  -and / -a           Logical AND (default)\n"
        "  -or  / -o           Logical OR\n"
        "  ( expression )      Grouping\n"
    );
    exit(1);
}

int main(int argc, char *argv[]) {
    /* Collect starting paths (everything before first '-' or '(' token) */
    int i = 1;
    char **paths    = NULL;
    int   numpaths  = 0;

    while (i < argc && argv[i][0] != '-' &&
           strcmp(argv[i], "!") != 0 && strcmp(argv[i], "(") != 0) {
        paths = realloc(paths, (numpaths + 1) * sizeof(char *));
        paths[numpaths++] = argv[i++];
    }

    /* Default path is "." */
    if (numpaths == 0) {
        paths = realloc(paths, sizeof(char *));
        paths[0] = ".";
        numpaths = 1;
    }

    /* Parse expression from remaining args */
    Expr *expr = NULL;
    if (i < argc) {
        expr = parse_expr(argv, &i, argc);
        if (i < argc) {
            fprintf(stderr, "find: unexpected argument: %s\n", argv[i]);
            usage();
        }
    }

    /* Traverse each starting path */
    for (int p = 0; p < numpaths; p++)
        traverse(paths[p], expr, 0);

    free(paths);
    return 0;
}
