/*
 * sed - Stream Editor reimplemented from scratch in C
 *
 * Supported features:
 *   Commands:
 *     s/regex/replacement/[flags]  Substitute (flags: g, i, p, 1-9)
 *     d                            Delete line
 *     p                            Print line
 *     q [code]                     Quit (with optional exit code)
 *     Q [code]                     Quit without printing
 *     a\text  or  a text           Append text after line
 *     i\text  or  i text           Insert text before line
 *     c\text  or  c text           Change (replace) line
 *     y/src/dst/                   Transliterate characters
 *     =                            Print current line number
 *     l                            Print line unambiguously (escape special chars)
 *     r file                       Read file and append after line
 *     R file                       Read one line from file
 *     w file                       Write pattern space to file
 *     n                            Read next line (print current if -n not set)
 *     N                            Append next line to pattern space
 *     P                            Print up to first newline of pattern space
 *     D                            Delete up to first newline, restart cycle
 *     h / H                        Copy/Append pattern space to hold space
 *     g / G                        Copy/Append hold space to pattern space
 *     x                            Exchange pattern and hold space
 *     l                            Unambiguously print pattern space
 *     { }                          Group commands
 *     : label                      Define label
 *     b [label]                    Branch to label (or end of script)
 *     t [label]                    Branch if substitution made since last t/T
 *     T [label]                    Branch if no substitution made since last t/T
 *     #                            Comment
 *
 *   Address types:
 *     (none)                       Match every line
 *     n                            Line number
 *     $                            Last line
 *     /regex/                      Lines matching regex
 *     addr,addr                    Range (inclusive)
 *     addr~step                    First,step (e.g. 0~2 = even lines)
 *     addr!                        Negate
 *
 *   Options:
 *     -n                           Suppress auto-print
 *     -e script                    Add script
 *     -f file                      Read script from file
 *     -i[suffix]                   In-place edit (with optional backup suffix)
 *     -r / -E                      Extended regex (ERE)
 *
 * Usage: sed [options] [script] [file...]
 *
 * Build: gcc -O2 -Wall -o sed sed.c
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>

/* ── Dynamic string ──────────────────────────────────────────────────────── */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} Str;

static void str_init(Str *s) { s->buf = NULL; s->len = s->cap = 0; }

static void str_ensure(Str *s, size_t need) {
    if (s->cap >= need) return;
    size_t nc = s->cap ? s->cap * 2 : 64;
    while (nc < need) nc *= 2;
    s->buf = realloc(s->buf, nc);
    if (!s->buf) { perror("realloc"); exit(1); }
    s->cap = nc;
}

static void str_set(Str *s, const char *data, size_t len) {
    str_ensure(s, len + 1);
    memcpy(s->buf, data, len);
    s->buf[len] = '\0';
    s->len = len;
}

static void str_setc(Str *s, const char *c) { str_set(s, c, strlen(c)); }

static void str_append(Str *s, const char *data, size_t len) {
    str_ensure(s, s->len + len + 1);
    memcpy(s->buf + s->len, data, len);
    s->len += len;
    s->buf[s->len] = '\0';
}

static void str_appendc(Str *s, const char *c) { str_append(s, c, strlen(c)); }

static void str_appendch(Str *s, char c) { str_append(s, &c, 1); }

static void str_free(Str *s) { free(s->buf); str_init(s); }

/* ── Address ─────────────────────────────────────────────────────────────── */

typedef enum {
    ADDR_NONE,    /* matches every line */
    ADDR_LINE,    /* specific line number (0 = before first) */
    ADDR_LAST,    /* $ */
    ADDR_REGEX,   /* /pattern/ */
    ADDR_STEP,    /* first~step */
} AddrType;

typedef struct {
    AddrType type;
    long     line;          /* ADDR_LINE or first in ADDR_STEP */
    long     step;          /* ADDR_STEP */
    regex_t  re;
    int      re_valid;
} Addr;

/* ── Command ─────────────────────────────────────────────────────────────── */

typedef enum {
    CMD_SUBST,      /* s */
    CMD_DELETE,     /* d */
    CMD_PRINT,      /* p */
    CMD_PRINT_UNAMBIG, /* l */
    CMD_QUIT,       /* q */
    CMD_QUIT_SILENT,/* Q */
    CMD_APPEND,     /* a */
    CMD_INSERT,     /* i */
    CMD_CHANGE,     /* c */
    CMD_TRANSLIT,   /* y */
    CMD_LINENUM,    /* = */
    CMD_READ_FILE,  /* r */
    CMD_READ_LINE,  /* R */
    CMD_WRITE_FILE, /* w */
    CMD_NEXT,       /* n */
    CMD_NEXT_APPEND,/* N */
    CMD_PRINT_FIRST,/* P */
    CMD_DELETE_FIRST,/* D */
    CMD_HOLD_COPY,  /* h */
    CMD_HOLD_APPEND,/* H */
    CMD_GET_COPY,   /* g */
    CMD_GET_APPEND, /* G */
    CMD_EXCHANGE,   /* x */
    CMD_LABEL,      /* : */
    CMD_BRANCH,     /* b */
    CMD_BRANCH_TRUE,/* t */
    CMD_BRANCH_FALSE,/* T */
    CMD_BLOCK_START,/* { */
    CMD_BLOCK_END,  /* } */
    CMD_COMMENT,    /* # */
} CmdType;

typedef struct Cmd Cmd;

typedef struct {
    regex_t  re;
    int      re_valid;
    char    *replacement;   /* with & and \1..\9 references */
    int      global;        /* g flag */
    int      icase;         /* i flag */
    int      print;         /* p flag */
    int      nth;           /* nth occurrence (0 = all if global, else 1) */
} SubstCmd;

typedef struct {
    char src[256];
    char dst[256];
    int  len;
} TransCmd;

struct Cmd {
    CmdType  type;
    Addr     addr[2];
    int      naddr;         /* 0, 1, or 2 */
    int      negate;        /* ! */
    int      block_end;     /* index of matching } for { */

    SubstCmd subst;
    TransCmd trans;
    char    *text;          /* a, i, c, r, R, w, b, t, T, : */
    int      ival;          /* q exit code */

    Cmd     *next;          /* linked list */
};

/* ── Global state ────────────────────────────────────────────────────────── */

static int    g_suppress  = 0;   /* -n */
static int    g_ere       = 0;   /* -E / -r */
static int    g_inplace   = 0;   /* -i */
static const char *g_inplace_suffix = NULL;

/* Script is stored as array of Cmd for easy random-access (branch) */
static Cmd  **g_cmds      = NULL;
static int    g_ncmds     = 0;
static int    g_ccmds     = 0;

static Str    g_pattern;         /* pattern space */
static Str    g_hold;            /* hold space */
static long   g_lineno    = 0;
static int    g_last_line = 0;
static int    g_subst_flag= 0;   /* substitution made since last t/T */
static int    g_quit      = 0;
static int    g_quit_code = 0;

/* Deferred output after current line (a, r, R commands) */
static Str    g_deferred;

/* ── Script source management ────────────────────────────────────────────── */

/* Concatenated script text */
static Str    g_script;

/* ── Regex helpers ───────────────────────────────────────────────────────── */

static regex_t g_last_re;
static int     g_last_re_valid = 0;

static int re_flags(void) {
    return g_ere ? REG_EXTENDED : 0;
}

static void compile_re(regex_t *re, const char *pat, int extra_flags) {
    int r = regcomp(re, pat, re_flags() | REG_NEWLINE | extra_flags);
    if (r) {
        char buf[256];
        regerror(r, re, buf, sizeof buf);
        fprintf(stderr, "sed: regex error: %s\n", buf);
        exit(1);
    }
}

/* ── Parser ──────────────────────────────────────────────────────────────── */

static const char *g_pos;   /* current parse position in script */

static void skip_ws_no_nl(void) {
    while (*g_pos == ' ' || *g_pos == '\t') g_pos++;
}

static void skip_ws(void) {
    while (*g_pos && (*g_pos == ' ' || *g_pos == '\t' || *g_pos == '\n' || *g_pos == ';'))
        g_pos++;
}

/* Read a delimited string, returning allocated copy; advances g_pos past closing delim */
static char *read_delimited(char delim) __attribute__((unused));
static char *read_delimited(char delim) {
    Str s;
    str_init(&s);
    while (*g_pos && *g_pos != delim) {
        if (*g_pos == '\\' && *(g_pos+1)) {
            g_pos++;
            switch (*g_pos) {
            case 'n': str_appendch(&s, '\n'); break;
            case 't': str_appendch(&s, '\t'); break;
            case '\\': str_appendch(&s, '\\'); break;
            default:
                str_appendch(&s, '\\');
                str_appendch(&s, *g_pos);
                break;
            }
        } else {
            str_appendch(&s, *g_pos);
        }
        g_pos++;
    }
    if (*g_pos == delim) g_pos++;  /* skip closing delim */
    char *r = s.buf ? s.buf : strdup("");
    if (!s.buf) str_free(&s);
    return r;
}

/* Parse one address; returns 1 if an address was parsed */
static int parse_addr(Addr *a) {
    memset(a, 0, sizeof *a);
    skip_ws_no_nl(  );
    if (*g_pos == '$') {
        a->type = ADDR_LAST;
        g_pos++;
        return 1;
    }
    if (isdigit((unsigned char)*g_pos)) {
        a->type = ADDR_LINE;
        a->line = strtol(g_pos, (char **)&g_pos, 10);
        /* check for step: first~step */
        if (*g_pos == '~') {
            g_pos++;
            a->step = strtol(g_pos, (char **)&g_pos, 10);
            a->type = ADDR_STEP;
        }
        return 1;
    }
    if (*g_pos == '/' || *g_pos == '\\') {
        char delim = '/';
        if (*g_pos == '\\') { g_pos++; delim = *g_pos; }
        g_pos++;
        Str pat; str_init(&pat);
        while (*g_pos && *g_pos != delim) {
            if (*g_pos == '\\' && *(g_pos+1)) {
                g_pos++;
                if (*g_pos == 'n') str_appendch(&pat, '\n');
                else { str_appendch(&pat, '\\'); str_appendch(&pat, *g_pos); }
            } else {
                str_appendch(&pat, *g_pos);
            }
            g_pos++;
        }
        if (*g_pos == delim) g_pos++;
        a->type = ADDR_REGEX;
        compile_re(&a->re, pat.buf ? pat.buf : "", 0);
        a->re_valid = 1;
        str_free(&pat);
        return 1;
    }
    return 0;
}

/* Read text for a/i/c commands (rest of line, or next line after \) */
static char *read_text(void) {
    /* skip optional backslash + newline */
    if (*g_pos == '\\') g_pos++;
    if (*g_pos == '\n') g_pos++;
    else skip_ws_no_nl();

    Str s; str_init(&s);
    while (*g_pos && *g_pos != '\n') {
        if (*g_pos == '\\' && *(g_pos+1) == '\n') {
            /* continuation */
            str_appendch(&s, '\n');
            g_pos += 2;
            continue;
        }
        if (*g_pos == '\\' && *(g_pos+1) == 'n') {
            str_appendch(&s, '\n');
            g_pos += 2;
            continue;
        }
        str_appendch(&s, *g_pos++);
    }
    return s.buf ? s.buf : strdup("");
}

/* Read until end of line (for r, w, label) */
static char *read_to_eol(void) {
    skip_ws_no_nl();
    const char *start = g_pos;
    while (*g_pos && *g_pos != '\n' && *g_pos != ';') g_pos++;
    size_t len = (size_t)(g_pos - start);
    char *r = malloc(len + 1);
    memcpy(r, start, len);
    r[len] = '\0';
    /* trim trailing spaces */
    while (len && (r[len-1] == ' ' || r[len-1] == '\t')) r[--len] = '\0';
    return r;
}

static Cmd *alloc_cmd(CmdType t) {
    Cmd *c = calloc(1, sizeof *c);
    if (!c) { perror("calloc"); exit(1); }
    c->type = t;
    return c;
}

static void push_cmd(Cmd *c) {
    if (g_ncmds >= g_ccmds) {
        g_ccmds = g_ccmds ? g_ccmds * 2 : 32;
        g_cmds  = realloc(g_cmds, g_ccmds * sizeof *g_cmds);
        if (!g_cmds) { perror("realloc"); exit(1); }
    }
    g_cmds[g_ncmds++] = c;
}

static void parse_script(void);

/* Parse the s command: s/regex/replacement/flags */
static void parse_subst(Cmd *c) {
    if (!*g_pos) { fprintf(stderr, "sed: s: missing delimiter\n"); exit(1); }
    char delim = *g_pos++;
    /* regex */
    Str pat; str_init(&pat);
    while (*g_pos && *g_pos != delim) {
        if (*g_pos == '\\' && *(g_pos+1)) {
            if (*(g_pos+1) == 'n') { str_appendch(&pat, '\n'); g_pos += 2; continue; }
            str_appendch(&pat, '\\');
            g_pos++;
        }
        str_appendch(&pat, *g_pos++);
    }
    if (*g_pos == delim) g_pos++;

    /* replacement */
    Str rep; str_init(&rep);
    while (*g_pos && *g_pos != delim) {
        if (*g_pos == '\\' && *(g_pos+1)) {
            g_pos++;
            if (*g_pos == 'n') { str_appendch(&rep, '\n'); g_pos++; continue; }
            str_appendch(&rep, '\\');
        }
        str_appendch(&rep, *g_pos++);
    }
    if (*g_pos == delim) g_pos++;

    /* flags */
    c->subst.global = 0;
    c->subst.icase  = 0;
    c->subst.print  = 0;
    c->subst.nth    = 0;
    while (*g_pos && *g_pos != '\n' && *g_pos != ';' && *g_pos != '}') {
        char f = *g_pos++;
        if (f == 'g') c->subst.global = 1;
        else if (f == 'i' || f == 'I') c->subst.icase = 1;
        else if (f == 'p') c->subst.print = 1;
        else if (isdigit((unsigned char)f)) c->subst.nth = f - '0';
        else if (f == 'w') {
            /* w flag — we'll skip for simplicity, consume filename */
            skip_ws_no_nl();
            while (*g_pos && *g_pos != '\n') g_pos++;
            break;
        }
        else if (f == ' ' || f == '\t') continue;
        else break;
    }

    int iflags = c->subst.icase ? REG_ICASE : 0;
    const char *pats = pat.buf ? pat.buf : "";
    if (*pats == '\0' && g_last_re_valid) {
        /* empty regex = reuse last */
        memcpy(&c->subst.re, &g_last_re, sizeof g_last_re);
        c->subst.re_valid = 1;
    } else {
        compile_re(&c->subst.re, pats, iflags);
        c->subst.re_valid = 1;
        /* save as last regex */
        if (g_last_re_valid) regfree(&g_last_re);
        compile_re(&g_last_re, pats, iflags);
        g_last_re_valid = 1;
    }
    c->subst.replacement = rep.buf ? rep.buf : strdup("");
    str_free(&pat);
}

/* Parse y/src/dst/ */
static void parse_translit(Cmd *c) {
    if (!*g_pos) { fprintf(stderr, "sed: y: missing delimiter\n"); exit(1); }
    char delim = *g_pos++;
    int si = 0, di = 0;
    /* src */
    while (*g_pos && *g_pos != delim) {
        char ch;
        if (*g_pos == '\\' && *(g_pos+1)) {
            g_pos++;
            ch = (*g_pos == 'n') ? '\n' : *g_pos;
        } else ch = *g_pos;
        if (si < 255) c->trans.src[si++] = ch;
        g_pos++;
    }
    c->trans.src[si] = '\0';
    if (*g_pos == delim) g_pos++;
    /* dst */
    while (*g_pos && *g_pos != delim) {
        char ch;
        if (*g_pos == '\\' && *(g_pos+1)) {
            g_pos++;
            ch = (*g_pos == 'n') ? '\n' : *g_pos;
        } else ch = *g_pos;
        if (di < 255) c->trans.dst[di++] = ch;
        g_pos++;
    }
    c->trans.dst[di] = '\0';
    if (*g_pos == delim) g_pos++;
    if (si != di) {
        fprintf(stderr, "sed: y: source and dest lengths differ\n");
        exit(1);
    }
    c->trans.len = si;
}

static void parse_script(void) {
    /* Stack for block matching */
    int block_stack[256];
    int block_top = 0;

    while (1) {
        skip_ws();
        if (!*g_pos) break;

        Cmd *c = alloc_cmd(CMD_COMMENT);

        /* Parse addresses */
        c->naddr = 0;
        if (parse_addr(&c->addr[0])) {
            c->naddr = 1;
            skip_ws_no_nl();
            if (*g_pos == ',') {
                g_pos++;
                skip_ws_no_nl();
                if (parse_addr(&c->addr[1])) c->naddr = 2;
            }
        }

        skip_ws_no_nl();
        /* negation */
        if (*g_pos == '!') { c->negate = 1; g_pos++; skip_ws_no_nl(); }

        if (!*g_pos || *g_pos == '\n' || *g_pos == ';') {
            free(c); continue;
        }

        char cmd = *g_pos++;

        switch (cmd) {
        case '#':
            c->type = CMD_COMMENT;
            while (*g_pos && *g_pos != '\n') g_pos++;
            break;
        case 'd': c->type = CMD_DELETE; break;
        case 'p': c->type = CMD_PRINT; break;
        case 'l': c->type = CMD_PRINT_UNAMBIG; break;
        case 'P': c->type = CMD_PRINT_FIRST; break;
        case 'D': c->type = CMD_DELETE_FIRST; break;
        case 'h': c->type = CMD_HOLD_COPY; break;
        case 'H': c->type = CMD_HOLD_APPEND; break;
        case 'g': c->type = CMD_GET_COPY; break;
        case 'G': c->type = CMD_GET_APPEND; break;
        case 'x': c->type = CMD_EXCHANGE; break;
        case 'n': c->type = CMD_NEXT; break;
        case 'N': c->type = CMD_NEXT_APPEND; break;
        case '=': c->type = CMD_LINENUM; break;
        case 'q':
            c->type = CMD_QUIT;
            skip_ws_no_nl();
            if (isdigit((unsigned char)*g_pos)) c->ival = atoi(g_pos);
            while (*g_pos && *g_pos != '\n' && *g_pos != ';') g_pos++;
            break;
        case 'Q':
            c->type = CMD_QUIT_SILENT;
            skip_ws_no_nl();
            if (isdigit((unsigned char)*g_pos)) c->ival = atoi(g_pos);
            while (*g_pos && *g_pos != '\n' && *g_pos != ';') g_pos++;
            break;
        case 'a':
            c->type = CMD_APPEND;
            c->text = read_text();
            break;
        case 'i':
            c->type = CMD_INSERT;
            c->text = read_text();
            break;
        case 'c':
            c->type = CMD_CHANGE;
            c->text = read_text();
            break;
        case 's':
            c->type = CMD_SUBST;
            parse_subst(c);
            break;
        case 'y':
            c->type = CMD_TRANSLIT;
            parse_translit(c);
            break;
        case 'r':
            c->type = CMD_READ_FILE;
            c->text = read_to_eol();
            break;
        case 'R':
            c->type = CMD_READ_LINE;
            c->text = read_to_eol();
            break;
        case 'w':
            c->type = CMD_WRITE_FILE;
            c->text = read_to_eol();
            break;
        case ':':
            c->type = CMD_LABEL;
            skip_ws_no_nl();
            c->text = read_to_eol();
            break;
        case 'b':
            c->type = CMD_BRANCH;
            skip_ws_no_nl();
            c->text = read_to_eol();
            break;
        case 't':
            c->type = CMD_BRANCH_TRUE;
            skip_ws_no_nl();
            c->text = read_to_eol();
            break;
        case 'T':
            c->type = CMD_BRANCH_FALSE;
            skip_ws_no_nl();
            c->text = read_to_eol();
            break;
        case '{':
            c->type = CMD_BLOCK_START;
            if (block_top < 255) block_stack[block_top++] = g_ncmds;
            push_cmd(c);
            continue;
        case '}':
            c->type = CMD_BLOCK_END;
            if (block_top > 0) {
                int open_idx = block_stack[--block_top];
                g_cmds[open_idx]->block_end = g_ncmds;
            }
            push_cmd(c);
            continue;
        default:
            fprintf(stderr, "sed: unknown command: '%c'\n", cmd);
            exit(1);
        }

        push_cmd(c);
    }
}

/* ── Address matching ────────────────────────────────────────────────────── */

static int addr_matches(Addr *a, long lineno, int is_last, const char *line) {
    switch (a->type) {
    case ADDR_NONE:  return 1;
    case ADDR_LINE:  return lineno == a->line;
    case ADDR_LAST:  return is_last;
    case ADDR_STEP:
        if (a->step == 0) return lineno == a->line;
        return (a->line == 0)
            ? (lineno % a->step == 0)
            : (lineno >= a->line && (lineno - a->line) % a->step == 0);
    case ADDR_REGEX: {
        regmatch_t m;
        return regexec(&a->re, line, 1, &m, 0) == 0;
    }
    }
    return 0;
}

/* Range state: for each 2-addr command, track whether we're inside the range */
static int *g_in_range  = NULL;
static int  g_range_cap = 0;

static void ensure_range(int idx) {
    if (idx >= g_range_cap) {
        int nc = g_range_cap ? g_range_cap * 2 : 64;
        while (nc <= idx) nc *= 2;
        g_in_range = realloc(g_in_range, nc * sizeof(int));
        memset(g_in_range + g_range_cap, 0, (nc - g_range_cap) * sizeof(int));
        g_range_cap = nc;
    }
}

static int cmd_matches(Cmd *c, int idx) {
    int match;

    if (c->naddr == 0) {
        match = 1;
    } else if (c->naddr == 1) {
        match = addr_matches(&c->addr[0], g_lineno, g_last_line, g_pattern.buf ? g_pattern.buf : "");
    } else {
        /* 2-address range */
        ensure_range(idx);
        if (!g_in_range[idx]) {
            if (addr_matches(&c->addr[0], g_lineno, g_last_line, g_pattern.buf ? g_pattern.buf : "")) {
                g_in_range[idx] = 1;
                /* if addr2 is a line <= addr1, end immediately */
                if (c->addr[1].type == ADDR_LINE && c->addr[1].line <= g_lineno)
                    g_in_range[idx] = 0;
                match = 1;
            } else match = 0;
        } else {
            match = 1;
            if (addr_matches(&c->addr[1], g_lineno, g_last_line, g_pattern.buf ? g_pattern.buf : ""))
                g_in_range[idx] = 0;
        }
    }

    return c->negate ? !match : match;
}

/* ── Substitution ────────────────────────────────────────────────────────── */

/* Build replacement string, expanding & and \1..\9 */
static void expand_replacement(Str *out, const char *rep,
                                const char *src, regmatch_t *matches, int nmatches) {
    for (const char *p = rep; *p; p++) {
        if (*p == '\\' && *(p+1)) {
            p++;
            if (*p >= '1' && *p <= '9') {
                int n = *p - '0';
                if (n < nmatches && matches[n].rm_so >= 0)
                    str_append(out, src + matches[n].rm_so,
                               (size_t)(matches[n].rm_eo - matches[n].rm_so));
            } else if (*p == '\\') {
                str_appendch(out, '\\');
            } else if (*p == 'n') {
                str_appendch(out, '\n');
            } else {
                str_appendch(out, '\\');
                str_appendch(out, *p);
            }
        } else if (*p == '&') {
            if (matches[0].rm_so >= 0)
                str_append(out, src + matches[0].rm_so,
                           (size_t)(matches[0].rm_eo - matches[0].rm_so));
        } else {
            str_appendch(out, *p);
        }
    }
}

/* Returns 1 if substitution was made */
static int do_subst(Cmd *c) {
    if (!g_pattern.buf) return 0;
    const char *src  = g_pattern.buf;
    size_t      srclen = g_pattern.len;
    Str out; str_init(&out);
    int made = 0;
    int occurrence = 0;
    size_t pos = 0;
    int want_nth = c->subst.nth; /* 0 = apply based on global flag */
    regmatch_t matches[10];

    while (pos <= srclen) {
        int r = regexec(&c->subst.re, src + pos, 10, matches, pos > 0 ? REG_NOTBOL : 0);
        if (r != 0) {
            /* no more matches */
            str_append(&out, src + pos, srclen - pos);
            break;
        }
        occurrence++;
        /* copy everything before match */
        str_append(&out, src + pos, (size_t)matches[0].rm_so);

        int do_replace;
        if (want_nth > 0)
            do_replace = (occurrence == want_nth);
        else
            do_replace = (c->subst.global || occurrence == 1);

        if (do_replace) {
            expand_replacement(&out, c->subst.replacement, src + pos, matches, 10);
            made = 1;
        } else {
            /* no replace: keep original match */
            str_append(&out, src + pos + matches[0].rm_so,
                       (size_t)(matches[0].rm_eo - matches[0].rm_so));
        }

        size_t consumed = (size_t)matches[0].rm_eo;
        if (consumed == 0) {
            /* zero-length match: advance by one char to avoid infinite loop */
            if (pos < srclen)
                str_appendch(&out, src[pos]);
            pos++;
        } else {
            pos += consumed;
        }

        if (!c->subst.global && want_nth == 0) {
            /* only replace first occurrence */
            str_append(&out, src + pos, srclen - pos);
            break;
        }
        if (want_nth > 0 && occurrence >= want_nth) {
            str_append(&out, src + pos, srclen - pos);
            break;
        }
    }

    if (made) {
        str_set(&g_pattern, out.buf ? out.buf : "", out.len);
        if (c->subst.print) {
            puts(g_pattern.buf ? g_pattern.buf : "");
        }
    }
    str_free(&out);
    return made;
}

/* ── Transliterate ───────────────────────────────────────────────────────── */

static void do_translit(Cmd *c) {
    if (!g_pattern.buf) return;
    for (size_t i = 0; i < g_pattern.len; i++) {
        unsigned char ch = (unsigned char)g_pattern.buf[i];
        for (int j = 0; j < c->trans.len; j++) {
            if ((unsigned char)c->trans.src[j] == ch) {
                g_pattern.buf[i] = c->trans.dst[j];
                break;
            }
        }
    }
}

/* ── Print unambiguously (l command) ─────────────────────────────────────── */

static void do_print_unambig(void) {
    const char *p = g_pattern.buf ? g_pattern.buf : "";
    int col = 0;
    while (*p) {
        unsigned char c = *p++;
        char tmp[8];
        int n;
        if (c == '\\')      { n = snprintf(tmp, sizeof tmp, "\\\\"); }
        else if (c == '\a') { n = snprintf(tmp, sizeof tmp, "\\a"); }
        else if (c == '\b') { n = snprintf(tmp, sizeof tmp, "\\b"); }
        else if (c == '\f') { n = snprintf(tmp, sizeof tmp, "\\f"); }
        else if (c == '\r') { n = snprintf(tmp, sizeof tmp, "\\r"); }
        else if (c == '\t') { n = snprintf(tmp, sizeof tmp, "\\t"); }
        else if (c == '\v') { n = snprintf(tmp, sizeof tmp, "\\v"); }
        else if (c == '\n') { n = snprintf(tmp, sizeof tmp, "\\n"); }
        else if (c < 0x20 || c == 0x7f)
                            { n = snprintf(tmp, sizeof tmp, "\\%03o", c); }
        else                { tmp[0] = (char)c; tmp[1] = '\0'; n = 1; }
        if (col + n >= 70) { putchar('\\'); putchar('\n'); col = 0; }
        fputs(tmp, stdout);
        col += n;
    }
    puts("$");
}

/* ── Append deferred output ──────────────────────────────────────────────── */

static void flush_deferred(void) {
    if (g_deferred.len) {
        fputs(g_deferred.buf, stdout);
        g_deferred.len = 0;
        g_deferred.buf[0] = '\0';
    }
}

/* ── Find label index ────────────────────────────────────────────────────── */

static int find_label(const char *name) {
    for (int i = 0; i < g_ncmds; i++) {
        if (g_cmds[i]->type == CMD_LABEL) {
            const char *l = g_cmds[i]->text;
            if (l && strcmp(l, name) == 0) return i;
        }
    }
    return -1;
}

/* ── Execute one cycle ────────────────────────────────────────────────────── */

/* Read next line from input; returns 1 on success, 0 on EOF */
typedef struct {
    FILE **files;
    int    nfiles;
    int    cur;
    int    eof;
} Input;

static Input g_input;

static int input_getline(Str *out) {
    while (g_input.cur < g_input.nfiles) {
        FILE *f = g_input.files[g_input.cur];
        if (!f) { g_input.cur++; continue; }
        int c;
        Str tmp; str_init(&tmp);
        int got = 0;
        while ((c = fgetc(f)) != EOF) {
            got = 1;
            if (c == '\n') break;
            str_appendch(&tmp, (char)c);
        }
        if (got) {
            if (out) str_set(out, tmp.buf ? tmp.buf : "", tmp.len);
            str_free(&tmp);
            return 1;
        }
        str_free(&tmp);
        /* EOF on this file */
        if (f != stdin) fclose(f);
        g_input.cur++;
    }
    g_input.eof = 1;
    return 0;
}

/* Peek: is this the last line? */
static int peek_is_last(void) {
    /* We check by trying to peek at the next byte of the next file */
    FILE *f = NULL;
    for (int i = g_input.cur; i < g_input.nfiles; i++) {
        if (g_input.files[i]) { f = g_input.files[i]; break; }
    }
    if (!f) return 1;
    int c = fgetc(f);
    if (c == EOF) return 1;
    ungetc(c, f);
    return 0;
}

static void auto_print(void) {
    if (!g_suppress)
        puts(g_pattern.buf ? g_pattern.buf : "");
    flush_deferred();
}

/* Execute script on current pattern space. Returns 0 normally, 1 to restart cycle, 2 to end. */
static int execute(void);

static int execute(void) {
    int i = 0;
    while (i < g_ncmds) {
        Cmd *c = g_cmds[i];

        if (c->type == CMD_BLOCK_END) { i++; continue; }
        if (c->type == CMD_LABEL)     { i++; continue; }
        if (c->type == CMD_COMMENT)   { i++; continue; }

        if (!cmd_matches(c, i)) {
            if (c->type == CMD_BLOCK_START)
                i = c->block_end + 1;
            else
                i++;
            continue;
        }

        switch (c->type) {

        case CMD_DELETE:
            str_setc(&g_pattern, "");
            return 1;  /* restart cycle, don't print */

        case CMD_PRINT:
            puts(g_pattern.buf ? g_pattern.buf : "");
            break;

        case CMD_PRINT_UNAMBIG:
            do_print_unambig();
            break;

        case CMD_PRINT_FIRST: {
            const char *p = g_pattern.buf ? g_pattern.buf : "";
            const char *nl = strchr(p, '\n');
            if (nl) fwrite(p, 1, (size_t)(nl - p), stdout), putchar('\n');
            else puts(p);
            break;
        }

        case CMD_DELETE_FIRST: {
            const char *p = g_pattern.buf ? g_pattern.buf : "";
            const char *nl = strchr(p, '\n');
            if (nl) {
                str_setc(&g_pattern, nl + 1);
            } else {
                str_setc(&g_pattern, "");
            }
            /* restart cycle without reading new line */
            return 1;
        }

        case CMD_QUIT:
            auto_print();
            g_quit = 1;
            g_quit_code = c->ival;
            return 2;

        case CMD_QUIT_SILENT:
            g_quit = 1;
            g_quit_code = c->ival;
            return 2;

        case CMD_APPEND:
            /* deferred: print text after current line is printed */
            if (c->text) {
                str_appendc(&g_deferred, c->text);
                str_appendc(&g_deferred, "\n");
            }
            break;

        case CMD_INSERT:
            if (c->text) puts(c->text);
            break;

        case CMD_CHANGE:
            /* for ranges, only print on last line of range */
            /* simplified: print on every match for now */
            if (c->text) puts(c->text);
            flush_deferred();
            return 1;  /* don't print pattern space */

        case CMD_SUBST:
            if (do_subst(c)) g_subst_flag = 1;
            break;

        case CMD_TRANSLIT:
            do_translit(c);
            break;

        case CMD_LINENUM:
            printf("%ld\n", g_lineno);
            break;

        case CMD_READ_FILE:
            if (c->text) {
                FILE *rf = fopen(c->text, "r");
                if (rf) {
                    Str tmp; str_init(&tmp);
                    int ch;
                    while ((ch = fgetc(rf)) != EOF)
                        str_appendch(&tmp, (char)ch);
                    fclose(rf);
                    if (tmp.buf) {
                        str_appendc(&g_deferred, tmp.buf);
                        if (tmp.len && tmp.buf[tmp.len-1] != '\n')
                            str_appendc(&g_deferred, "\n");
                    }
                    str_free(&tmp);
                }
            }
            break;

        case CMD_READ_LINE:
            if (c->text) {
                FILE *rf = fopen(c->text, "r");
                if (rf) {
                    char line[4096];
                    if (fgets(line, sizeof line, rf)) {
                        str_appendc(&g_deferred, line);
                        if (!strchr(line, '\n'))
                            str_appendc(&g_deferred, "\n");
                    }
                    fclose(rf);
                }
            }
            break;

        case CMD_WRITE_FILE:
            if (c->text) {
                FILE *wf = fopen(c->text, "a");
                if (wf) {
                    fprintf(wf, "%s\n", g_pattern.buf ? g_pattern.buf : "");
                    fclose(wf);
                }
            }
            break;

        case CMD_NEXT: {
            auto_print();
            Str next; str_init(&next);
            if (!input_getline(&next)) {
                str_free(&next);
                g_quit = 1;
                return 2;
            }
            g_lineno++;
            g_last_line = peek_is_last();
            str_set(&g_pattern, next.buf ? next.buf : "", next.len);
            str_free(&next);
            g_subst_flag = 0;
            break;
        }

        case CMD_NEXT_APPEND: {
            Str next; str_init(&next);
            if (!input_getline(&next)) {
                auto_print();
                str_free(&next);
                g_quit = 1;
                return 2;
            }
            g_lineno++;
            g_last_line = peek_is_last();
            str_appendch(&g_pattern, '\n');
            str_appendc(&g_pattern, next.buf ? next.buf : "");
            str_free(&next);
            break;
        }

        case CMD_HOLD_COPY:
            str_set(&g_hold, g_pattern.buf ? g_pattern.buf : "", g_pattern.len);
            break;

        case CMD_HOLD_APPEND:
            str_appendch(&g_hold, '\n');
            str_appendc(&g_hold, g_pattern.buf ? g_pattern.buf : "");
            break;

        case CMD_GET_COPY:
            str_set(&g_pattern, g_hold.buf ? g_hold.buf : "", g_hold.len);
            break;

        case CMD_GET_APPEND:
            str_appendch(&g_pattern, '\n');
            str_appendc(&g_pattern, g_hold.buf ? g_hold.buf : "");
            break;

        case CMD_EXCHANGE: {
            Str tmp = g_pattern;
            g_pattern = g_hold;
            g_hold = tmp;
            break;
        }

        case CMD_LABEL:
            break;

        case CMD_BRANCH: {
            const char *lbl = c->text;
            if (!lbl || *lbl == '\0') {
                /* branch to end of script */
                return 0;
            }
            int li = find_label(lbl);
            if (li < 0) { fprintf(stderr, "sed: label not found: %s\n", lbl); exit(1); }
            i = li;
            continue;
        }

        case CMD_BRANCH_TRUE:
            if (g_subst_flag) {
                g_subst_flag = 0;
                const char *lbl = c->text;
                if (!lbl || *lbl == '\0') return 0;
                int li = find_label(lbl);
                if (li < 0) { fprintf(stderr, "sed: label not found: %s\n", lbl); exit(1); }
                i = li;
                continue;
            }
            break;

        case CMD_BRANCH_FALSE:
            if (!g_subst_flag) {
                g_subst_flag = 0;
                const char *lbl = c->text;
                if (!lbl || *lbl == '\0') return 0;
                int li = find_label(lbl);
                if (li < 0) { fprintf(stderr, "sed: label not found: %s\n", lbl); exit(1); }
                i = li;
                continue;
            }
            g_subst_flag = 0;
            break;

        case CMD_BLOCK_START:
            /* enter block */
            break;

        default:
            break;
        }

        i++;
    }
    return 0;  /* end of script: proceed to auto-print */
}

/* ── Main processing loop ────────────────────────────────────────────────── */

static void process(void) {
    Str line; str_init(&line);

    while (!g_quit && input_getline(&line)) {
        g_lineno++;
        g_last_line = peek_is_last();
        g_subst_flag = 0;

        str_set(&g_pattern, line.buf ? line.buf : "", line.len);

        int result = execute();
        if (result == 1) {
            /* cycle restarted: don't auto-print */
            flush_deferred();
            continue;
        }
        if (result == 2) {
            /* quit */
            break;
        }
        auto_print();
    }

    str_free(&line);
}

/* ── In-place editing ────────────────────────────────────────────────────── */

static void process_inplace(const char *filename) {
    /* Redirect stdout to a temp file, then rename */
    char tmpname[PATH_MAX];
    snprintf(tmpname, sizeof tmpname, "%s.sedXXXXXX", filename);
    int fd = mkstemp(tmpname);
    if (fd < 0) { perror(tmpname); exit(1); }

    /* Preserve permissions */
    struct stat st;
    if (stat(filename, &st) == 0) fchmod(fd, st.st_mode);

    FILE *tmpf = fdopen(fd, "w");
    if (!tmpf) { perror(tmpname); exit(1); }

    /* Redirect stdout */
    FILE *orig_stdout = stdout;
    stdout = tmpf;

    process();

    stdout = orig_stdout;
    fclose(tmpf);

    /* Backup if needed */
    if (g_inplace_suffix && *g_inplace_suffix) {
        char backup[PATH_MAX];
        snprintf(backup, sizeof backup, "%s%s", filename, g_inplace_suffix);
        rename(filename, backup);
    }

    if (rename(tmpname, filename) != 0) {
        perror("rename");
        unlink(tmpname);
        exit(1);
    }
}

/* ── main ────────────────────────────────────────────────────────────────── */

static void usage(void) {
    fprintf(stderr,
        "Usage: sed [options] [script] [file...]\n"
        "\n"
        "Options:\n"
        "  -n            Suppress auto-print\n"
        "  -e script     Add script fragment\n"
        "  -f file       Read script from file\n"
        "  -i[suffix]    In-place edit (optional backup suffix)\n"
        "  -E, -r        Extended regular expressions\n"
        "\n"
        "Commands: s, d, p, q, Q, a, i, c, y, =, l, r, R, w, n, N, P, D,\n"
        "          h, H, g, G, x, :, b, t, T, { }\n"
    );
    exit(1);
}

int main(int argc, char *argv[]) {
    str_init(&g_script);
    str_init(&g_pattern);
    str_init(&g_hold);
    str_init(&g_deferred);
    str_setc(&g_hold, "");

    /* --- option parsing --- */
    int  has_script  = 0;
    int  fileidx     = 0;

    int argi = 1;
    for (; argi < argc; argi++) {
        int i = argi;
        if (argv[i][0] == '-' && argv[i][1]) {
            const char *opt = argv[i] + 1;
            if (*opt == 'n') {
                g_suppress = 1;
            } else if (*opt == 'E' || *opt == 'r') {
                g_ere = 1;
            } else if (*opt == 'e') {
                const char *script;
                if (opt[1]) script = opt + 1;
                else if (argi + 1 < argc) { argi++; script = argv[argi]; }
                else usage();
                str_appendc(&g_script, script);
                str_appendch(&g_script, '\n');
                has_script = 1;
            } else if (*opt == 'f') {
                const char *fname;
                if (opt[1]) fname = opt + 1;
                else if (argi + 1 < argc) { argi++; fname = argv[argi]; }
                else usage();
                FILE *sf = fopen(fname, "r");
                if (!sf) { perror(fname); exit(1); }
                int c;
                while ((c = fgetc(sf)) != EOF) str_appendch(&g_script, (char)c);
                str_appendch(&g_script, '\n');
                fclose(sf);
                has_script = 1;
            } else if (*opt == 'i') {
                g_inplace = 1;
                g_inplace_suffix = (opt[1]) ? opt + 1 : NULL;
            } else if (*opt == '-' && opt[1] == '\0') {
                /* -- end of options */
                fileidx = i + 1;
                argi = i + 1;
                goto opts_done;
            } else {
                fprintf(stderr, "sed: unknown option: -%c\n", *opt);
                usage();
            }
        } else {
            fileidx = i;
            argi = i;
            break;
        }
    }
    opts_done:

    if (fileidx == 0) fileidx = argi;
    /* If no -e/-f, first non-option arg is the script */
    if (!has_script) {
        if (fileidx >= argc) usage();
        str_appendc(&g_script, argv[fileidx++]);
        str_appendch(&g_script, '\n');
    }

    /* Parse script */
    g_pos = g_script.buf ? g_script.buf : "";
    parse_script();

    /* Collect input files */
    int nfiles = argc - fileidx;
    FILE **files;

    if (nfiles == 0) {
        files = malloc(sizeof(FILE *));
        files[0] = stdin;
        nfiles = 1;
    } else {
        files = malloc(nfiles * sizeof(FILE *));
        for (int i = 0; i < nfiles; i++) {
            if (strcmp(argv[fileidx + i], "-") == 0)
                files[i] = stdin;
            else {
                files[i] = fopen(argv[fileidx + i], "r");
                if (!files[i]) { perror(argv[fileidx + i]); exit(1); }
            }
        }
    }

    if (g_inplace && nfiles > 0 && files[0] != stdin) {
        /* Process each file separately */
        for (int fi = 0; fi < nfiles; fi++) {
            const char *fname = argv[fileidx + fi];
            if (files[fi]) fclose(files[fi]);

            /* Reset state */
            g_lineno    = 0;
            g_last_line = 0;
            g_subst_flag= 0;
            g_quit      = 0;
            g_quit_code = 0;
            str_setc(&g_pattern, "");
            str_setc(&g_hold, "");
            str_setc(&g_deferred, "");
            if (g_in_range) memset(g_in_range, 0, g_range_cap * sizeof(int));

            FILE *inf = fopen(fname, "r");
            if (!inf) { perror(fname); continue; }
            g_input.files = &inf;
            g_input.nfiles = 1;
            g_input.cur = 0;
            g_input.eof = 0;

            process_inplace(fname);
            fclose(inf);
        }
    } else {
        g_input.files  = files;
        g_input.nfiles = nfiles;
        g_input.cur    = 0;
        g_input.eof    = 0;
        process();
    }

    free(files);
    return g_quit_code;
}
