/*
 * toolsh - A real Linux shell with ./tool syntax support
 * Supports: pipes, redirection, background jobs, variables,
 *           aliases, history, builtins, and tool-prefixed commands
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <termios.h>
#include <ctype.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <limits.h>

/* ─── Constants ─────────────────────────────────────────── */
#define SH_INPUT_MAX    4096
#define MAX_ARGS        256
#define MAX_HISTORY     500
#define MAX_ALIASES     128
#define MAX_VARS        256
#define MAX_JOBS        64
#define PROMPT_MAX      256
#define MAX_PIPES       32

/* ANSI colors */
#define C_RESET   "\033[0m"
#define C_BOLD    "\033[1m"
#define C_RED     "\033[31m"
#define C_GREEN   "\033[32m"
#define C_YELLOW  "\033[33m"
#define C_BLUE    "\033[34m"
#define C_CYAN    "\033[36m"
#define C_MAGENTA "\033[35m"
#define C_WHITE   "\033[37m"

/* ─── Known tools (used with coreutils/bin/<name> prefix) ────────── */
static const char *KNOWN_TOOLS[] = {
    "awk","basename","cat","chmod","chown","cp","cut","date","dd",
    "df","dirname","du","echo","env","false","find","head","id",
    "install","ln","ls","md5sum","mkdir","mknod","mv","pwd","readlink",
    "rm","rmdir","sed","sleep","sort","stat","sync","tac","tail","tee",
    "test","touch","tr","true","uname","uniq","wc","whoami","yes",NULL
};

/* ─── Structures ─────────────────────────────────────────── */
typedef struct {
    char *name;
    char *value;
} ShVar;

typedef struct {
    char *name;
    char *expansion;
} Alias;

typedef struct {
    pid_t  pid;
    int    id;
    char   cmd[256];
    int    running;
} Job;

typedef struct {
    char **argv;
    int    argc;
    char  *infile;
    char  *outfile;
    char  *appendfile;
    char  *errfile;
    int    background;
} Command;

/* ─── Globals ────────────────────────────────────────────── */
static char   *history[MAX_HISTORY];
static int     hist_count   = 0;
static int     hist_pos     = 0;

static Alias   aliases[MAX_ALIASES];
static int     alias_count  = 0;

static ShVar   variables[MAX_VARS];
static int     var_count    = 0;

static Job     jobs[MAX_JOBS];
static int     job_count    = 0;

static int     last_exit    = 0;
static int     shell_pid;
static char    cwd[PATH_MAX];
static int     interactive  = 1;

/* ─── Forward declarations ───────────────────────────────── */
static void    handle_sigchld(int sig);
static void    handle_sigint(int sig);
static char   *expand_variables(const char *str);
static int     run_pipeline(char *line);
static void    print_prompt(void);
static int     execute_builtin(char **argv, int argc);
static char   *sh_var_get(const char *name);
static void    sh_var_set(const char *name, const char *value);
static void    add_history(const char *line);
static char   *read_line_interactive(void);

/* ─── Utility: strip leading/trailing whitespace ─────────── */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s) - 1;
    while (e > s && isspace((unsigned char)*e)) *e-- = '\0';
    return s;
}

/* ─── Utility: duplicate string safely ──────────────────── */
static char *xstrdup(const char *s) {
    if (!s) return NULL;
    char *p = strdup(s);
    if (!p) { perror("strdup"); exit(1); }
    return p;
}

/* ─── Utility: is this a known tool? ────────────────────── */
static int is_known_tool(const char *name) {
    for (int i = 0; KNOWN_TOOLS[i]; i++)
        if (strcmp(KNOWN_TOOLS[i], name) == 0) return 1;
    return 0;
}

/* ─── Resolve command → coreutils/bin/<n> for known tools ── */
static char *resolve_command(const char *cmd) {
    /* Already explicit coreutils/bin/ path */
    if (strncmp(cmd, "coreutils/bin/", 14) == 0)
        return xstrdup(cmd);
    /* Legacy ./tool or ./bin/tool → coreutils/bin/tool */
    if (strncmp(cmd, "./", 2) == 0) {
        const char *name = cmd + 2;
        /* strip optional "bin/" sub-prefix */
        if (strncmp(name, "bin/", 4) == 0) name += 4;
        if (is_known_tool(name)) {
            char buf[PATH_MAX];
            snprintf(buf, sizeof(buf), "coreutils/bin/%s", name);
            return xstrdup(buf);
        }
        return xstrdup(cmd);
    }
    /* Bare known tool name → coreutils/bin/<n> */
    if (is_known_tool(cmd)) {
        char buf[PATH_MAX];
        snprintf(buf, sizeof(buf), "coreutils/bin/%s", cmd);
        return xstrdup(buf);
    }
    /* Unknown command — return as-is, exec_with_fallback will error */
    return xstrdup(cmd);
}

/* ─── Execute — coreutils/bin/ only, no PATH fallback ───── */
static void exec_with_fallback(char **argv) {
    /* Only known tools (resolved to coreutils/bin/) are allowed.
     * Everything else is rejected — no PATH searching ever. */
    if (strncmp(argv[0], "coreutils/bin/", 14) == 0) {
        execv(argv[0], argv);
        fprintf(stderr, C_RED "toolsh: %s: not found\n" C_RESET, argv[0]);
    } else {
        fprintf(stderr, C_RED "toolsh: %s: unknown command\n" C_RESET, argv[0]);
    }
    exit(127);
}

/* ─── Print tool listing ────────────────────────────────── */
static void print_tools(void) {
    int cols = 4, i = 0;
    printf("\n");
    while (KNOWN_TOOLS[i]) {
        printf("  " C_CYAN "coreutils/bin/%-12s" C_RESET, KNOWN_TOOLS[i]);
        i++;
        if (i % cols == 0) printf("\n");
    }
    if (i % cols != 0) printf("\n");
    printf("\n");
}

/* ─── Variable store ─────────────────────────────────────── */
static char *sh_var_get(const char *name) {
    /* Check shell vars first */
    for (int i = 0; i < var_count; i++)
        if (strcmp(variables[i].name, name) == 0)
            return variables[i].value;
    /* Fall back to environment */
    return getenv(name);
}

static void sh_var_set(const char *name, const char *value) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            free(variables[i].value);
            variables[i].value = xstrdup(value);
            setenv(name, value, 1);
            return;
        }
    }
    if (var_count < MAX_VARS) {
        variables[var_count].name  = xstrdup(name);
        variables[var_count].value = xstrdup(value);
        var_count++;
        setenv(name, value, 1);
    }
}

static void sh_var_unset(const char *name) {
    for (int i = 0; i < var_count; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            free(variables[i].name);
            free(variables[i].value);
            variables[i] = variables[--var_count];
            unsetenv(name);
            return;
        }
    }
}

/* ─── Variable expansion ─────────────────────────────────── */
static char *expand_variables(const char *str) {
    if (!str) return xstrdup("");
    char buf[SH_INPUT_MAX * 4];
    int bi = 0;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '$') {
            i++;
            if (str[i] == '?') {
                bi += snprintf(buf+bi, sizeof(buf)-bi, "%d", last_exit);
            } else if (str[i] == '$') {
                bi += snprintf(buf+bi, sizeof(buf)-bi, "%d", shell_pid);
            } else if (str[i] == '{') {
                i++;
                char var[256]; int vi = 0;
                while (str[i] && str[i] != '}') var[vi++] = str[i++];
                var[vi] = '\0';
                const char *val = sh_var_get(var);
                if (val) bi += snprintf(buf+bi, sizeof(buf)-bi, "%s", val);
            } else if (isalpha((unsigned char)str[i]) || str[i]=='_') {
                char var[256]; int vi = 0;
                while (str[i] && (isalnum((unsigned char)str[i])||str[i]=='_'))
                    var[vi++] = str[i++];
                i--;
                var[vi] = '\0';
                const char *val = sh_var_get(var);
                if (val) bi += snprintf(buf+bi, sizeof(buf)-bi, "%s", val);
            } else {
                buf[bi++] = '$';
                buf[bi++] = str[i];
            }
        } else if (str[i] == '~' && (i==0 || str[i-1]==' ')) {
            const char *home = sh_var_get("HOME");
            if (home) bi += snprintf(buf+bi, sizeof(buf)-bi, "%s", home);
            else buf[bi++] = '~';
        } else {
            buf[bi++] = str[i];
        }
        if (bi >= (int)sizeof(buf)-2) break;
    }
    buf[bi] = '\0';
    return xstrdup(buf);
}

/* ─── History ────────────────────────────────────────────── */
static void add_history(const char *line) {
    if (!line || !*line) return;
    /* Avoid duplicates */
    if (hist_count > 0 && strcmp(history[(hist_pos-1+MAX_HISTORY)%MAX_HISTORY], line)==0)
        return;
    free(history[hist_pos]);
    history[hist_pos] = xstrdup(line);
    hist_pos = (hist_pos + 1) % MAX_HISTORY;
    if (hist_count < MAX_HISTORY) hist_count++;
}

static void show_history(void) {
    int start = (hist_pos - hist_count + MAX_HISTORY) % MAX_HISTORY;
    for (int i = 0; i < hist_count; i++) {
        int idx = (start + i) % MAX_HISTORY;
        printf(C_YELLOW "%4d" C_RESET "  %s\n", i+1, history[idx]);
    }
}

/* ─── Alias management ───────────────────────────────────── */
static char *alias_lookup(const char *name) {
    for (int i = 0; i < alias_count; i++)
        if (strcmp(aliases[i].name, name) == 0)
            return aliases[i].expansion;
    return NULL;
}

static void alias_set(const char *name, const char *expansion) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            free(aliases[i].expansion);
            aliases[i].expansion = xstrdup(expansion);
            return;
        }
    }
    if (alias_count < MAX_ALIASES) {
        aliases[alias_count].name      = xstrdup(name);
        aliases[alias_count].expansion = xstrdup(expansion);
        alias_count++;
    }
}

static void alias_unset(const char *name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            free(aliases[i].name);
            free(aliases[i].expansion);
            aliases[i] = aliases[--alias_count];
            return;
        }
    }
}

static void alias_list(void) {
    for (int i = 0; i < alias_count; i++)
        printf("alias %s='%s'\n", aliases[i].name, aliases[i].expansion);
}

/* ─── Job control ────────────────────────────────────────── */
static void handle_sigchld(int sig) {
    (void)sig;
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == pid) {
                jobs[i].running = 0;
                printf("\n[%d] Done\t%s\n", jobs[i].id, jobs[i].cmd);
                break;
            }
        }
    }
}

static void handle_sigint(int sig) {
    (void)sig;
    printf("\n");
}

static int add_job(pid_t pid, const char *cmd) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].running) {
            jobs[i].pid     = pid;
            jobs[i].id      = i + 1;
            jobs[i].running = 1;
            strncpy(jobs[i].cmd, cmd, 255);
            if (i >= job_count) job_count = i + 1;
            return i + 1;
        }
    }
    return -1;
}

static void list_jobs(void) {
    for (int i = 0; i < job_count; i++)
        if (jobs[i].running)
            printf("[%d] Running\t%s\n", jobs[i].id, jobs[i].cmd);
}

/* ─── Token splitter (respects quotes) ───────────────────── */
static int tokenize(char *line, char **tokens, int max_tokens) {
    int count = 0;
    char *p = line;
    while (*p && count < max_tokens - 1) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (*p == '"') {
            p++;
            char buf[SH_INPUT_MAX*4]; int bi = 0;
            while (*p && *p != '"') {
                if (*p == '\\' && *(p+1) && *(p+1) != '"' && *(p+1) != '\\' && *(p+1) != '$') {
                    /* Preserve backslash escape for echo -e etc */
                    if (bi < (int)sizeof(buf)-2) { buf[bi++] = '\\'; }
                    if (bi < (int)sizeof(buf)-1) buf[bi++] = *(p+1);
                    p += 2;
                } else if (*p == '\\' && *(p+1)) {
                    if (bi < (int)sizeof(buf)-1) buf[bi++] = *(p+1);
                    p += 2;
                } else {
                    if (bi < (int)sizeof(buf)-1) buf[bi++] = *p;
                    p++;
                }
            }
            buf[bi] = '\0';
            if (*p == '"') p++;
            tokens[count++] = xstrdup(buf);
        } else if (*p == '\'') {
            p++;
            char buf[SH_INPUT_MAX*4]; int bi = 0;
            while (*p && *p != '\'') {
                if (bi < (int)sizeof(buf)-1) buf[bi++] = *p;
                p++;
            }
            buf[bi] = '\0';
            if (*p == '\'') p++;
            tokens[count++] = xstrdup(buf);
        } else if (*p == '>' || *p == '<') {
            /* Check for 2> (stderr redirect): look back for '2' token */
            char op[4] = {*p, '\0', '\0', '\0'};
            if (*p == '>' && *(p+1) == '>') { op[1]='>'; p++; }
            p++;
            tokens[count++] = xstrdup(op);
        } else if (*p == '|' || *p == '&' || *p == ';') {
            char op[3] = {*p, '\0', '\0'};
            p++;
            tokens[count++] = xstrdup(op);
        } else {
            char *tok_start = p;
            while (*p && !isspace((unsigned char)*p) &&
                   *p != '|' && *p != '>' && *p != '<' && *p != '&' && *p != ';' &&
                   *p != '"' && *p != '\'')
                p++;
            char saved = *p; *p = '\0';
            tokens[count++] = xstrdup(tok_start);
            *p = saved;
        }
    }
    tokens[count] = NULL;
    return count;
}

/* ─── Prompt ─────────────────────────────────────────────── */
static void print_prompt(void) {
    const char *user = sh_var_get("USER");
    if (!user) user = "user";
    const char *host_env = sh_var_get("HOSTNAME");
    char host[64] = "toolsh";
    if (host_env) strncpy(host, host_env, 63);
    else gethostname(host, sizeof(host)-1);

    getcwd(cwd, sizeof(cwd));
    const char *home = sh_var_get("HOME");
    char display_cwd[PATH_MAX];
    if (home && strncmp(cwd, home, strlen(home)) == 0) {
        snprintf(display_cwd, sizeof(display_cwd), "~%s", cwd + strlen(home));
    } else {
        strncpy(display_cwd, cwd, sizeof(display_cwd)-1);
    }

    printf(C_BOLD C_GREEN "%s@%s" C_RESET ":" C_BOLD C_BLUE "%s" C_RESET
           C_BOLD " $ " C_RESET, user, host, display_cwd);
    fflush(stdout);
}

/* ─── Interactive line reader with history ───────────────── */
static char *read_line_interactive(void) {
    static char line[SH_INPUT_MAX];
    static char saved[SH_INPUT_MAX];
    int pos = 0, len = 0;
    int hist_nav = 0, hist_idx = hist_count;

    struct termios old_t, raw_t;
    if (tcgetattr(STDIN_FILENO, &old_t) < 0) {
        /* Non-terminal fallback */
        if (!fgets(line, SH_INPUT_MAX, stdin)) return NULL;
        line[strcspn(line, "\n")] = '\0';
        return line;
    }

    raw_t = old_t;
    raw_t.c_lflag &= ~(ICANON | ECHO);
    raw_t.c_cc[VMIN]  = 1;
    raw_t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_t);

    memset(line, 0, sizeof(line));

    while (1) {
        unsigned char c;
        if (read(STDIN_FILENO, &c, 1) <= 0) break;

        if (c == '\n' || c == '\r') {
            write(STDOUT_FILENO, "\n", 1);
            break;
        } else if (c == 127 || c == 8) { /* Backspace */
            if (pos > 0) {
                memmove(line+pos-1, line+pos, len-pos);
                pos--; len--;
                line[len] = '\0';
                /* Redraw */
                printf("\r\033[K");
                print_prompt();
                printf("%s", line);
                /* Move cursor back */
                int back = len - pos;
                if (back) printf("\033[%dD", back);
                fflush(stdout);
            }
        } else if (c == '\033') { /* Escape sequence */
            unsigned char seq[3];
            if (read(STDIN_FILENO, &seq[0], 1) <= 0) break;
            if (read(STDIN_FILENO, &seq[1], 1) <= 0) break;
            if (seq[0] == '[') {
                if (seq[1] == 'A') { /* Up arrow - history */
                    if (!hist_nav) { strncpy(saved, line, SH_INPUT_MAX-1); hist_nav=1; }
                    if (hist_idx > 0 && hist_count > 0) {
                        hist_idx--;
                        int real_idx = (hist_pos - hist_count + hist_idx + MAX_HISTORY) % MAX_HISTORY;
                        strncpy(line, history[real_idx], SH_INPUT_MAX-1);
                        len = pos = strlen(line);
                        printf("\r\033[K"); print_prompt(); printf("%s", line); fflush(stdout);
                    }
                } else if (seq[1] == 'B') { /* Down arrow */
                    if (hist_nav && hist_idx < hist_count) {
                        hist_idx++;
                        if (hist_idx == hist_count) {
                            strncpy(line, saved, SH_INPUT_MAX-1);
                        } else {
                            int real_idx = (hist_pos - hist_count + hist_idx + MAX_HISTORY) % MAX_HISTORY;
                            strncpy(line, history[real_idx], SH_INPUT_MAX-1);
                        }
                        len = pos = strlen(line);
                        printf("\r\033[K"); print_prompt(); printf("%s", line); fflush(stdout);
                    }
                } else if (seq[1] == 'C') { /* Right */
                    if (pos < len) { pos++; printf("\033[1C"); fflush(stdout); }
                } else if (seq[1] == 'D') { /* Left */
                    if (pos > 0) { pos--; printf("\033[1D"); fflush(stdout); }
                } else if (seq[1] == 'H' || seq[1] == '1') { /* Home */
                    if (pos > 0) { printf("\033[%dD", pos); pos = 0; fflush(stdout); }
                } else if (seq[1] == 'F' || seq[1] == '4') { /* End */
                    if (pos < len) { printf("\033[%dC", len-pos); pos = len; fflush(stdout); }
                } else if (seq[1] == '3') { /* Delete */
                    unsigned char tmp; read(STDIN_FILENO, &tmp, 1); /* consume ~ */
                    if (pos < len) {
                        memmove(line+pos, line+pos+1, len-pos);
                        len--; line[len] = '\0';
                        printf("\r\033[K"); print_prompt(); printf("%s", line);
                        if (len-pos) printf("\033[%dD", len-pos);
                        fflush(stdout);
                    }
                }
            }
        } else if (c == 4) { /* Ctrl-D */
            if (len == 0) {
                tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
                return NULL;
            }
        } else if (c == 3) { /* Ctrl-C */
            printf("^C\n");
            tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
            print_prompt();
            memset(line, 0, sizeof(line));
            pos = len = 0;
            /* re-enter raw */
            tcsetattr(STDIN_FILENO, TCSANOW, &raw_t);
        } else if (c == 12) { /* Ctrl-L clear */
            printf("\033[2J\033[H");
            print_prompt(); printf("%s", line);
            if (len-pos) printf("\033[%dD", len-pos);
            fflush(stdout);
        } else if (c == 21) { /* Ctrl-U kill line */
            memset(line, 0, sizeof(line)); pos = len = 0;
            printf("\r\033[K"); print_prompt(); fflush(stdout);
        } else if (c == 1) { /* Ctrl-A */
            if (pos > 0) { printf("\033[%dD", pos); pos = 0; fflush(stdout); }
        } else if (c == 5) { /* Ctrl-E */
            if (pos < len) { printf("\033[%dC", len-pos); pos = len; fflush(stdout); }
        } else if (c == 9) { /* Tab - basic completion */
            /* simple: do nothing for now */
        } else if (c >= 32 && c < 127) {
            if (len < SH_INPUT_MAX - 2) {
                memmove(line+pos+1, line+pos, len-pos);
                line[pos] = c; pos++; len++;
                line[len] = '\0';
                /* rewrite from cursor */
                printf("\033[s"); /* save */
                printf("%s", line+pos-1);
                printf("\033[u\033[1C"); /* restore, move right */
                fflush(stdout);
            }
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
    return line;
}

/* ─── Parse a single command segment ─────────────────────── */
static void parse_command(char *seg, Command *cmd) {
    memset(cmd, 0, sizeof(*cmd));
    char *tokens[MAX_ARGS];
    int tc = tokenize(seg, tokens, MAX_ARGS);

    char **argv = calloc(MAX_ARGS, sizeof(char*));
    int argc = 0;

    for (int i = 0; i < tc; i++) {
        char *t = tokens[i];
        /* Check for N> or N>> fd redirects (e.g. 2>/dev/null) */
        int is_fd_redir = 0;
        if (strlen(t) == 1 && isdigit((unsigned char)t[0]) && i+1 < tc) {
            char *next = tokens[i+1];
            if (strcmp(next,">")==0 || strcmp(next,">>")==0) {
                int fd = t[0] - '0';
                int append = (next[1]=='>');
                free(t); free(tokens[i+1]); i += 2;
                if (i < tc) {
                    char *fname = tokens[i];
                    if (fd == 2) cmd->errfile = fname;
                    else if (!append) cmd->outfile = fname;
                    else cmd->appendfile = fname;
                }
                is_fd_redir = 1;
            }
        }
        if (is_fd_redir) continue;
        if ((strcmp(t,">")==0 || strcmp(t,">>")==0 || strcmp(t,"<")==0) && i+1<tc) {
            char op = t[0]; int app = (t[1]=='>');
            free(t);
            i++;
            char *fname = tokens[i];
            if (op=='>' && !app) cmd->outfile    = fname;
            else if (app)        cmd->appendfile  = fname;
            else                 cmd->infile      = fname;
        } else if (strcmp(t,"&")==0) {
            cmd->background = 1;
            free(t);
        } else {
            char *expanded = expand_variables(t);
            free(t);
            argv[argc++] = expanded;
        }
    }
    argv[argc] = NULL;

    /* Resolve ./tool → tool */
    if (argc > 0) {
        char *resolved = resolve_command(argv[0]);
        free(argv[0]);
        argv[0] = resolved;
        /* Check alias */
        char *al = alias_lookup(argv[0]);
        if (al) {
            char *tokens2[MAX_ARGS];
            char tmp[SH_INPUT_MAX*4];
            strncpy(tmp, al, sizeof(tmp)-1);
            int atc = tokenize(tmp, tokens2, MAX_ARGS);
            char **new_argv = calloc(MAX_ARGS, sizeof(char*));
            int new_argc = 0;
            for (int j = 0; j < atc; j++) new_argv[new_argc++] = tokens2[j];
            for (int j = 1; j < argc; j++) new_argv[new_argc++] = xstrdup(argv[j]);
            new_argv[new_argc] = NULL;
            for (int j = 0; j < argc; j++) free(argv[j]);
            free(argv);
            argv = new_argv;
            argc = new_argc;
        }
    }

    cmd->argv = argv;
    cmd->argc = argc;
}

/* ─── Built-in commands ──────────────────────────────────── */
static int builtin_cd(char **argv) {
    const char *dir = argv[1];
    if (!dir) dir = sh_var_get("HOME");
    if (!dir) { fprintf(stderr, "cd: no home\n"); return 1; }
    if (strcmp(dir,"-")==0) {
        const char *oldpwd = sh_var_get("OLDPWD");
        if (!oldpwd) { fprintf(stderr, "cd: OLDPWD not set\n"); return 1; }
        dir = oldpwd;
        printf("%s\n", dir);
    }
    char old[PATH_MAX];
    getcwd(old, sizeof(old));
    if (chdir(dir) != 0) { perror("cd"); return 1; }
    sh_var_set("OLDPWD", old);
    getcwd(cwd, sizeof(cwd));
    sh_var_set("PWD", cwd);
    return 0;
}

static int builtin_export(char **argv) {
    for (int i = 1; argv[i]; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            sh_var_set(argv[i], eq+1);
            setenv(argv[i], eq+1, 1);
            *eq = '=';
        } else {
            char *val = sh_var_get(argv[i]);
            if (val) setenv(argv[i], val, 1);
        }
    }
    return 0;
}

static int builtin_set(char **argv) {
    if (!argv[1]) {
        /* Print all variables */
        for (int i = 0; i < var_count; i++)
            printf("%s=%s\n", variables[i].name, variables[i].value);
        return 0;
    }
    /* set name=value */
    char *eq = strchr(argv[1], '=');
    if (eq) {
        char name[256];
        strncpy(name, argv[1], eq - argv[1]);
        name[eq - argv[1]] = '\0';
        sh_var_set(name, eq + 1);
    }
    return 0;
}

static int builtin_echo(char **argv) {
    int no_newline = 0, interpret = 0;
    int start = 1;
    for (int i = 1; argv[i] && argv[i][0]=='-'; i++) {
        char *opt = argv[i]+1;
        if (*opt == '\0') break;
        int valid = 1;
        int local_n = 0, local_e = 0;
        for (char *c = opt; *c; c++) {
            if (*c == 'n') local_n = 1;
            else if (*c == 'e') local_e = 1;
            else { valid = 0; break; }
        }
        if (!valid) break;
        if (local_n) no_newline = 1;
        if (local_e) interpret = 1;
        start++;
    }
    for (int i = start; argv[i]; i++) {
        if (i > start) putchar(' ');
        if (interpret) {
            for (char *c = argv[i]; *c; c++) {
                if (*c == '\\' && *(c+1)) {
                    c++;
                    switch (*c) {
                        case 'n': putchar('\n'); break;
                        case 't': putchar('\t'); break;
                        case 'r': putchar('\r'); break;
                        case 'a': putchar('\a'); break;
                        case 'b': putchar('\b'); break;
                        case '\\': putchar('\\'); break;
                        case 'e': putchar('\033'); break;
                        case '0': putchar('\0'); break;
                        default: putchar('\\'); putchar(*c); break;
                    }
                } else {
                    putchar(*c);
                }
            }
        } else {
            printf("%s", argv[i]);
        }
    }
    if (!no_newline) putchar('\n');
    fflush(stdout);
    return 0;
}

static int builtin_alias(char **argv) {
    if (!argv[1]) { alias_list(); return 0; }
    for (int i = 1; argv[i]; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            char name[256];
            int nl = eq - argv[i];
            strncpy(name, argv[i], nl); name[nl] = '\0';
            char *val = eq + 1;
            /* Strip surrounding single or double quotes */
            int vl = strlen(val);
            if (vl >= 2 && ((val[0]=='\'' && val[vl-1]=='\'')||(val[0]=='"' && val[vl-1]=='"'))) {
                char *stripped = xstrdup(val + 1);
                stripped[vl - 2] = '\0';
                alias_set(name, stripped);
                free(stripped);
            } else {
                /* Collect remaining args as part of the value (space-separated) */
                char full_val[SH_INPUT_MAX*4];
                strncpy(full_val, val, sizeof(full_val)-1);
                /* Append any remaining argv tokens (from space in quoted alias) */
                for (int j = i+1; argv[j]; j++) {
                    strncat(full_val, " ", sizeof(full_val)-strlen(full_val)-1);
                    strncat(full_val, argv[j], sizeof(full_val)-strlen(full_val)-1);
                    i = j; /* skip these tokens */
                }
                alias_set(name, full_val);
            }
        } else {
            char *exp = alias_lookup(argv[i]);
            if (exp) printf("alias %s='%s'\n", argv[i], exp);
            else { fprintf(stderr, "alias: %s: not found\n", argv[i]); return 1; }
        }
    }
    return 0;
}

static int builtin_unalias(char **argv) {
    for (int i = 1; argv[i]; i++) alias_unset(argv[i]);
    return 0;
}

static int builtin_type(char **argv) {
    for (int i = 1; argv[i]; i++) {
        const char *n = argv[i];
        if (alias_lookup(n)) { printf("%s is aliased to '%s'\n", n, alias_lookup(n)); continue; }
        if (strcmp(n,"cd")==0||strcmp(n,"exit")==0||strcmp(n,"help")==0||
            strcmp(n,"history")==0||strcmp(n,"export")==0||strcmp(n,"echo")==0||
            strcmp(n,"alias")==0||strcmp(n,"unalias")==0||strcmp(n,"set")==0||
            strcmp(n,"unset")==0||strcmp(n,"jobs")==0||strcmp(n,"source")==0||
            strcmp(n,".")==0||strcmp(n,"type")==0||strcmp(n,"pwd")==0||
            strcmp(n,"true")==0||strcmp(n,"false")==0||strcmp(n,"tools")==0) {
            printf("%s is a shell builtin\n", n);
            continue;
        }
        /* Search PATH */
        char *path_env = sh_var_get("PATH");
        int found = 0;
        if (path_env) {
            char paths[4096]; strncpy(paths, path_env, 4095);
            char *tok = strtok(paths, ":");
            while (tok) {
                char full[PATH_MAX];
                snprintf(full, sizeof(full), "%s/%s", tok, n);
                if (access(full, X_OK) == 0) {
                    printf("%s is %s\n", n, full);
                    found = 1; break;
                }
                tok = strtok(NULL, ":");
            }
        }
        if (!found) printf("%s: not found\n", n);
    }
    return 0;
}

static int builtin_source(const char *file) {
    FILE *f = fopen(file, "r");
    if (!f) { perror(file); return 1; }
    char line[SH_INPUT_MAX];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line,"\n")] = '\0';
        if (*line && line[0] != '#') run_pipeline(line);
    }
    fclose(f);
    return 0;
}

static int builtin_help(void) {
    printf(C_BOLD C_CYAN "\n  toolsh — Advanced Linux Shell\n" C_RESET);
    printf("  ─────────────────────────────\n");
    printf("  Built-in commands:\n");
    printf("  " C_YELLOW "cd" C_RESET " [dir]        Change directory\n");
    printf("  " C_YELLOW "echo" C_RESET " [-n|-e]     Print text\n");
    printf("  " C_YELLOW "export" C_RESET " VAR=val    Export variable\n");
    printf("  " C_YELLOW "set" C_RESET " [VAR=val]    Set variable / show vars\n");
    printf("  " C_YELLOW "unset" C_RESET " VAR         Unset variable\n");
    printf("  " C_YELLOW "alias" C_RESET " [n=val]     Manage aliases\n");
    printf("  " C_YELLOW "unalias" C_RESET " name       Remove alias\n");
    printf("  " C_YELLOW "history" C_RESET "            Show command history\n");
    printf("  " C_YELLOW "jobs" C_RESET "               List background jobs\n");
    printf("  " C_YELLOW "source" C_RESET " / " C_YELLOW "." C_RESET " file  Run script in shell\n");
    printf("  " C_YELLOW "type" C_RESET " cmd           Show command type\n");
    printf("  " C_YELLOW "tools" C_RESET "              List available tools (coreutils/bin/<name>)\n");
    printf("  " C_YELLOW "help" C_RESET "               This message\n");
    printf("  " C_YELLOW "exit" C_RESET " [code]        Exit shell\n");
    printf("\n  Features: pipes (|), redirection (> >> <),\n");
    printf("  background (&), variables ($VAR), history (↑↓),\n");
    printf("  semicolons (;), and coreutils/bin/tool → tool translation.\n\n");
    return 0;
}

static int execute_builtin(char **argv, int argc) {
    if (!argv[0]) return -1;
    const char *cmd = argv[0];

    if (strcmp(cmd,"cd")==0)        return builtin_cd(argv);
    if (strcmp(cmd,"exit")==0||strcmp(cmd,"quit")==0) {
        int code = argc>1 ? atoi(argv[1]) : last_exit;
        printf(C_YELLOW "Goodbye!\n" C_RESET);
        exit(code);
    }
    if (strcmp(cmd,"echo")==0)      return builtin_echo(argv);
    if (strcmp(cmd,"help")==0)      { builtin_help(); return 0; }
    if (strcmp(cmd,"history")==0)   { show_history(); return 0; }
    if (strcmp(cmd,"export")==0)    return builtin_export(argv);
    if (strcmp(cmd,"set")==0)       return builtin_set(argv);
    if (strcmp(cmd,"unset")==0)     { for(int i=1;argv[i];i++) sh_var_unset(argv[i]); return 0; }
    if (strcmp(cmd,"alias")==0)     return builtin_alias(argv);
    if (strcmp(cmd,"unalias")==0)   return builtin_unalias(argv);
    if (strcmp(cmd,"jobs")==0)      { list_jobs(); return 0; }
    if (strcmp(cmd,"type")==0)      return builtin_type(argv);
    if (strcmp(cmd,"tools")==0)     { print_tools(); return 0; }
    if (strcmp(cmd,"pwd")==0)       { getcwd(cwd,sizeof(cwd)); printf("%s\n",cwd); return 0; }
    if (strcmp(cmd,"true")==0)      return 0;
    if (strcmp(cmd,"false")==0)     return 1;
    if (strcmp(cmd,"source")==0||strcmp(cmd,".")==0) {
        if (!argv[1]) { fprintf(stderr, "source: filename required\n"); return 1; }
        return builtin_source(argv[1]);
    }
    if (strcmp(cmd,"clear")==0)     { printf("\033[2J\033[H"); fflush(stdout); return 0; }

    /* Variable assignment: NAME=value */
    if (strchr(cmd,'=') && cmd[0]!='=') {
        char name[256];
        char *eq = strchr(cmd,'=');
        int nl = eq - cmd;
        strncpy(name, cmd, nl); name[nl]='\0';
        /* Valid variable name? */
        int valid = 1;
        for (int i=0;i<nl;i++)
            if (!isalnum((unsigned char)name[i])&&name[i]!='_') { valid=0; break; }
        if (valid) {
            sh_var_set(name, eq+1);
            return 0;
        }
    }

    return -1; /* not a builtin */
}

/* ─── Execute a single Command struct ────────────────────── */
static int exec_command(Command *cmd, int in_fd, int out_fd) {
    if (cmd->argc == 0) return 0;

    /* Try builtins (only if not piped) */
    if (in_fd == STDIN_FILENO && out_fd == STDOUT_FILENO) {
        int r = execute_builtin(cmd->argv, cmd->argc);
        if (r >= 0) return r;
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        /* Child */
        if (in_fd != STDIN_FILENO)   { dup2(in_fd, STDIN_FILENO);  close(in_fd); }
        if (out_fd != STDOUT_FILENO) { dup2(out_fd, STDOUT_FILENO); close(out_fd); }

        /* Redirections */
        if (cmd->infile) {
            int fd = open(cmd->infile, O_RDONLY);
            if (fd<0) { perror(cmd->infile); exit(1); }
            dup2(fd, STDIN_FILENO); close(fd);
        }
        if (cmd->outfile) {
            int fd = open(cmd->outfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
            if (fd<0) { perror(cmd->outfile); exit(1); }
            dup2(fd, STDOUT_FILENO); close(fd);
        }
        if (cmd->appendfile) {
            int fd = open(cmd->appendfile, O_WRONLY|O_CREAT|O_APPEND, 0644);
            if (fd<0) { perror(cmd->appendfile); exit(1); }
            dup2(fd, STDOUT_FILENO); close(fd);
        }

        exec_with_fallback(cmd->argv);


    }

    return pid;
}

/* ─── Run a pipeline ─────────────────────────────────────── */
static int run_pipeline(char *line) {
    if (!line || !*line) return 0;
    line = trim(line);
    if (!*line || line[0]=='#') return 0;

    /* Handle semicolons: split into multiple commands */
    /* But not inside quotes */
    int in_sq=0, in_dq=0;
    for (char *p=line; *p; p++) {
        if (*p=='\'' && !in_dq) in_sq=!in_sq;
        if (*p=='"'  && !in_sq) in_dq=!in_dq;
        if (*p==';' && !in_sq && !in_dq) {
            *p = '\0';
            run_pipeline(line);
            run_pipeline(p+1);
            return last_exit;
        }
    }

    /* Check for && and || */
    in_sq=0; in_dq=0;
    for (int i=0; line[i]; i++) {
        if (line[i]=='\'' && !in_dq) in_sq=!in_sq;
        if (line[i]=='"'  && !in_sq) in_dq=!in_dq;
        if (!in_sq && !in_dq) {
            if (line[i]=='&' && line[i+1]=='&') {
                line[i]='\0';
                int r = run_pipeline(line);
                if (r==0) return run_pipeline(line+i+2);
                return r;
            }
            if (line[i]=='|' && line[i+1]=='|') {
                line[i]='\0';
                int r = run_pipeline(line);
                if (r!=0) return run_pipeline(line+i+2);
                return r;
            }
        }
    }

    /* Background? */
    int background = 0;
    int ll = strlen(line);
    while (ll > 0 && isspace((unsigned char)line[ll-1])) ll--;
    if (ll > 0 && line[ll-1]=='&') {
        background = 1;
        line[ll-1] = '\0';
        trim(line);
    }

    /* Split on pipes */
    char *segments[MAX_PIPES];
    int nseg = 0;
    in_sq=0; in_dq=0;
    char *start = line, *p = line;
    for (; *p; p++) {
        if (*p=='\'' && !in_dq) in_sq=!in_sq;
        if (*p=='"'  && !in_sq) in_dq=!in_dq;
        if (*p=='|' && !in_sq && !in_dq && *(p+1)!='|') {
            *p = '\0';
            segments[nseg++] = start;
            start = p+1;
        }
    }
    segments[nseg++] = start;

    /* Parse each segment */
    Command cmds[MAX_PIPES];
    for (int i=0; i<nseg; i++) {
        parse_command(segments[i], &cmds[i]);
        if (background && i==nseg-1) cmds[i].background=1;
    }

    if (nseg == 1) {
        /* Simple command */
        Command *cmd = &cmds[0];
        if (cmd->argc == 0) return 0;

        /* Try builtin first */
        int r = execute_builtin(cmd->argv, cmd->argc);
        if (r >= 0) { last_exit = r; return r; }

        /* External command */
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            if (cmd->infile) {
                int fd = open(cmd->infile, O_RDONLY);
                if (fd<0) { perror(cmd->infile); exit(1); }
                dup2(fd, STDIN_FILENO); close(fd);
            }
            if (cmd->outfile) {
                int fd = open(cmd->outfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                if (fd<0) { perror(cmd->outfile); exit(1); }
                dup2(fd, STDOUT_FILENO); close(fd);
            }
            if (cmd->appendfile) {
                int fd = open(cmd->appendfile, O_WRONLY|O_CREAT|O_APPEND, 0644);
                if (fd<0) { perror(cmd->appendfile); exit(1); }
                dup2(fd, STDOUT_FILENO); close(fd);
            }
            if (cmd->errfile) {
                int fd = open(cmd->errfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                if (fd<0) { perror(cmd->errfile); exit(1); }
                dup2(fd, STDERR_FILENO); close(fd);
            }
            if (cmd->background) {
                setsid();
            }
            exec_with_fallback(cmd->argv);
        }

        if (cmd->background) {
            int jid = add_job(pid, line);
            printf("[%d] %d\n", jid, pid);
        } else {
            int status;
            waitpid(pid, &status, 0);
            last_exit = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        }
        return last_exit;
    }

    /* Pipeline with multiple stages */
    int pipes[MAX_PIPES][2];
    pid_t pids[MAX_PIPES];

    for (int i=0; i<nseg-1; i++) {
        if (pipe(pipes[i]) < 0) { perror("pipe"); return 1; }
    }

    for (int i=0; i<nseg; i++) {
        pid_t pid = fork();
        if (pid < 0) { perror("fork"); return 1; }
        if (pid == 0) {
            /* Set up input */
            if (i > 0) {
                dup2(pipes[i-1][0], STDIN_FILENO);
                close(pipes[i-1][0]);
            }
            /* Set up output */
            if (i < nseg-1) {
                dup2(pipes[i][1], STDOUT_FILENO);
                close(pipes[i][1]);
            }
            /* Close all other pipe fds */
            for (int j=0; j<nseg-1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }
            /* Redirections */
            if (cmds[i].infile) {
                int fd = open(cmds[i].infile, O_RDONLY);
                if (fd<0) { perror(cmds[i].infile); exit(1); }
                dup2(fd, STDIN_FILENO); close(fd);
            }
            if (cmds[i].outfile) {
                int fd = open(cmds[i].outfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                if (fd<0) { perror(cmds[i].outfile); exit(1); }
                dup2(fd, STDOUT_FILENO); close(fd);
            }
            if (cmds[i].appendfile) {
                int fd = open(cmds[i].appendfile, O_WRONLY|O_CREAT|O_APPEND, 0644);
                if (fd<0) { perror(cmds[i].appendfile); exit(1); }
                dup2(fd, STDOUT_FILENO); close(fd);
            }
            if (cmds[i].errfile) {
                int fd = open(cmds[i].errfile, O_WRONLY|O_CREAT|O_TRUNC, 0644);
                if (fd<0) { perror(cmds[i].errfile); exit(1); }
                dup2(fd, STDERR_FILENO); close(fd);
            }
            exec_with_fallback(cmds[i].argv);
        }
        pids[i] = pid;
    }

    /* Parent: close all pipe fds */
    for (int i=0; i<nseg-1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    /* Wait for all children */
    int status = 0;
    for (int i=0; i<nseg; i++) {
        int s;
        waitpid(pids[i], &s, 0);
        if (i == nseg-1)
            status = WIFEXITED(s) ? WEXITSTATUS(s) : 1;
    }

    last_exit = status;
    return last_exit;
}

/* ─── Load rc file ───────────────────────────────────────── */
static void load_rc(void) {
    const char *home = sh_var_get("HOME");
    if (!home) return;
    char rcpath[PATH_MAX];
    snprintf(rcpath, sizeof(rcpath), "%s/.toolshrc", home);
    if (access(rcpath, R_OK) == 0)
        builtin_source(rcpath);
}

/* ─── Banner ─────────────────────────────────────────────── */
static void print_banner(void) {
    printf(C_BOLD C_CYAN
        "\n"
        "  ████████╗ ██████╗  ██████╗ ██╗     ███████╗██╗  ██╗\n"
        "     ██╔══╝██╔═══██╗██╔═══██╗██║     ██╔════╝██║  ██║\n"
        "     ██║   ██║   ██║██║   ██║██║     ███████╗███████║\n"
        "     ██║   ██║   ██║██║   ██║██║     ╚════██║██╔══██║\n"
        "     ██║   ╚██████╔╝╚██████╔╝███████╗███████║██║  ██║\n"
        "     ╚═╝    ╚═════╝  ╚═════╝ ╚══════╝╚══════╝╚═╝  ╚═╝\n"
        C_RESET);
    printf(C_YELLOW "  Advanced Linux Shell with ./tool syntax\n" C_RESET);
    printf("  Type " C_GREEN "help" C_RESET " for builtins, "
           C_GREEN "tools" C_RESET " to list available tools.\n");
    printf("  Use " C_GREEN "coreutils/bin/ls" C_RESET ", " C_GREEN "coreutils/bin/echo" C_RESET
           ", etc. — they map to system commands.\n\n");
}

/* ─── Main ───────────────────────────────────────────────── */
int main(int argc, char *argv[]) {
    shell_pid = getpid();
    interactive = isatty(STDIN_FILENO);

    /* Init signals */
    signal(SIGCHLD, handle_sigchld);
    signal(SIGINT,  handle_sigint);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);

    /* Init environment variables */
    sh_var_set("SHELL", "/usr/local/bin/toolsh");
    sh_var_set("TERM",  getenv("TERM") ? getenv("TERM") : "xterm-256color");

    const char *home = getenv("HOME");
    if (home) sh_var_set("HOME", home);

    const char *user = getenv("USER");
    if (user) sh_var_set("USER", user);
    else {
        struct passwd *pw = getpwuid(getuid());
        if (pw) sh_var_set("USER", pw->pw_name);
    }

    char *path = getenv("PATH");
    if (path) sh_var_set("PATH", path);
    else sh_var_set("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");

    getcwd(cwd, sizeof(cwd));
    sh_var_set("PWD", cwd);

    char pid_str[32];
    snprintf(pid_str, sizeof(pid_str), "%d", shell_pid);
    sh_var_set("$", pid_str);

    /* Default aliases */
    alias_set("ll",    "ls -la");
    alias_set("la",    "ls -A");
    alias_set("l",     "ls -CF");
    alias_set("grep",  "grep --color=auto");
    alias_set("egrep", "egrep --color=auto");
    alias_set("fgrep", "fgrep --color=auto");
    alias_set("..",    "cd ..");
    alias_set("...",   "cd ../.."); 

    /* Script mode */
    if (argc > 1) {
        if (strcmp(argv[1],"-c")==0 && argc>2) {
            interactive = 0;
            return run_pipeline(argv[2]);
        }
        /* Script file */
        interactive = 0;
        FILE *f = fopen(argv[1], "r");
        if (!f) { perror(argv[1]); return 1; }
        char line[SH_INPUT_MAX];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line,"\n")] = '\0';
            if (*line && line[0]!='#') run_pipeline(line);
        }
        fclose(f);
        return last_exit;
    }

    /* Interactive mode */
    if (interactive) {
        print_banner();
        load_rc();
    }

    while (1) {
        if (interactive) print_prompt();

        char *line;
        if (interactive) {
            line = read_line_interactive();
        } else {
            static char buf[SH_INPUT_MAX];
            line = fgets(buf, sizeof(buf), stdin);
            if (line) line[strcspn(line,"\n")] = '\0';
        }

        if (!line) {
            if (interactive) printf(C_YELLOW "\nGoodbye!\n" C_RESET);
            break;
        }

        char *trimmed = trim(line);
        if (!*trimmed) continue;

        /* History expansion: !! = last command */
        if (strcmp(trimmed,"!!")==0 && hist_count>0) {
            int idx = (hist_pos-1+MAX_HISTORY)%MAX_HISTORY;
            printf("%s\n", history[idx]);
            char tmp[SH_INPUT_MAX];
            strncpy(tmp, history[idx], SH_INPUT_MAX-1);
            add_history(tmp);
            run_pipeline(tmp);
            continue;
        }
        /* !N = Nth history entry */
        if (trimmed[0]=='!' && isdigit((unsigned char)trimmed[1])) {
            int n = atoi(trimmed+1) - 1;
            if (n>=0 && n<hist_count) {
                int idx=(hist_pos-hist_count+n+MAX_HISTORY)%MAX_HISTORY;
                printf("%s\n", history[idx]);
                char tmp[SH_INPUT_MAX];
                strncpy(tmp,history[idx],SH_INPUT_MAX-1);
                add_history(tmp);
                run_pipeline(tmp);
            } else fprintf(stderr,"toolsh: !%d: event not found\n",n+1);
            continue;
        }

        add_history(trimmed);
        run_pipeline(trimmed);
    }

    return last_exit;
}
