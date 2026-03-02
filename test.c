#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

/* test/[ builtin implementation
 * Returns 0 for true, 1 for false
 */

static int expr(const char *s1, const char *op, const char *s2);
static int unary(const char *op, const char *arg);
static int is_integer(const char *s);
static int parse_mode(const char *mode);

int main(int argc, char *argv[]) {
    int result = 1; /* default false */
    
    /* Check if called as '[' - requires closing ']' */
    if (strcmp(argv[0], "[") == 0) {
        if (argc < 2 || strcmp(argv[argc-1], "]") != 0) {
            fprintf(stderr, "%s: missing ']'\n", argv[0]);
            return 2; /* error */
        }
        argc--; /* ignore final ']' */
    }
    
    argc--;
    argv++;
    
    /* No arguments -> false */
    if (argc == 0) {
        return 1;
    }
    
    /* Single argument -> true if non-empty */
    if (argc == 1) {
        return (argv[0][0] != '\0') ? 0 : 1;
    }
    
    /* Handle ! operator */
    int invert = 0;
    if (strcmp(argv[0], "!") == 0) {
        invert = 1;
        argc--;
        argv++;
        
        if (argc == 0) {
            return 2; /* error: nothing after ! */
        }
    }
    
    /* Handle parentheses for grouping - simplified */
    if (argc >= 3 && strcmp(argv[0], "(") == 0 && strcmp(argv[argc-1], ")") == 0) {
        /* Create new argv without parentheses */
        char *new_argv[argc-1];
        for (int i = 1; i < argc-1; i++) {
            new_argv[i-1] = argv[i];
        }
        
        /* Call test recursively */
        char *test_argv[] = { "test", new_argv[0], new_argv[1], new_argv[2], NULL };
        int new_argc = argc-2;
        
        if (new_argc == 1) {
            result = (new_argv[0][0] != '\0') ? 0 : 1;
        } else if (new_argc == 2) {
            result = unary(new_argv[0], new_argv[1]);
        } else if (new_argc == 3) {
            result = expr(new_argv[0], new_argv[1], new_argv[2]);
        } else {
            return 2; /* too many args inside parentheses */
        }
        
        return invert ? !result : result;
    }
    
    /* Binary expression or unary */
    if (argc == 2) {
        /* Unary test */
        result = unary(argv[0], argv[1]);
    } else if (argc == 3) {
        /* Binary test */
        result = expr(argv[0], argv[1], argv[2]);
    } else {
        return 2; /* Too many arguments */
    }
    
    return invert ? !result : result;
}

/* Binary expressions */
static int expr(const char *s1, const char *op, const char *s2) {
    /* String comparison operators */
    if (strcmp(op, "=") == 0 || strcmp(op, "==") == 0) {
        return strcmp(s1, s2) == 0;
    }
    if (strcmp(op, "!=") == 0) {
        return strcmp(s1, s2) != 0;
    }
    
    /* Numeric comparison operators */
    if (strcmp(op, "-eq") == 0) {
        return atoi(s1) == atoi(s2);
    }
    if (strcmp(op, "-ne") == 0) {
        return atoi(s1) != atoi(s2);
    }
    if (strcmp(op, "-gt") == 0) {
        return atoi(s1) > atoi(s2);
    }
    if (strcmp(op, "-ge") == 0) {
        return atoi(s1) >= atoi(s2);
    }
    if (strcmp(op, "-lt") == 0) {
        return atoi(s1) < atoi(s2);
    }
    if (strcmp(op, "-le") == 0) {
        return atoi(s1) <= atoi(s2);
    }
    
    return 2; /* Error: unknown operator */
}

/* Unary expressions */
static int unary(const char *op, const char *arg) {
    struct stat st;
    
    /* File tests */
    if (strcmp(op, "-e") == 0) {
        return access(arg, F_OK) == 0;
    }
    if (strcmp(op, "-f") == 0) {
        return (stat(arg, &st) == 0 && S_ISREG(st.st_mode));
    }
    if (strcmp(op, "-d") == 0) {
        return (stat(arg, &st) == 0 && S_ISDIR(st.st_mode));
    }
    if (strcmp(op, "-h") == 0 || strcmp(op, "-L") == 0) {
        return (lstat(arg, &st) == 0 && S_ISLNK(st.st_mode));
    }
    if (strcmp(op, "-r") == 0) {
        return access(arg, R_OK) == 0;
    }
    if (strcmp(op, "-w") == 0) {
        return access(arg, W_OK) == 0;
    }
    if (strcmp(op, "-x") == 0) {
        return access(arg, X_OK) == 0;
    }
    if (strcmp(op, "-s") == 0) {
        struct stat st;
        return (stat(arg, &st) == 0 && st.st_size > 0);
    }
    if (strcmp(op, "-b") == 0) {
        return (stat(arg, &st) == 0 && S_ISBLK(st.st_mode));
    }
    if (strcmp(op, "-c") == 0) {
        return (stat(arg, &st) == 0 && S_ISCHR(st.st_mode));
    }
    if (strcmp(op, "-p") == 0) {
        return (stat(arg, &st) == 0 && S_ISFIFO(st.st_mode));
    }
    if (strcmp(op, "-S") == 0) {
        return (stat(arg, &st) == 0 && S_ISSOCK(st.st_mode));
    }
    if (strcmp(op, "-u") == 0) {
        return (stat(arg, &st) == 0 && (st.st_mode & S_ISUID));
    }
    if (strcmp(op, "-g") == 0) {
        return (stat(arg, &st) == 0 && (st.st_mode & S_ISGID));
    }
    if (strcmp(op, "-k") == 0) {
        return (stat(arg, &st) == 0 && (st.st_mode & S_ISVTX));
    }
    if (strcmp(op, "-O") == 0) {
        struct stat st;
        return (stat(arg, &st) == 0 && st.st_uid == geteuid());
    }
    if (strcmp(op, "-G") == 0) {
        struct stat st;
        return (stat(arg, &st) == 0 && st.st_gid == getegid());
    }
    if (strcmp(op, "-N") == 0) {
        struct stat st;
        return (stat(arg, &st) == 0 && st.st_atime <= st.st_mtime);
    }
    
    /* String tests */
    if (strcmp(op, "-z") == 0) {
        return arg[0] == '\0';
    }
    if (strcmp(op, "-n") == 0) {
        return arg[0] != '\0';
    }
    
    /* File descriptor test */
    if (strcmp(op, "-t") == 0) {
        int fd = atoi(arg);
        return isatty(fd);
    }
    
    /* Permissions tests with octal mode */
    if (strcmp(op, "-perm") == 0) {
        struct stat st;
        if (stat(arg, &st) != 0) return 0;
        
        mode_t mode = parse_mode(arg);
        if (mode == (mode_t)-1) return 2; /* error */
        
        return (st.st_mode & 07777) == mode;
    }
    
    /* File comparison by age */
    if (strcmp(op, "-nt") == 0 || strcmp(op, "-ot") == 0 || strcmp(op, "-ef") == 0) {
        /* These are binary but we handle them here */
        return 2; /* Should be handled as binary */
    }
    
    return 2; /* Error: unknown operator */
}

/* Check if string is a valid integer */
static int is_integer(const char *s) {
    if (*s == '-' || *s == '+') s++;
    if (*s == '\0') return 0;
    while (*s) {
        if (!isdigit((unsigned char)*s)) return 0;
        s++;
    }
    return 1;
}

/* Parse octal mode string */
static int parse_mode(const char *mode) {
    if (mode[0] == '\0') return -1;
    
    /* Check if it's octal */
    const char *p = mode;
    while (*p) {
        if (*p < '0' || *p > '7') return -1;
        p++;
    }
    
    /* Convert octal to mode_t */
    mode_t result = 0;
    while (*mode) {
        result = (result << 3) | (*mode - '0');
        mode++;
    }
    
    return result;
}
