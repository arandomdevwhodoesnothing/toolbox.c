/*
 * awk - A minimal AWK implementation in C
 * Supports: patterns, actions, BEGIN/END, built-in variables,
 * arithmetic, string ops, printf, getline, arrays, functions,
 * regex matching, field splitting, pipes, and more.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <errno.h>
#include <regex.h>
#include <time.h>

/* ============================================================
 * LIMITS & CONSTANTS
 * ============================================================ */
#define MAX_FIELDS      1024
#define MAX_LINE        65536
#define MAX_TOKENS      65536
#define MAX_RULES       512
#define MAX_FUNCTIONS   128
#define MAX_STACK       4096
#define MAX_LOCALS      64
#define MAX_ARGS        32
#define MAX_ARRAYS      256
#define MAX_ARRAY_KEYS  4096
#define HASH_SIZE       2048
#define MAX_PIPES       16
#define VERSION         "awk 1.0"

/* ============================================================
 * TOKEN TYPES
 * ============================================================ */
typedef enum {
    /* Literals */
    TK_NUMBER, TK_STRING, TK_REGEX,
    /* Identifiers */
    TK_IDENT,
    /* Keywords */
    TK_BEGIN, TK_END, TK_IF, TK_ELSE, TK_WHILE, TK_FOR,
    TK_DO, TK_BREAK, TK_CONTINUE, TK_RETURN, TK_EXIT,
    TK_PRINT, TK_PRINTF, TK_GETLINE, TK_DELETE, TK_IN,
    TK_NEXT, TK_NEXTFILE, TK_FUNCTION, TK_PIPE,
    /* Operators */
    TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_PERCENT,
    TK_CARET, TK_BANG, TK_AND, TK_OR,
    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LE, TK_GE,
    TK_ASSIGN, TK_PLUSEQ, TK_MINUSEQ, TK_STAREQ,
    TK_SLASHEQ, TK_PERCENTEQ, TK_CARETEQ,
    TK_INC, TK_DEC,
    TK_MATCH, TK_NOTMATCH, TK_CONCAT,
    TK_TERNARY, TK_COLON,
    /* Punctuation */
    TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE,
    TK_LBRACKET, TK_RBRACKET,
    TK_SEMICOLON, TK_COMMA, TK_NEWLINE,
    TK_DOLLAR, TK_APPEND,
    /* Special */
    TK_EOF
} TokenType;

typedef struct {
    TokenType type;
    char     *str;      /* string value for TK_STRING, TK_IDENT, TK_REGEX */
    double    num;      /* numeric value for TK_NUMBER */
    int       line;
} Token;

/* ============================================================
 * AST NODE TYPES
 * ============================================================ */
typedef enum {
    ND_NUM, ND_STR, ND_REGEX,
    ND_IDENT, ND_FIELD, ND_ARRAY_REF,
    ND_ASSIGN, ND_OPASSIGN,
    ND_BINOP, ND_UNOP, ND_TERNARY,
    ND_INC, ND_DEC,   /* pre/post */
    ND_CALL, ND_BUILTIN,
    ND_PRINT, ND_PRINTF,
    ND_GETLINE,
    ND_IF, ND_WHILE, ND_DO_WHILE,
    ND_FOR, ND_FOR_IN,
    ND_BREAK, ND_CONTINUE, ND_RETURN, ND_EXIT, ND_NEXT, ND_NEXTFILE,
    ND_DELETE,
    ND_BLOCK, ND_EMPTY,
    ND_MATCH_EXPR,  /* ~ and !~ */
    ND_IN,          /* (k in arr) */
    ND_CONCAT,
    ND_PIPE_PRINT,  /* print | cmd */
    ND_APPEND_PRINT /* print >> file */
} NodeType;

struct Node;
typedef struct Node Node;

struct Node {
    NodeType type;
    double   numval;
    char    *strval;
    Token    op;        /* operator token */
    Node    *left;
    Node    *right;
    Node    *cond;
    Node    *body;
    Node    *elsebody;
    Node    *init;
    Node    *incr;
    Node   **children;
    int      nchildren;
    int      flags;     /* pre=1/post=0 for inc/dec, etc */
    int      line;
};

/* ============================================================
 * VALUE TYPES
 * ============================================================ */
typedef enum {
    VAL_UNDEF = 0,
    VAL_NUM,
    VAL_STR,
    VAL_NUMSTR   /* a string that also has numeric meaning */
} ValType;

typedef struct Value {
    ValType type;
    double  num;
    char   *str;
} Value;

/* ============================================================
 * ARRAY
 * ============================================================ */
typedef struct ArrayEntry {
    char            *key;
    Value            val;
    struct ArrayEntry *next;
} ArrayEntry;

typedef struct {
    char        *name;
    ArrayEntry  *buckets[HASH_SIZE];
    int          size;
} Array;

/* ============================================================
 * FUNCTION
 * ============================================================ */
typedef struct {
    char  *name;
    char **params;
    int    nparam;
    Node  *body;
} Function;

/* ============================================================
 * RULE
 * ============================================================ */
typedef struct {
    Node *pattern;   /* NULL = match all; TK_BEGIN/TK_END special */
    Node *pattern2;  /* for range patterns */
    Node *action;
    int   is_begin;
    int   is_end;
    int   range_active; /* for range patterns */
} Rule;

/* ============================================================
 * GLOBAL STATE
 * ============================================================ */
static char   g_line[MAX_LINE];
static char  *g_fields[MAX_FIELDS];
static int    g_nfields;
static char   g_ofs[256];   /* OFS */
static char   g_ors[256];   /* ORS */
static char   g_fs[256];    /* FS */
static char   g_rs[256];    /* RS */
static char   g_subsep[64]; /* SUBSEP */
static long   g_nr;         /* NR */
static long   g_fnr;        /* FNR */
static int    g_exit_flag;
static int    g_next_flag;
static int    g_nextfile_flag;
static int    g_break_flag;
static int    g_continue_flag;
static int    g_return_flag;
static Value  g_return_val;
static int    g_exit_code;
static char  *g_filename;   /* FILENAME */
static char  *g_argv0;

/* Variable table */
typedef struct VarEntry {
    char           *name;
    Value           val;
    Array          *arr;
    struct VarEntry *next;
} VarEntry;

#define VAR_HASH 512
static VarEntry *g_vars[VAR_HASH];

/* Functions table */
static Function g_functions[MAX_FUNCTIONS];
static int      g_nfunctions;

/* Rules */
static Rule g_rules[MAX_RULES];
static int  g_nrules;

/* Pipe table */
typedef struct {
    char  *cmd;
    FILE  *fp;
    int    write; /* 1=write, 0=read */
} PipeEntry;
static PipeEntry g_pipes[MAX_PIPES];
static int       g_npipes;

/* Token stream */
static Token *g_tokens;
static int    g_ntokens;
static int    g_tokpos;

/* Source */
static char *g_src;
static int   g_srclen;
static int   g_srcpos;
static int   g_srcline;

/* ============================================================
 * UTILITIES
 * ============================================================ */
static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "awk: ");
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (!p) die("out of memory");
    return p;
}

static void *xrealloc(void *p, size_t n) {
    p = realloc(p, n);
    if (!p) die("out of memory");
    return p;
}

static char *xstrdup(const char *s) {
    char *p = strdup(s);
    if (!p) die("out of memory");
    return p;
}

static char *xstrndup(const char *s, int n) {
    char *p = xmalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ============================================================
 * VALUE OPERATIONS
 * ============================================================ */
static Value val_num(double n) {
    Value v; v.type = VAL_NUM; v.num = n; v.str = NULL; return v;
}

static Value val_str(const char *s) {
    Value v; v.type = VAL_STR; v.num = 0; v.str = xstrdup(s); return v;
}

static Value val_undef(void) {
    Value v; v.type = VAL_UNDEF; v.num = 0; v.str = NULL; return v;
}

static double val_to_num(Value *v) {
    if (v->type == VAL_NUM || v->type == VAL_NUMSTR) return v->num;
    if (v->type == VAL_STR && v->str) return atof(v->str);
    return 0.0;
}

static char *val_to_str_buf(Value *v, char *buf, size_t bufsz) {
    if (v->type == VAL_STR || v->type == VAL_NUMSTR) {
        if (v->str) return v->str;
        return "";
    }
    if (v->type == VAL_NUM) {
        double n = v->num;
        if (n == (long long)n && fabs(n) < 1e15)
            snprintf(buf, bufsz, "%.0f", n);
        else
            snprintf(buf, bufsz, "%g", n);
        return buf;
    }
    return "";
}

static char *val_to_str_alloc(Value *v) {
    char buf[64];
    char *s = val_to_str_buf(v, buf, sizeof(buf));
    return xstrdup(s);
}

static void val_free(Value *v) {
    if (v->str) { free(v->str); v->str = NULL; }
}

static Value val_copy(Value *v) {
    Value c = *v;
    if (v->str) c.str = xstrdup(v->str);
    return c;
}

static int val_is_true(Value *v) {
    if (v->type == VAL_NUM) return v->num != 0.0;
    if (v->type == VAL_STR) return v->str && v->str[0] != '\0';
    if (v->type == VAL_NUMSTR) return v->num != 0.0 || (v->str && v->str[0]);
    return 0;
}

/* ============================================================
 * VARIABLE TABLE
 * ============================================================ */
static unsigned int var_hash(const char *s) {
    unsigned int h = 5381;
    while (*s) h = h * 33 + (unsigned char)*s++;
    return h % VAR_HASH;
}

static VarEntry *var_find(const char *name) {
    unsigned int h = var_hash(name);
    VarEntry *e = g_vars[h];
    while (e) {
        if (strcmp(e->name, name) == 0) return e;
        e = e->next;
    }
    return NULL;
}

static VarEntry *var_get_or_create(const char *name) {
    unsigned int h = var_hash(name);
    VarEntry *e = g_vars[h];
    while (e) {
        if (strcmp(e->name, name) == 0) return e;
        e = e->next;
    }
    e = xmalloc(sizeof(VarEntry));
    e->name = xstrdup(name);
    e->val  = val_undef();
    e->arr  = NULL;
    e->next = g_vars[h];
    g_vars[h] = e;
    return e;
}

static Value var_get(const char *name) {
    VarEntry *e = var_find(name);
    if (!e) return val_undef();
    return val_copy(&e->val);
}

static void var_set(const char *name, Value v) {
    VarEntry *e = var_get_or_create(name);
    val_free(&e->val);
    e->val = v;
}

/* ============================================================
 * ARRAY OPERATIONS
 * ============================================================ */
static unsigned int arr_hash(const char *k) {
    unsigned int h = 5381;
    while (*k) h = h * 33 + (unsigned char)*k++;
    return h % HASH_SIZE;
}

static Array *array_get_or_create(const char *name) {
    VarEntry *e = var_get_or_create(name);
    if (!e->arr) {
        e->arr = xmalloc(sizeof(Array));
        memset(e->arr, 0, sizeof(Array));
        e->arr->name = xstrdup(name);
    }
    return e->arr;
}

static Array *array_find(const char *name) {
    VarEntry *e = var_find(name);
    if (!e) return NULL;
    return e->arr;
}

static Value *array_ref(Array *arr, const char *key) {
    unsigned int h = arr_hash(key);
    ArrayEntry *e = arr->buckets[h];
    while (e) {
        if (strcmp(e->key, key) == 0) return &e->val;
        e = e->next;
    }
    e = xmalloc(sizeof(ArrayEntry));
    e->key  = xstrdup(key);
    e->val  = val_undef();
    e->next = arr->buckets[h];
    arr->buckets[h] = e;
    arr->size++;
    return &e->val;
}

static int array_in(Array *arr, const char *key) {
    if (!arr) return 0;
    unsigned int h = arr_hash(key);
    ArrayEntry *e = arr->buckets[h];
    while (e) {
        if (strcmp(e->key, key) == 0) return 1;
        e = e->next;
    }
    return 0;
}

static void array_delete(Array *arr, const char *key) {
    unsigned int h = arr_hash(key);
    ArrayEntry **ep = &arr->buckets[h];
    while (*ep) {
        if (strcmp((*ep)->key, key) == 0) {
            ArrayEntry *tmp = *ep;
            *ep = tmp->next;
            val_free(&tmp->val);
            free(tmp->key);
            free(tmp);
            arr->size--;
            return;
        }
        ep = &(*ep)->next;
    }
}

static void array_delete_all(Array *arr) {
    for (int i = 0; i < HASH_SIZE; i++) {
        ArrayEntry *e = arr->buckets[i];
        while (e) {
            ArrayEntry *nx = e->next;
            val_free(&e->val);
            free(e->key);
            free(e);
            e = nx;
        }
        arr->buckets[i] = NULL;
    }
    arr->size = 0;
}

/* ============================================================
 * FIELD SPLITTING
 * ============================================================ */
static void split_fields(char *line, const char *fs) {
    static char line_copy[MAX_LINE];
    /* free old fields */
    for (int i = 0; i < g_nfields; i++) {
        free(g_fields[i]);
        g_fields[i] = NULL;
    }
    g_nfields = 0;

    strncpy(line_copy, line, MAX_LINE-1);
    line_copy[MAX_LINE-1] = '\0';

    if (strcmp(fs, " ") == 0) {
        /* Split on runs of whitespace */
        char *p = line_copy;
        while (*p && isspace((unsigned char)*p)) p++;
        while (*p) {
            char *start = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (g_nfields < MAX_FIELDS-1)
                g_fields[g_nfields++] = xstrndup(start, p - start);
            while (*p && isspace((unsigned char)*p)) p++;
        }
    } else if (strlen(fs) == 1) {
        /* Single character FS */
        char *p = line_copy;
        while (1) {
            char *start = p;
            while (*p && *p != fs[0]) p++;
            if (g_nfields < MAX_FIELDS-1)
                g_fields[g_nfields++] = xstrndup(start, p - start);
            if (!*p) break;
            p++;
        }
    } else {
        /* Regex FS */
        regex_t re;
        if (regcomp(&re, fs, REG_EXTENDED) != 0) {
            die("invalid FS regex: %s", fs);
        }
        char *p = line_copy;
        while (*p) {
            regmatch_t m;
            if (regexec(&re, p, 1, &m, 0) == 0) {
                if (g_nfields < MAX_FIELDS-1)
                    g_fields[g_nfields++] = xstrndup(p, m.rm_so);
                p += m.rm_eo;
                if (m.rm_eo == m.rm_so) { /* zero-length match */
                    if (g_nfields < MAX_FIELDS-1)
                        g_fields[g_nfields++] = xstrndup(p, 1);
                    p++;
                }
            } else {
                if (g_nfields < MAX_FIELDS-1)
                    g_fields[g_nfields++] = xstrdup(p);
                break;
            }
        }
        regfree(&re);
    }
}

static void rebuild_line(void) {
    char buf[MAX_LINE];
    buf[0] = '\0';
    for (int i = 0; i < g_nfields; i++) {
        if (i > 0) strncat(buf, g_ofs, sizeof(buf)-strlen(buf)-1);
        strncat(buf, g_fields[i] ? g_fields[i] : "", sizeof(buf)-strlen(buf)-1);
    }
    strncpy(g_line, buf, MAX_LINE-1);
}

/* ============================================================
 * LEXER
 * ============================================================ */
static void lex_init(const char *src) {
    g_src = xstrdup(src);
    g_srclen = strlen(g_src);
    g_srcpos = 0;
    g_srcline = 1;
    g_tokens = xmalloc(sizeof(Token) * MAX_TOKENS);
    g_ntokens = 0;
    g_tokpos = 0;
}

static int lex_peek(int off) {
    int pos = g_srcpos + off;
    if (pos < g_srclen) return (unsigned char)g_src[pos];
    return -1;
}

static int lex_char(void) {
    if (g_srcpos >= g_srclen) return -1;
    return (unsigned char)g_src[g_srcpos++];
}

static void emit_token(TokenType t, const char *s, double n, int line) {
    if (g_ntokens >= MAX_TOKENS) die("too many tokens");
    g_tokens[g_ntokens].type = t;
    g_tokens[g_ntokens].str  = s ? xstrdup(s) : NULL;
    g_tokens[g_ntokens].num  = n;
    g_tokens[g_ntokens].line = line;
    g_ntokens++;
}

static struct { const char *word; TokenType type; } g_keywords[] = {
    {"BEGIN",    TK_BEGIN},
    {"END",      TK_END},
    {"if",       TK_IF},
    {"else",     TK_ELSE},
    {"while",    TK_WHILE},
    {"for",      TK_FOR},
    {"do",       TK_DO},
    {"break",    TK_BREAK},
    {"continue", TK_CONTINUE},
    {"return",   TK_RETURN},
    {"exit",     TK_EXIT},
    {"print",    TK_PRINT},
    {"printf",   TK_PRINTF},
    {"getline",  TK_GETLINE},
    {"delete",   TK_DELETE},
    {"in",       TK_IN},
    {"next",     TK_NEXT},
    {"nextfile", TK_NEXTFILE},
    {"function", TK_FUNCTION},
    {NULL, TK_EOF}
};

static void lex_all(void) {
    while (1) {
        int line = g_srcline;
        int c = lex_peek(0);
        if (c == -1) { emit_token(TK_EOF, NULL, 0, line); break; }

        /* Whitespace (not newlines) */
        if (c == ' ' || c == '\t' || c == '\r') { lex_char(); continue; }

        /* Comments */
        if (c == '#') {
            while (g_srcpos < g_srclen && g_src[g_srcpos] != '\n') g_srcpos++;
            continue;
        }

        /* Newlines */
        if (c == '\n') {
            lex_char(); g_srcline++;
            /* Only emit newline if last meaningful token can end a statement */
            if (g_ntokens > 0) {
                TokenType lt = g_tokens[g_ntokens-1].type;
                if (lt == TK_NUMBER || lt == TK_STRING || lt == TK_IDENT ||
                    lt == TK_RBRACE || lt == TK_RBRACKET || lt == TK_RPAREN ||
                    lt == TK_INC || lt == TK_DEC ||
                    lt == TK_BREAK || lt == TK_CONTINUE ||
                    lt == TK_RETURN || lt == TK_NEXT || lt == TK_NEXTFILE ||
                    lt == TK_EXIT || lt == TK_GETLINE) {
                    emit_token(TK_NEWLINE, NULL, 0, line);
                }
            }
            continue;
        }

        /* String literals */
        if (c == '"') {
            lex_char();
            char buf[MAX_LINE]; int bi = 0;
            while ((c = lex_peek(0)) != -1 && c != '"') {
                lex_char();
                if (c == '\\') {
                    int nc = lex_char();
                    switch(nc) {
                        case 'n': buf[bi++] = '\n'; break;
                        case 't': buf[bi++] = '\t'; break;
                        case 'r': buf[bi++] = '\r'; break;
                        case '\\': buf[bi++] = '\\'; break;
                        case '"': buf[bi++] = '"'; break;
                        case 'a': buf[bi++] = '\a'; break;
                        case 'b': buf[bi++] = '\b'; break;
                        case 'f': buf[bi++] = '\f'; break;
                        case 'v': buf[bi++] = '\v'; break;
                        case '/': buf[bi++] = '/'; break;
                        default:  buf[bi++] = '\\'; buf[bi++] = nc; break;
                    }
                } else {
                    buf[bi++] = c;
                }
                if (bi >= MAX_LINE-2) break;
            }
            lex_char(); /* closing " */
            buf[bi] = '\0';
            emit_token(TK_STRING, buf, 0, line);
            continue;
        }

        /* Regex literals /.../ */
        if (c == '/') {
            /* Check if it could be regex: after beginning, after = ~ ( etc */
            int is_regex = 0;
            if (g_ntokens == 0) is_regex = 1;
            else {
                TokenType lt = g_tokens[g_ntokens-1].type;
                if (lt == TK_ASSIGN || lt == TK_MATCH || lt == TK_NOTMATCH ||
                    lt == TK_LPAREN || lt == TK_COMMA || lt == TK_LBRACE ||
                    lt == TK_NEWLINE || lt == TK_SEMICOLON ||
                    lt == TK_AND || lt == TK_OR || lt == TK_BANG ||
                    lt == TK_EQ || lt == TK_NEQ || lt == TK_LT ||
                    lt == TK_GT || lt == TK_LE || lt == TK_GE)
                    is_regex = 1;
            }
            if (is_regex) {
                lex_char(); /* consume / */
                char buf[MAX_LINE]; int bi = 0;
                while ((c = lex_peek(0)) != -1 && c != '/') {
                    lex_char();
                    if (c == '\\') {
                        int nc = lex_char();
                        buf[bi++] = '\\'; buf[bi++] = nc;
                    } else {
                        buf[bi++] = c;
                    }
                    if (bi >= MAX_LINE-2) break;
                }
                lex_char(); /* closing / */
                buf[bi] = '\0';
                emit_token(TK_REGEX, buf, 0, line);
                continue;
            }
        }

        /* Numbers */
        if (isdigit(c) || (c == '.' && isdigit(lex_peek(1)))) {
            char buf[64]; int bi = 0;
            if (c == '0' && (lex_peek(1) == 'x' || lex_peek(1) == 'X')) {
                buf[bi++] = lex_char();
                buf[bi++] = lex_char();
                while (isxdigit(lex_peek(0))) buf[bi++] = lex_char();
            } else {
                while (isdigit(lex_peek(0)) || lex_peek(0) == '.') buf[bi++] = lex_char();
                if (lex_peek(0) == 'e' || lex_peek(0) == 'E') {
                    buf[bi++] = lex_char();
                    if (lex_peek(0) == '+' || lex_peek(0) == '-') buf[bi++] = lex_char();
                    while (isdigit(lex_peek(0))) buf[bi++] = lex_char();
                }
            }
            buf[bi] = '\0';
            emit_token(TK_NUMBER, NULL, strtod(buf, NULL), line);
            continue;
        }

        /* Identifiers and keywords */
        if (isalpha(c) || c == '_') {
            char buf[256]; int bi = 0;
            while (isalnum(lex_peek(0)) || lex_peek(0) == '_')
                buf[bi++] = lex_char();
            buf[bi] = '\0';
            /* Check keywords */
            TokenType tt = TK_IDENT;
            for (int i = 0; g_keywords[i].word; i++) {
                if (strcmp(buf, g_keywords[i].word) == 0) {
                    tt = g_keywords[i].type;
                    break;
                }
            }
            emit_token(tt, buf, 0, line);
            continue;
        }

        /* Operators and punctuation */
        lex_char();
        switch(c) {
            case '+':
                if (lex_peek(0)=='+') { lex_char(); emit_token(TK_INC,NULL,0,line); }
                else if (lex_peek(0)=='=') { lex_char(); emit_token(TK_PLUSEQ,NULL,0,line); }
                else emit_token(TK_PLUS,NULL,0,line);
                break;
            case '-':
                if (lex_peek(0)=='-') { lex_char(); emit_token(TK_DEC,NULL,0,line); }
                else if (lex_peek(0)=='=') { lex_char(); emit_token(TK_MINUSEQ,NULL,0,line); }
                else emit_token(TK_MINUS,NULL,0,line);
                break;
            case '*':
                if (lex_peek(0)=='=') { lex_char(); emit_token(TK_STAREQ,NULL,0,line); }
                else emit_token(TK_STAR,NULL,0,line);
                break;
            case '/':
                if (lex_peek(0)=='=') { lex_char(); emit_token(TK_SLASHEQ,NULL,0,line); }
                else emit_token(TK_SLASH,NULL,0,line);
                break;
            case '%':
                if (lex_peek(0)=='=') { lex_char(); emit_token(TK_PERCENTEQ,NULL,0,line); }
                else emit_token(TK_PERCENT,NULL,0,line);
                break;
            case '^':
                if (lex_peek(0)=='=') { lex_char(); emit_token(TK_CARETEQ,NULL,0,line); }
                else emit_token(TK_CARET,NULL,0,line);
                break;
            case '!':
                if (lex_peek(0)=='=') { lex_char(); emit_token(TK_NEQ,NULL,0,line); }
                else if (lex_peek(0)=='~') { lex_char(); emit_token(TK_NOTMATCH,NULL,0,line); }
                else emit_token(TK_BANG,NULL,0,line);
                break;
            case '=':
                if (lex_peek(0)=='=') { lex_char(); emit_token(TK_EQ,NULL,0,line); }
                else emit_token(TK_ASSIGN,NULL,0,line);
                break;
            case '<':
                if (lex_peek(0)=='=') { lex_char(); emit_token(TK_LE,NULL,0,line); }
                else emit_token(TK_LT,NULL,0,line);
                break;
            case '>':
                if (lex_peek(0)=='>') { lex_char(); emit_token(TK_APPEND,NULL,0,line); }
                else if (lex_peek(0)=='=') { lex_char(); emit_token(TK_GE,NULL,0,line); }
                else emit_token(TK_GT,NULL,0,line);
                break;
            case '&':
                if (lex_peek(0)=='&') { lex_char(); emit_token(TK_AND,NULL,0,line); }
                else emit_token(TK_AND,NULL,0,line); /* treat single & as && */
                break;
            case '|':
                if (lex_peek(0)=='|') { lex_char(); emit_token(TK_OR,NULL,0,line); }
                else emit_token(TK_PIPE,NULL,0,line);
                break;
            case '~': emit_token(TK_MATCH,NULL,0,line); break;
            case '?': emit_token(TK_TERNARY,NULL,0,line); break;
            case ':': emit_token(TK_COLON,NULL,0,line); break;
            case '(': emit_token(TK_LPAREN,NULL,0,line); break;
            case ')': emit_token(TK_RPAREN,NULL,0,line); break;
            case '{': emit_token(TK_LBRACE,NULL,0,line); break;
            case '}': emit_token(TK_RBRACE,NULL,0,line); break;
            case '[': emit_token(TK_LBRACKET,NULL,0,line); break;
            case ']': emit_token(TK_RBRACKET,NULL,0,line); break;
            case ';': emit_token(TK_SEMICOLON,NULL,0,line); break;
            case ',': emit_token(TK_COMMA,NULL,0,line); break;
            case '$': emit_token(TK_DOLLAR,NULL,0,line); break;
            case '\\':
                /* line continuation */
                if (lex_peek(0) == '\n') { lex_char(); g_srcline++; }
                break;
            default:
                break;
        }
    }
}

/* ============================================================
 * PARSER HELPERS
 * ============================================================ */
static Token *peek_tok(int off) {
    int pos = g_tokpos + off;
    if (pos >= g_ntokens) return &g_tokens[g_ntokens-1]; /* EOF */
    return &g_tokens[pos];
}

static Token *cur_tok(void) { return peek_tok(0); }
static Token *next_tok(void) { Token *t = cur_tok(); g_tokpos++; return t; }

static int check(TokenType t) { return cur_tok()->type == t; }

static int match_tok(TokenType t) {
    if (cur_tok()->type == t) { next_tok(); return 1; }
    return 0;
}

static void expect(TokenType t) {
    if (!match_tok(t))
        die("line %d: expected token %d, got %d ('%s')",
            cur_tok()->line, t, cur_tok()->type,
            cur_tok()->str ? cur_tok()->str : "");
}

static void skip_newlines(void) {
    while (cur_tok()->type == TK_NEWLINE || cur_tok()->type == TK_SEMICOLON)
        next_tok();
}

/* ============================================================
 * NODE ALLOCATION
 * ============================================================ */
static Node *new_node(NodeType t) {
    Node *n = xmalloc(sizeof(Node));
    memset(n, 0, sizeof(Node));
    n->type = t;
    n->line = cur_tok()->line;
    return n;
}

/* ============================================================
 * PARSER (recursive descent)
 * ============================================================ */
static Node *parse_expr(void);
static Node *parse_stmt(void);
static Node *parse_block(void);

/* Parse comma-separated expressions */
static Node **parse_args(int *nout, TokenType end) {
    Node **args = NULL; int nargs = 0;
    skip_newlines();
    while (!check(end) && !check(TK_EOF)) {
        args = xrealloc(args, sizeof(Node*) * (nargs+1));
        args[nargs++] = parse_expr();
        if (!match_tok(TK_COMMA)) break;
        skip_newlines();
    }
    *nout = nargs;
    return args;
}

static Node *parse_primary(void) {
    Token *t = cur_tok();
    Node *n;

    if (t->type == TK_NUMBER) {
        next_tok();
        n = new_node(ND_NUM);
        n->numval = t->num;
        return n;
    }

    if (t->type == TK_STRING) {
        next_tok();
        n = new_node(ND_STR);
        n->strval = xstrdup(t->str);
        return n;
    }

    if (t->type == TK_REGEX) {
        next_tok();
        n = new_node(ND_REGEX);
        n->strval = xstrdup(t->str);
        return n;
    }

    if (t->type == TK_DOLLAR) {
        next_tok();
        n = new_node(ND_FIELD);
        n->left = parse_primary(); /* $expr */
        return n;
    }

    if (t->type == TK_LPAREN) {
        next_tok();
        n = parse_expr();
        expect(TK_RPAREN);
        return n;
    }

    if (t->type == TK_BANG) {
        next_tok();
        n = new_node(ND_UNOP);
        n->op = *t;
        n->left = parse_primary();
        return n;
    }

    if (t->type == TK_MINUS) {
        next_tok();
        n = new_node(ND_UNOP);
        n->op = *t;
        n->left = parse_primary();
        return n;
    }

    if (t->type == TK_PLUS) {
        next_tok();
        n = new_node(ND_UNOP);
        n->op = *t;
        n->left = parse_primary();
        return n;
    }

    if (t->type == TK_INC || t->type == TK_DEC) {
        next_tok();
        n = new_node(ND_INC);
        n->op = *t;
        n->flags = 1; /* pre */
        n->left = parse_primary();
        return n;
    }

    if (t->type == TK_IDENT) {
        next_tok();
        /* function call? */
        if (check(TK_LPAREN)) {
            next_tok();
            n = new_node(ND_CALL);
            n->strval = xstrdup(t->str);
            int nargs = 0;
            if (!check(TK_RPAREN)) {
                n->children = parse_args(&nargs, TK_RPAREN);
            }
            n->nchildren = nargs;
            expect(TK_RPAREN);
            return n;
        }
        /* array ref? */
        if (check(TK_LBRACKET)) {
            next_tok();
            n = new_node(ND_ARRAY_REF);
            n->strval = xstrdup(t->str);
            int nkeys = 0;
            n->children = parse_args(&nkeys, TK_RBRACKET);
            n->nchildren = nkeys;
            expect(TK_RBRACKET);
            /* post increment/decrement */
            if (check(TK_INC) || check(TK_DEC)) {
                Token op = *cur_tok(); next_tok();
                Node *inc = new_node(ND_INC);
                inc->op = op; inc->flags = 0; /* post */
                inc->left = n;
                return inc;
            }
            return n;
        }

        n = new_node(ND_IDENT);
        n->strval = xstrdup(t->str);
        /* post inc/dec */
        if (check(TK_INC) || check(TK_DEC)) {
            Token op = *cur_tok(); next_tok();
            Node *inc = new_node(ND_INC);
            inc->op = op; inc->flags = 0;
            inc->left = n;
            return inc;
        }
        return n;
    }

    if (t->type == TK_GETLINE) {
        next_tok();
        n = new_node(ND_GETLINE);
        return n;
    }

    /* fallback */
    n = new_node(ND_EMPTY);
    return n;
}

static Node *parse_power(void) {
    Node *left = parse_primary();
    while (check(TK_CARET)) {
        Token op = *cur_tok(); next_tok();
        Node *n = new_node(ND_BINOP);
        n->op = op; n->left = left; n->right = parse_primary();
        left = n;
    }
    return left;
}

static Node *parse_unary(void) {
    /* Already handled in primary; this just calls power */
    return parse_power();
}

static Node *parse_mul(void) {
    Node *left = parse_unary();
    while (check(TK_STAR) || check(TK_SLASH) || check(TK_PERCENT)) {
        Token op = *cur_tok(); next_tok();
        Node *n = new_node(ND_BINOP);
        n->op = op; n->left = left; n->right = parse_unary();
        left = n;
    }
    return left;
}

static Node *parse_add(void) {
    Node *left = parse_mul();
    while (check(TK_PLUS) || check(TK_MINUS)) {
        Token op = *cur_tok(); next_tok();
        Node *n = new_node(ND_BINOP);
        n->op = op; n->left = left; n->right = parse_mul();
        left = n;
    }
    return left;
}

/* String concatenation: two adjacent non-operator expressions */
static Node *parse_concat(void) {
    Node *left = parse_add();
    while (1) {
        /* Heuristic: if next token can start an expression, it's concat */
        TokenType tt = cur_tok()->type;
        if (tt == TK_NUMBER || tt == TK_STRING || tt == TK_IDENT ||
            tt == TK_LPAREN || tt == TK_DOLLAR || tt == TK_REGEX) {
            Node *n = new_node(ND_CONCAT);
            n->left = left; n->right = parse_add();
            left = n;
        } else break;
    }
    return left;
}

static Node *parse_cmp(void) {
    Node *left = parse_concat();
    while (check(TK_LT) || check(TK_GT) || check(TK_LE) || check(TK_GE)) {
        Token op = *cur_tok(); next_tok();
        Node *n = new_node(ND_BINOP);
        n->op = op; n->left = left; n->right = parse_concat();
        left = n;
    }
    return left;
}

static Node *parse_eq(void) {
    Node *left = parse_cmp();
    while (check(TK_EQ) || check(TK_NEQ)) {
        Token op = *cur_tok(); next_tok();
        Node *n = new_node(ND_BINOP);
        n->op = op; n->left = left; n->right = parse_cmp();
        left = n;
    }
    return left;
}

static Node *parse_match_expr(void) {
    Node *left = parse_eq();
    while (check(TK_MATCH) || check(TK_NOTMATCH)) {
        Token op = *cur_tok(); next_tok();
        Node *n = new_node(ND_MATCH_EXPR);
        n->op = op; n->left = left; n->right = parse_eq();
        left = n;
    }
    return left;
}

static Node *parse_in_expr(void) {
    Node *left = parse_match_expr();
    if (check(TK_IN)) {
        next_tok();
        Node *n = new_node(ND_IN);
        n->left = left;
        n->right = new_node(ND_IDENT);
        n->right->strval = xstrdup(cur_tok()->str);
        next_tok();
        return n;
    }
    return left;
}

static Node *parse_and(void) {
    Node *left = parse_in_expr();
    while (check(TK_AND)) {
        Token op = *cur_tok(); next_tok();
        Node *n = new_node(ND_BINOP);
        n->op = op; n->left = left; n->right = parse_in_expr();
        left = n;
    }
    return left;
}

static Node *parse_or(void) {
    Node *left = parse_and();
    while (check(TK_OR)) {
        Token op = *cur_tok(); next_tok();
        Node *n = new_node(ND_BINOP);
        n->op = op; n->left = left; n->right = parse_and();
        left = n;
    }
    return left;
}

static Node *parse_ternary(void) {
    Node *cond = parse_or();
    if (check(TK_TERNARY)) {
        next_tok();
        Node *n = new_node(ND_TERNARY);
        n->cond = cond;
        n->left = parse_expr();
        expect(TK_COLON);
        n->right = parse_expr();
        return n;
    }
    return cond;
}

static Node *parse_assign(void) {
    Node *left = parse_ternary();
    if (check(TK_ASSIGN) || check(TK_PLUSEQ) || check(TK_MINUSEQ) ||
        check(TK_STAREQ) || check(TK_SLASHEQ) || check(TK_PERCENTEQ) ||
        check(TK_CARETEQ)) {
        Token op = *cur_tok(); next_tok();
        Node *n = new_node(op.type == TK_ASSIGN ? ND_ASSIGN : ND_OPASSIGN);
        n->op = op; n->left = left; n->right = parse_assign();
        return n;
    }
    return left;
}

static Node *parse_expr(void) {
    return parse_assign();
}

/* Parse print output redirection */
static void parse_print_redir(Node *n) {
    if (check(TK_GT)) {
        next_tok();
        n->right = parse_expr();
        n->type = ND_PRINT; /* redirect to file */
        n->flags = 1; /* write */
    } else if (check(TK_APPEND)) {
        next_tok();
        n->right = parse_expr();
        n->flags = 2; /* append */
    } else if (check(TK_PIPE)) {
        next_tok();
        n->right = parse_expr();
        n->flags = 3; /* pipe */
    }
}

static Node *parse_stmt(void) {
    skip_newlines();
    Token *t = cur_tok();
    Node *n;

    if (t->type == TK_LBRACE) {
        return parse_block();
    }

    if (t->type == TK_IF) {
        next_tok();
        n = new_node(ND_IF);
        expect(TK_LPAREN);
        n->cond = parse_expr();
        expect(TK_RPAREN);
        skip_newlines();
        n->body = parse_stmt();
        skip_newlines();
        if (check(TK_ELSE)) {
            next_tok();
            skip_newlines();
            n->elsebody = parse_stmt();
        }
        return n;
    }

    if (t->type == TK_WHILE) {
        next_tok();
        n = new_node(ND_WHILE);
        expect(TK_LPAREN);
        n->cond = parse_expr();
        expect(TK_RPAREN);
        skip_newlines();
        n->body = parse_stmt();
        return n;
    }

    if (t->type == TK_DO) {
        next_tok();
        n = new_node(ND_DO_WHILE);
        n->body = parse_stmt();
        skip_newlines();
        expect(TK_WHILE);
        expect(TK_LPAREN);
        n->cond = parse_expr();
        expect(TK_RPAREN);
        return n;
    }

    if (t->type == TK_FOR) {
        next_tok(); expect(TK_LPAREN);
        /* Check for for (x in arr) */
        int saved = g_tokpos;
        /* Simple lookahead: ident followed by 'in' */
        if (cur_tok()->type == TK_IDENT && peek_tok(1)->type == TK_IN) {
            n = new_node(ND_FOR_IN);
            n->init = new_node(ND_IDENT);
            n->init->strval = xstrdup(cur_tok()->str);
            next_tok(); next_tok(); /* skip ident and 'in' */
            n->cond = new_node(ND_IDENT);
            n->cond->strval = xstrdup(cur_tok()->str);
            next_tok();
            expect(TK_RPAREN);
            n->body = parse_stmt();
            return n;
        }
        g_tokpos = saved;

        n = new_node(ND_FOR);
        if (!check(TK_SEMICOLON)) n->init = parse_expr();
        expect(TK_SEMICOLON);
        if (!check(TK_SEMICOLON)) n->cond = parse_expr();
        expect(TK_SEMICOLON);
        if (!check(TK_RPAREN)) n->incr = parse_expr();
        expect(TK_RPAREN);
        skip_newlines();
        n->body = parse_stmt();
        return n;
    }

    if (t->type == TK_PRINT || t->type == TK_PRINTF) {
        int is_printf = (t->type == TK_PRINTF);
        next_tok();
        n = new_node(ND_PRINT);
        int nargs = 0;
        /* print with parens */
        if (check(TK_LPAREN)) {
            int saved2 = g_tokpos;
            next_tok();
            /* parse exprs */
            if (!check(TK_RPAREN)) {
                n->children = parse_args(&nargs, TK_RPAREN);
            }
            n->nchildren = nargs;
            expect(TK_RPAREN);
        } else {
            /* parse up to newline/semicolon/pipe/redirect */
            Node **args = NULL; nargs = 0;
            while (!check(TK_NEWLINE) && !check(TK_SEMICOLON) &&
                   !check(TK_RBRACE) && !check(TK_EOF) &&
                   !check(TK_GT) && !check(TK_APPEND) && !check(TK_PIPE)) {
                args = xrealloc(args, sizeof(Node*) * (nargs+1));
                args[nargs++] = parse_expr();
                if (!match_tok(TK_COMMA)) break;
            }
            n->children = args;
            n->nchildren = nargs;
        }
        n->flags = is_printf ? 4 : 0;
        parse_print_redir(n);
        return n;
    }

    if (t->type == TK_RETURN) {
        next_tok();
        n = new_node(ND_RETURN);
        if (!check(TK_NEWLINE) && !check(TK_SEMICOLON) &&
            !check(TK_RBRACE) && !check(TK_EOF))
            n->left = parse_expr();
        return n;
    }

    if (t->type == TK_BREAK) { next_tok(); return new_node(ND_BREAK); }
    if (t->type == TK_CONTINUE) { next_tok(); return new_node(ND_CONTINUE); }
    if (t->type == TK_NEXT) { next_tok(); return new_node(ND_NEXT); }
    if (t->type == TK_NEXTFILE) { next_tok(); return new_node(ND_NEXTFILE); }
    if (t->type == TK_EXIT) {
        next_tok();
        n = new_node(ND_EXIT);
        if (!check(TK_NEWLINE) && !check(TK_SEMICOLON) &&
            !check(TK_RBRACE) && !check(TK_EOF))
            n->left = parse_expr();
        return n;
    }

    if (t->type == TK_DELETE) {
        next_tok();
        n = new_node(ND_DELETE);
        n->left = parse_expr();
        return n;
    }

    if (t->type == TK_GETLINE) {
        next_tok();
        return new_node(ND_GETLINE);
    }

    /* Expression statement */
    n = new_node(ND_EMPTY);
    if (!check(TK_NEWLINE) && !check(TK_SEMICOLON) && !check(TK_EOF)) {
        Node *e = parse_expr();
        free(n);
        n = e;
    }
    return n;
}

static Node *parse_block(void) {
    expect(TK_LBRACE);
    Node *n = new_node(ND_BLOCK);
    n->children = NULL; n->nchildren = 0;
    skip_newlines();
    while (!check(TK_RBRACE) && !check(TK_EOF)) {
        Node *s = parse_stmt();
        n->children = xrealloc(n->children, sizeof(Node*) * (n->nchildren+1));
        n->children[n->nchildren++] = s;
        while (check(TK_NEWLINE) || check(TK_SEMICOLON)) next_tok();
    }
    expect(TK_RBRACE);
    return n;
}

static void parse_function(void) {
    expect(TK_FUNCTION);
    if (!check(TK_IDENT)) die("expected function name");
    char *name = xstrdup(cur_tok()->str); next_tok();
    expect(TK_LPAREN);
    char **params = NULL; int nparam = 0;
    while (!check(TK_RPAREN) && !check(TK_EOF)) {
        if (!check(TK_IDENT)) die("expected parameter name");
        params = xrealloc(params, sizeof(char*) * (nparam+1));
        params[nparam++] = xstrdup(cur_tok()->str);
        next_tok();
        if (!match_tok(TK_COMMA)) break;
    }
    expect(TK_RPAREN);
    skip_newlines();
    Node *body = parse_block();
    if (g_nfunctions >= MAX_FUNCTIONS) die("too many functions");
    g_functions[g_nfunctions].name   = name;
    g_functions[g_nfunctions].params = params;
    g_functions[g_nfunctions].nparam = nparam;
    g_functions[g_nfunctions].body   = body;
    g_nfunctions++;
}

static void parse_program(void) {
    skip_newlines();
    while (!check(TK_EOF)) {
        if (check(TK_FUNCTION)) {
            parse_function();
            skip_newlines();
            continue;
        }

        Rule *r = &g_rules[g_nrules];
        memset(r, 0, sizeof(Rule));

        if (check(TK_BEGIN)) {
            next_tok(); r->is_begin = 1;
        } else if (check(TK_END)) {
            next_tok(); r->is_end = 1;
        } else if (!check(TK_LBRACE)) {
            r->pattern = parse_expr();
            /* Range pattern? */
            if (check(TK_COMMA)) {
                next_tok(); skip_newlines();
                r->pattern2 = parse_expr();
            }
        }

        skip_newlines();
        if (check(TK_LBRACE)) {
            r->action = parse_block();
        } else if (r->pattern) {
            /* pattern without action: default print */
            r->action = NULL;
        }

        if (g_nrules >= MAX_RULES) die("too many rules");
        g_nrules++;
        skip_newlines();
    }
}

/* ============================================================
 * BUILT-IN FUNCTIONS (string, math, I/O)
 * ============================================================ */
static Value eval_node(Node *n, VarEntry **locals, int nlocals);

static Value call_builtin(const char *name, Node **args, int nargs,
                          VarEntry **locals, int nlocals) {
    /* Helper macros */
#define EVAL(i) eval_node(args[i], locals, nlocals)
#define ESTR(i) ({ Value _v = EVAL(i); char *_s = val_to_str_alloc(&_v); val_free(&_v); _s; })
#define ENUM(i) ({ Value _v = EVAL(i); double _n = val_to_num(&_v); val_free(&_v); _n; })

    if (strcmp(name, "length") == 0) {
        if (nargs == 0) return val_num(strlen(g_line));
        Value v = EVAL(0);
        double len;
        if (v.type == VAL_UNDEF) {
            /* Check if it's an array */
            if (args[0]->type == ND_IDENT) {
                Array *arr = array_find(args[0]->strval);
                if (arr) { val_free(&v); return val_num(arr->size); }
            }
            len = 0;
        } else {
            char buf[64];
            char *s = val_to_str_buf(&v, buf, sizeof(buf));
            len = strlen(s);
        }
        val_free(&v);
        return val_num(len);
    }

    if (strcmp(name, "substr") == 0) {
        if (nargs < 2) return val_str("");
        char *s = ESTR(0);
        int start = (int)ENUM(1) - 1; /* AWK 1-indexed */
        int slen = strlen(s);
        if (start < 0) start = 0;
        if (start > slen) { free(s); return val_str(""); }
        int n = (nargs >= 3) ? (int)ENUM(2) : slen - start;
        if (start + n > slen) n = slen - start;
        if (n < 0) n = 0;
        char *res = xstrndup(s + start, n);
        free(s);
        Value v = val_str(res); free(res); return v;
    }

    if (strcmp(name, "index") == 0) {
        if (nargs < 2) return val_num(0);
        char *h = ESTR(0), *nd = ESTR(1);
        char *p = strstr(h, nd);
        double r = p ? (double)(p - h + 1) : 0;
        free(h); free(nd);
        return val_num(r);
    }

    if (strcmp(name, "split") == 0) {
        if (nargs < 2) return val_num(0);
        char *s = ESTR(0);
        /* get array name */
        char *arrname = args[1]->type == ND_IDENT ? args[1]->strval : "SPLIT_TMP";
        char *fs2 = (nargs >= 3) ? ESTR(2) : xstrdup(g_fs);
        Array *arr = array_get_or_create(arrname);
        array_delete_all(arr);
        /* do split */
        char tmp_line[MAX_LINE];
        strncpy(tmp_line, g_line, MAX_LINE-1);
        strncpy(g_line, s, MAX_LINE-1);
        char saved_fs[256];
        strcpy(saved_fs, g_fs);
        strcpy(g_fs, fs2);
        char *saved_fields[MAX_FIELDS];
        int saved_nf = g_nfields;
        for (int i = 0; i < g_nfields; i++) saved_fields[i] = g_fields[i];
        split_fields(s, fs2);
        int count = g_nfields;
        for (int i = 0; i < g_nfields; i++) {
            char key[32]; sprintf(key, "%d", i+1);
            Value *vp = array_ref(arr, key);
            val_free(vp); *vp = val_str(g_fields[i] ? g_fields[i] : "");
        }
        /* restore */
        for (int i = 0; i < g_nfields; i++) { free(g_fields[i]); g_fields[i] = NULL; }
        g_nfields = saved_nf;
        for (int i = 0; i < saved_nf; i++) g_fields[i] = saved_fields[i];
        strncpy(g_line, tmp_line, MAX_LINE-1);
        strcpy(g_fs, saved_fs);
        free(s); free(fs2);
        return val_num(count);
    }

    if (strcmp(name, "sub") == 0 || strcmp(name, "gsub") == 0) {
        if (nargs < 2) return val_num(0);
        /* For regex args, use the pattern string directly */
        char *pat = (args[0]->type == ND_REGEX || args[0]->type == ND_STR)
                    ? xstrdup(args[0]->strval) : ESTR(0);
        char *repl = ESTR(1);
        /* target */
        char *target;
        Value tval;
        if (nargs >= 3) {
            tval = EVAL(2);
            target = val_to_str_alloc(&tval);
        } else {
            target = xstrdup(g_line);
            tval = val_undef();
        }
        regex_t re;
        if (regcomp(&re, pat, REG_EXTENDED) != 0) {
            free(pat); free(repl); free(target); val_free(&tval);
            return val_num(0);
        }
        char out[MAX_LINE]; out[0] = '\0';
        char *p = target;
        int count = 0;
        int do_all = strcmp(name, "gsub") == 0;
        while (*p) {
            regmatch_t m;
            if (regexec(&re, p, 1, &m, 0) == 0) {
                strncat(out, p, m.rm_so);
                /* Handle & in replacement */
                for (char *r = repl; *r; r++) {
                    if (*r == '&') {
                        char tmp2[MAX_LINE]; 
                        snprintf(tmp2, m.rm_eo - m.rm_so + 1, "%s", p + m.rm_so);
                        strncat(out, tmp2, sizeof(out)-strlen(out)-1);
                    } else if (*r == '\\' && *(r+1)) {
                        r++;
                        char tmp2[3] = {*r, 0};
                        strncat(out, tmp2, sizeof(out)-strlen(out)-1);
                    } else {
                        char tmp2[2] = {*r, 0};
                        strncat(out, tmp2, sizeof(out)-strlen(out)-1);
                    }
                }
                p += m.rm_eo;
                count++;
                if (!do_all) { strncat(out, p, sizeof(out)-strlen(out)-1); break; }
                if (m.rm_eo == m.rm_so) {
                    if (*p) { char tmp2[2]={*p,0}; strncat(out,tmp2,sizeof(out)-strlen(out)-1); p++; }
                    else break;
                }
            } else {
                strncat(out, p, sizeof(out)-strlen(out)-1);
                break;
            }
        }
        regfree(&re);
        /* assign back */
        if (nargs >= 3 && args[2]->type == ND_IDENT) {
            var_set(args[2]->strval, val_str(out));
        } else if (nargs >= 3 && args[2]->type == ND_FIELD) {
            Value fv = EVAL(2);
            int fi = (int)val_to_num(&fv);
            val_free(&fv);
            if (fi == 0) { strncpy(g_line, out, MAX_LINE-1); split_fields(g_line, g_fs); }
            else if (fi >= 1 && fi <= g_nfields) {
                free(g_fields[fi-1]); g_fields[fi-1] = xstrdup(out); rebuild_line();
            }
        } else {
            strncpy(g_line, out, MAX_LINE-1);
            split_fields(g_line, g_fs);
        }
        val_free(&tval);
        free(target); free(pat); free(repl);
        return val_num(count);
    }

    if (strcmp(name, "match") == 0) {
        if (nargs < 2) return val_num(0);
        char *s = ESTR(0);
        char *pat = (args[1]->type == ND_REGEX || args[1]->type == ND_STR)
                    ? xstrdup(args[1]->strval) : ESTR(1);
        regex_t re;
        if (regcomp(&re, pat, REG_EXTENDED) != 0) {
            free(s); free(pat); return val_num(0);
        }
        regmatch_t m;
        double result;
        if (regexec(&re, s, 1, &m, 0) == 0) {
            var_set("RSTART", val_num(m.rm_so + 1));
            var_set("RLENGTH", val_num(m.rm_eo - m.rm_so));
            result = m.rm_so + 1;
        } else {
            var_set("RSTART", val_num(0));
            var_set("RLENGTH", val_num(-1));
            result = 0;
        }
        regfree(&re);
        free(s); free(pat);
        return val_num(result);
    }

    if (strcmp(name, "sprintf") == 0) {
        if (nargs == 0) return val_str("");
        char *fmt = ESTR(0);
        char out[MAX_LINE]; out[0] = '\0';
        /* Simple sprintf-like implementation */
        char *p = fmt; int ai = 1;
        char *outp = out;
        size_t outrem = sizeof(out)-1;
        while (*p && outrem > 0) {
            if (*p == '%') {
                char spec[64]; int si = 0;
                spec[si++] = '%';
                p++;
                /* flags */
                while (*p && (*p == '-' || *p == '+' || *p == ' ' || *p == '0' || *p == '#'))
                    spec[si++] = *p++;
                /* width */
                while (*p && isdigit((unsigned char)*p)) spec[si++] = *p++;
                /* precision */
                if (*p == '.') { spec[si++] = *p++; while (*p && isdigit((unsigned char)*p)) spec[si++] = *p++; }
                /* conversion */
                if (*p) {
                    spec[si++] = *p;
                    spec[si] = '\0';
                    char tmp3[256];
                    if (*p == 'd' || *p == 'i' || *p == 'o' || *p == 'x' || *p == 'X' || *p == 'u') {
                        long lv = ai < nargs ? (long)ENUM(ai++) : 0;
                        snprintf(tmp3, sizeof(tmp3), spec, lv);
                    } else if (*p == 'f' || *p == 'e' || *p == 'E' || *p == 'g' || *p == 'G') {
                        double dv = ai < nargs ? ENUM(ai++) : 0.0;
                        snprintf(tmp3, sizeof(tmp3), spec, dv);
                    } else if (*p == 's') {
                        char *sv = ai < nargs ? ESTR(ai++) : xstrdup("");
                        snprintf(tmp3, sizeof(tmp3), spec, sv);
                        free(sv);
                    } else if (*p == 'c') {
                        int cv = ai < nargs ? (int)ENUM(ai++) : 0;
                        snprintf(tmp3, sizeof(tmp3), "%c", cv);
                    } else {
                        tmp3[0] = '%'; tmp3[1] = *p; tmp3[2] = '\0';
                    }
                    size_t tl = strlen(tmp3);
                    if (tl > outrem) tl = outrem;
                    memcpy(outp, tmp3, tl); outp += tl; outrem -= tl;
                    p++;
                }
            } else if (*p == '\\') {
                p++;
                char c2 = 0;
                switch(*p) {
                    case 'n': c2='\n'; break; case 't': c2='\t'; break;
                    case 'r': c2='\r'; break; case '\\': c2='\\'; break;
                    default: *outp++='\\'; outrem--; c2=*p; break;
                }
                if (c2 && outrem > 0) { *outp++ = c2; outrem--; }
                if (*p) p++;
            } else {
                *outp++ = *p++; outrem--;
            }
        }
        *outp = '\0';
        free(fmt);
        return val_str(out);
    }

    if (strcmp(name, "tolower") == 0) {
        char *s = nargs > 0 ? ESTR(0) : xstrdup("");
        for (int i = 0; s[i]; i++) s[i] = tolower((unsigned char)s[i]);
        Value v = val_str(s); free(s); return v;
    }
    if (strcmp(name, "toupper") == 0) {
        char *s = nargs > 0 ? ESTR(0) : xstrdup("");
        for (int i = 0; s[i]; i++) s[i] = toupper((unsigned char)s[i]);
        Value v = val_str(s); free(s); return v;
    }

    if (strcmp(name, "int") == 0) {
        double n = nargs > 0 ? ENUM(0) : 0;
        return val_num((double)(long long)n);
    }

    if (strcmp(name, "sin") == 0)  return val_num(sin(nargs>0 ? ENUM(0) : 0));
    if (strcmp(name, "cos") == 0)  return val_num(cos(nargs>0 ? ENUM(0) : 0));
    if (strcmp(name, "atan2") == 0) {
        double y = nargs > 0 ? ENUM(0) : 0;
        double x2 = nargs > 1 ? ENUM(1) : 1;
        return val_num(atan2(y, x2));
    }
    if (strcmp(name, "exp") == 0)  return val_num(exp(nargs>0 ? ENUM(0) : 0));
    if (strcmp(name, "log") == 0)  return val_num(log(nargs>0 ? ENUM(0) : 1));
    if (strcmp(name, "sqrt") == 0) return val_num(sqrt(nargs>0 ? ENUM(0) : 0));
    if (strcmp(name, "rand") == 0) return val_num((double)rand() / ((double)RAND_MAX+1.0));
    if (strcmp(name, "srand") == 0) {
        static int last_seed = 1;
        int old = last_seed;
        last_seed = nargs > 0 ? (int)ENUM(0) : (int)time(NULL);
        srand(last_seed);
        return val_num(old);
    }
    if (strcmp(name, "systime") == 0) {
        return val_num((double)time(NULL));
    }
    if (strcmp(name, "system") == 0) {
        if (nargs == 0) return val_num(-1);
        char *cmd = ESTR(0);
        int r = system(cmd);
        free(cmd);
        return val_num(r);
    }
    if (strcmp(name, "gensub") == 0) {
        /* gensub(regexp, replacement, how [, target]) */
        if (nargs < 3) return val_str("");
        char *pat = (args[0]->type == ND_REGEX || args[0]->type == ND_STR)
                    ? xstrdup(args[0]->strval) : ESTR(0);
        char *repl = ESTR(1);
        char *how  = ESTR(2);
        char *target = (nargs >= 4) ? ESTR(3) : xstrdup(g_line);
        int do_all2 = (how[0] == 'g' || how[0] == 'G');
        regex_t re2;
        char out2[MAX_LINE]; out2[0] = '\0';
        if (regcomp(&re2, pat, REG_EXTENDED) == 0) {
            char *p2 = target;
            while (*p2) {
                regmatch_t m2;
                if (regexec(&re2, p2, 1, &m2, 0) == 0) {
                    strncat(out2, p2, m2.rm_so);
                    for (char *r = repl; *r; r++) {
                        if (*r == '&') {
                            char tmp4[MAX_LINE];
                            snprintf(tmp4, m2.rm_eo - m2.rm_so + 1, "%s", p2 + m2.rm_so);
                            strncat(out2, tmp4, sizeof(out2)-strlen(out2)-1);
                        } else {
                            char tmp4[2]={*r,0};
                            strncat(out2, tmp4, sizeof(out2)-strlen(out2)-1);
                        }
                    }
                    p2 += m2.rm_eo;
                    if (!do_all2) { strncat(out2, p2, sizeof(out2)-strlen(out2)-1); break; }
                    if (m2.rm_eo == m2.rm_so) {
                        if (*p2) { char t5[2]={*p2,0}; strncat(out2,t5,sizeof(out2)-strlen(out2)-1); p2++; }
                        else break;
                    }
                } else { strncat(out2, p2, sizeof(out2)-strlen(out2)-1); break; }
            }
            regfree(&re2);
        }
        free(pat); free(repl); free(how); free(target);
        return val_str(out2);
    }

    /* Unknown builtin: try user function */
    return val_undef();
#undef EVAL
#undef ESTR
#undef ENUM
}

/* ============================================================
 * EVALUATOR
 * ============================================================ */
static Value eval_node(Node *n, VarEntry **locals, int nlocals) {
    if (!n) return val_undef();

    /* Check local variables first */
    /* Helper to get/set lvalue */
    switch (n->type) {

    case ND_EMPTY: return val_undef();
    case ND_NUM: return val_num(n->numval);
    case ND_STR: return val_str(n->strval ? n->strval : "");

    case ND_REGEX: {
        /* Standalone regex: match against $0 */
        regex_t re;
        if (regcomp(&re, n->strval, REG_EXTENDED|REG_NOSUB) != 0) return val_num(0);
        int r = regexec(&re, g_line, 0, NULL, 0) == 0 ? 1 : 0;
        regfree(&re);
        return val_num(r);
    }

    case ND_IDENT: {
        /* Check locals */
        for (int i = 0; i < nlocals; i++) {
            if (locals[i] && strcmp(locals[i]->name, n->strval) == 0)
                return val_copy(&locals[i]->val);
        }
        /* Built-in vars */
        if (strcmp(n->strval, "NR") == 0)  return val_num(g_nr);
        if (strcmp(n->strval, "NF") == 0)  return val_num(g_nfields);
        if (strcmp(n->strval, "FNR") == 0) return val_num(g_fnr);
        if (strcmp(n->strval, "FS") == 0)  return val_str(g_fs);
        if (strcmp(n->strval, "OFS") == 0) return val_str(g_ofs);
        if (strcmp(n->strval, "ORS") == 0) return val_str(g_ors);
        if (strcmp(n->strval, "RS") == 0)  return val_str(g_rs);
        if (strcmp(n->strval, "SUBSEP") == 0) return val_str(g_subsep);
        if (strcmp(n->strval, "FILENAME") == 0) return val_str(g_filename ? g_filename : "");
        return var_get(n->strval);
    }

    case ND_FIELD: {
        Value idx = eval_node(n->left, locals, nlocals);
        int fi = (int)val_to_num(&idx);
        val_free(&idx);
        if (fi == 0) return val_str(g_line);
        if (fi >= 1 && fi <= g_nfields && g_fields[fi-1])
            return val_str(g_fields[fi-1]);
        return val_str("");
    }

    case ND_ARRAY_REF: {
        /* Build key from children */
        char key[MAX_LINE]; key[0] = '\0';
        Array *arr = NULL;
        /* Check locals for array */
        for (int i = 0; i < nlocals; i++) {
            if (locals[i] && strcmp(locals[i]->name, n->strval) == 0) {
                /* local array through var entry */
                break;
            }
        }
        arr = array_get_or_create(n->strval);
        for (int i = 0; i < n->nchildren; i++) {
            if (i > 0) strncat(key, g_subsep, sizeof(key)-strlen(key)-1);
            Value kv = eval_node(n->children[i], locals, nlocals);
            char buf[64];
            char *ks = val_to_str_buf(&kv, buf, sizeof(buf));
            strncat(key, ks, sizeof(key)-strlen(key)-1);
            val_free(&kv);
        }
        Value *vp = array_ref(arr, key);
        return val_copy(vp);
    }

    case ND_IN: {
        /* (key in arr) */
        char key[MAX_LINE]; key[0] = '\0';
        Node *kn = n->left;
        if (kn->type == ND_ARRAY_REF) {
            /* multi-dimensional key */
            for (int i = 0; i < kn->nchildren; i++) {
                if (i > 0) strncat(key, g_subsep, sizeof(key)-strlen(key)-1);
                Value kv = eval_node(kn->children[i], locals, nlocals);
                char buf[64];
                strncat(key, val_to_str_buf(&kv, buf, sizeof(buf)), sizeof(key)-strlen(key)-1);
                val_free(&kv);
            }
        } else {
            Value kv = eval_node(kn, locals, nlocals);
            char buf[64];
            strncpy(key, val_to_str_buf(&kv, buf, sizeof(buf)), sizeof(key)-1);
            val_free(&kv);
        }
        Array *arr = array_find(n->right->strval);
        return val_num(array_in(arr, key) ? 1.0 : 0.0);
    }

    case ND_ASSIGN: {
        Value rval = eval_node(n->right, locals, nlocals);
        /* assign to lvalue */
        Node *lhs = n->left;
        if (lhs->type == ND_IDENT) {
            /* check locals */
            int found = 0;
            for (int i = 0; i < nlocals; i++) {
                if (locals[i] && strcmp(locals[i]->name, lhs->strval) == 0) {
                    val_free(&locals[i]->val);
                    locals[i]->val = val_copy(&rval);
                    found = 1; break;
                }
            }
            if (!found) {
                /* built-in */
                if (strcmp(lhs->strval, "FS") == 0) {
                    char *s = val_to_str_alloc(&rval);
                    strncpy(g_fs, s, sizeof(g_fs)-1); free(s);
                } else if (strcmp(lhs->strval, "OFS") == 0) {
                    char *s = val_to_str_alloc(&rval);
                    strncpy(g_ofs, s, sizeof(g_ofs)-1); free(s);
                } else if (strcmp(lhs->strval, "ORS") == 0) {
                    char *s = val_to_str_alloc(&rval);
                    strncpy(g_ors, s, sizeof(g_ors)-1); free(s);
                } else if (strcmp(lhs->strval, "RS") == 0) {
                    char *s = val_to_str_alloc(&rval);
                    strncpy(g_rs, s, sizeof(g_rs)-1); free(s);
                } else if (strcmp(lhs->strval, "NF") == 0) {
                    int newNF = (int)val_to_num(&rval);
                    while (g_nfields > newNF) {
                        free(g_fields[g_nfields-1]); g_fields[g_nfields-1] = NULL;
                        g_nfields--;
                    }
                    while (g_nfields < newNF) {
                        g_fields[g_nfields++] = xstrdup("");
                    }
                    rebuild_line();
                } else {
                    var_set(lhs->strval, val_copy(&rval));
                }
            }
        } else if (lhs->type == ND_FIELD) {
            Value fidx = eval_node(lhs->left, locals, nlocals);
            int fi = (int)val_to_num(&fidx);
            val_free(&fidx);
            char *s = val_to_str_alloc(&rval);
            if (fi == 0) {
                strncpy(g_line, s, MAX_LINE-1);
                split_fields(g_line, g_fs);
            } else {
                while (g_nfields < fi) g_fields[g_nfields++] = xstrdup("");
                free(g_fields[fi-1]); g_fields[fi-1] = xstrdup(s);
                rebuild_line();
            }
            free(s);
        } else if (lhs->type == ND_ARRAY_REF) {
            Array *arr = array_get_or_create(lhs->strval);
            char key[MAX_LINE]; key[0] = '\0';
            for (int i = 0; i < lhs->nchildren; i++) {
                if (i > 0) strncat(key, g_subsep, sizeof(key)-strlen(key)-1);
                Value kv = eval_node(lhs->children[i], locals, nlocals);
                char buf[64];
                strncat(key, val_to_str_buf(&kv, buf, sizeof(buf)), sizeof(key)-strlen(key)-1);
                val_free(&kv);
            }
            Value *vp = array_ref(arr, key);
            val_free(vp); *vp = val_copy(&rval);
        }
        return rval;
    }

    case ND_OPASSIGN: {
        Value lval = eval_node(n->left, locals, nlocals);
        Value rval = eval_node(n->right, locals, nlocals);
        double l = val_to_num(&lval), r = val_to_num(&rval);
        double res;
        switch(n->op.type) {
            case TK_PLUSEQ:    res = l + r; break;
            case TK_MINUSEQ:   res = l - r; break;
            case TK_STAREQ:    res = l * r; break;
            case TK_SLASHEQ:   res = r != 0 ? l / r : 0; break;
            case TK_PERCENTEQ: res = r != 0 ? fmod(l, r) : 0; break;
            case TK_CARETEQ:   res = pow(l, r); break;
            default: res = l; break;
        }
        val_free(&lval); val_free(&rval);
        Value result = val_num(res);
        /* Re-assign */
        Node fake;
        memset(&fake, 0, sizeof(fake));
        fake.type = ND_ASSIGN; fake.left = n->left;
        fake.right = NULL; /* we'll assign manually below */
        /* Reuse assign logic */
        Node *lhs = n->left;
        if (lhs->type == ND_IDENT) {
            int found = 0;
            for (int i = 0; i < nlocals; i++) {
                if (locals[i] && strcmp(locals[i]->name, lhs->strval) == 0) {
                    val_free(&locals[i]->val); locals[i]->val = val_copy(&result); found=1; break;
                }
            }
            if (!found) var_set(lhs->strval, val_copy(&result));
        } else if (lhs->type == ND_FIELD) {
            Value fidx = eval_node(lhs->left, locals, nlocals);
            int fi = (int)val_to_num(&fidx); val_free(&fidx);
            char buf[64]; char *s = val_to_str_buf(&result, buf, sizeof(buf));
            if (fi == 0) { strncpy(g_line, s, MAX_LINE-1); split_fields(g_line, g_fs); }
            else { while (g_nfields < fi) g_fields[g_nfields++] = xstrdup("");
                   free(g_fields[fi-1]); g_fields[fi-1] = xstrdup(s); rebuild_line(); }
        } else if (lhs->type == ND_ARRAY_REF) {
            Array *arr = array_get_or_create(lhs->strval);
            char key[MAX_LINE]; key[0] = '\0';
            for (int i = 0; i < lhs->nchildren; i++) {
                if (i > 0) strncat(key, g_subsep, sizeof(key)-strlen(key)-1);
                Value kv = eval_node(lhs->children[i], locals, nlocals);
                char buf[64];
                strncat(key, val_to_str_buf(&kv, buf, sizeof(buf)), sizeof(key)-strlen(key)-1);
                val_free(&kv);
            }
            Value *vp = array_ref(arr, key);
            val_free(vp); *vp = val_copy(&result);
        }
        return result;
    }

    case ND_INC: {
        Value cur = eval_node(n->left, locals, nlocals);
        double old = val_to_num(&cur);
        double newv = (n->op.type == TK_INC) ? old + 1 : old - 1;
        val_free(&cur);
        Value result = val_num(newv);
        /* assign back */
        Node *lhs = n->left;
        if (lhs->type == ND_IDENT) {
            int found = 0;
            for (int i = 0; i < nlocals; i++) {
                if (locals[i] && strcmp(locals[i]->name, lhs->strval) == 0) {
                    val_free(&locals[i]->val); locals[i]->val = val_copy(&result); found=1; break;
                }
            }
            if (!found) var_set(lhs->strval, val_copy(&result));
        } else if (lhs->type == ND_FIELD) {
            Value fidx = eval_node(lhs->left, locals, nlocals);
            int fi = (int)val_to_num(&fidx); val_free(&fidx);
            char buf[64]; char *s = val_to_str_buf(&result, buf, sizeof(buf));
            if (fi == 0) { strncpy(g_line, s, MAX_LINE-1); split_fields(g_line, g_fs); }
            else { while (g_nfields < fi) g_fields[g_nfields++] = xstrdup("");
                   free(g_fields[fi-1]); g_fields[fi-1] = xstrdup(s); rebuild_line(); }
        } else if (lhs->type == ND_ARRAY_REF) {
            Array *arr = array_get_or_create(lhs->strval);
            char key[MAX_LINE]; key[0] = '\0';
            for (int i = 0; i < lhs->nchildren; i++) {
                if (i > 0) strncat(key, g_subsep, sizeof(key)-strlen(key)-1);
                Value kv = eval_node(lhs->children[i], locals, nlocals);
                char buf[64];
                strncat(key, val_to_str_buf(&kv, buf, sizeof(buf)), sizeof(key)-strlen(key)-1);
                val_free(&kv);
            }
            Value *vp = array_ref(arr, key);
            val_free(vp); *vp = val_copy(&result);
        }
        /* post returns old, pre returns new */
        if (n->flags == 0) return val_num(old); /* post */
        return result;
    }

    case ND_BINOP: {
        /* Short circuit for && and || */
        if (n->op.type == TK_AND) {
            Value l = eval_node(n->left, locals, nlocals);
            if (!val_is_true(&l)) { val_free(&l); return val_num(0); }
            val_free(&l);
            Value r = eval_node(n->right, locals, nlocals);
            int rv = val_is_true(&r); val_free(&r);
            return val_num(rv);
        }
        if (n->op.type == TK_OR) {
            Value l = eval_node(n->left, locals, nlocals);
            if (val_is_true(&l)) { val_free(&l); return val_num(1); }
            val_free(&l);
            Value r = eval_node(n->right, locals, nlocals);
            int rv = val_is_true(&r); val_free(&r);
            return val_num(rv);
        }

        Value l = eval_node(n->left, locals, nlocals);
        Value r = eval_node(n->right, locals, nlocals);

        switch(n->op.type) {
        case TK_PLUS:    { double v = val_to_num(&l) + val_to_num(&r); val_free(&l); val_free(&r); return val_num(v); }
        case TK_MINUS:   { double v = val_to_num(&l) - val_to_num(&r); val_free(&l); val_free(&r); return val_num(v); }
        case TK_STAR:    { double v = val_to_num(&l) * val_to_num(&r); val_free(&l); val_free(&r); return val_num(v); }
        case TK_SLASH:   {
            double rv2 = val_to_num(&r);
            if (rv2 == 0) { val_free(&l); val_free(&r); fprintf(stderr, "division by zero\n"); return val_num(0); }
            double v = val_to_num(&l) / rv2; val_free(&l); val_free(&r); return val_num(v);
        }
        case TK_PERCENT: {
            double rv2 = val_to_num(&r);
            if (rv2 == 0) { val_free(&l); val_free(&r); return val_num(0); }
            double v = fmod(val_to_num(&l), rv2); val_free(&l); val_free(&r); return val_num(v);
        }
        case TK_CARET:   { double v = pow(val_to_num(&l), val_to_num(&r)); val_free(&l); val_free(&r); return val_num(v); }
        case TK_EQ: case TK_NEQ: case TK_LT: case TK_GT: case TK_LE: case TK_GE: {
            int cmp;
            /* String comparison if both are strings */
            if ((l.type == VAL_STR && r.type == VAL_STR)) {
                char buf1[64], buf2[64];
                cmp = strcmp(val_to_str_buf(&l, buf1, sizeof(buf1)),
                             val_to_str_buf(&r, buf2, sizeof(buf2)));
            } else {
                double dl = val_to_num(&l), dr = val_to_num(&r);
                cmp = dl < dr ? -1 : dl > dr ? 1 : 0;
            }
            int result;
            switch(n->op.type) {
                case TK_EQ:  result = cmp == 0; break;
                case TK_NEQ: result = cmp != 0; break;
                case TK_LT:  result = cmp < 0; break;
                case TK_GT:  result = cmp > 0; break;
                case TK_LE:  result = cmp <= 0; break;
                case TK_GE:  result = cmp >= 0; break;
                default:     result = 0;
            }
            val_free(&l); val_free(&r);
            return val_num(result);
        }
        default: val_free(&l); val_free(&r); return val_undef();
        }
    }

    case ND_UNOP: {
        Value v = eval_node(n->left, locals, nlocals);
        if (n->op.type == TK_MINUS) { double d = -val_to_num(&v); val_free(&v); return val_num(d); }
        if (n->op.type == TK_PLUS)  { double d = val_to_num(&v); val_free(&v); return val_num(d); }
        if (n->op.type == TK_BANG)  { int b = !val_is_true(&v); val_free(&v); return val_num(b); }
        val_free(&v); return val_undef();
    }

    case ND_TERNARY: {
        Value cond = eval_node(n->cond, locals, nlocals);
        int b = val_is_true(&cond); val_free(&cond);
        return b ? eval_node(n->left, locals, nlocals) : eval_node(n->right, locals, nlocals);
    }

    case ND_CONCAT: {
        Value l = eval_node(n->left, locals, nlocals);
        Value r = eval_node(n->right, locals, nlocals);
        char buf1[64], buf2[64];
        char *ls = val_to_str_buf(&l, buf1, sizeof(buf1));
        char *rs = val_to_str_buf(&r, buf2, sizeof(buf2));
        size_t len = strlen(ls) + strlen(rs) + 1;
        char *out = xmalloc(len);
        strcpy(out, ls); strcat(out, rs);
        val_free(&l); val_free(&r);
        Value v = val_str(out); free(out); return v;
    }

    case ND_MATCH_EXPR: {
        Value l = eval_node(n->left, locals, nlocals);
        Value r = eval_node(n->right, locals, nlocals);
        char buf[64];
        char *s = val_to_str_buf(&l, buf, sizeof(buf));
        char *pat;
        char patbuf[MAX_LINE];
        if (n->right->type == ND_REGEX || n->right->type == ND_STR) {
            pat = val_to_str_buf(&r, patbuf, sizeof(patbuf));
        } else {
            pat = val_to_str_buf(&r, patbuf, sizeof(patbuf));
        }
        regex_t re;
        int matched = 0;
        if (regcomp(&re, pat, REG_EXTENDED|REG_NOSUB) == 0) {
            matched = (regexec(&re, s, 0, NULL, 0) == 0);
            regfree(&re);
        }
        val_free(&l); val_free(&r);
        if (n->op.type == TK_NOTMATCH) matched = !matched;
        return val_num(matched);
    }

    case ND_CALL: {
        /* Try builtin first */
        Value bv = call_builtin(n->strval, n->children, n->nchildren, locals, nlocals);
        if (bv.type != VAL_UNDEF || bv.str != NULL) {
            /* Actually builtin returns undef for unknown, check return */
        }
        /* Find user function */
        Function *fn = NULL;
        for (int i = 0; i < g_nfunctions; i++) {
            if (strcmp(g_functions[i].name, n->strval) == 0) { fn = &g_functions[i]; break; }
        }
        if (!fn) {
            /* It was a builtin */
            return bv;
        }
        val_free(&bv);

        /* Set up locals */
        VarEntry *fn_locals[MAX_LOCALS] = {0};
        int fn_nlocals = fn->nparam;
        for (int i = 0; i < fn->nparam; i++) {
            fn_locals[i] = xmalloc(sizeof(VarEntry));
            fn_locals[i]->name = fn->params[i];
            fn_locals[i]->arr  = NULL;
            if (i < n->nchildren) {
                fn_locals[i]->val = eval_node(n->children[i], locals, nlocals);
            } else {
                fn_locals[i]->val = val_undef();
            }
            fn_locals[i]->next = NULL;
        }

        /* Execute function body */
        int saved_ret = g_return_flag;
        Value saved_rval = g_return_val;
        g_return_flag = 0;
        g_return_val = val_undef();

        eval_node(fn->body, fn_locals, fn_nlocals);

        Value ret = g_return_val;
        g_return_flag = saved_ret;
        g_return_val = saved_rval;

        /* Free locals */
        for (int i = 0; i < fn->nparam; i++) {
            val_free(&fn_locals[i]->val);
            free(fn_locals[i]);
        }
        return ret;
    }

    case ND_PRINT: case ND_PRINTF: {
        int is_printf = (n->flags & 4);
        FILE *outfp = stdout;
        /* Determine output target */
        if (n->flags == 1 || n->flags == 2 || n->flags == 3) {
            Value fv = eval_node(n->right, locals, nlocals);
            char buf[256];
            char *fname = val_to_str_buf(&fv, buf, sizeof(buf));
            if (n->flags == 3) {
                /* pipe */
                outfp = NULL;
                for (int i = 0; i < g_npipes; i++) {
                    if (g_pipes[i].write && strcmp(g_pipes[i].cmd, fname) == 0) {
                        outfp = g_pipes[i].fp; break;
                    }
                }
                if (!outfp && g_npipes < MAX_PIPES) {
                    outfp = popen(fname, "w");
                    g_pipes[g_npipes].cmd = xstrdup(fname);
                    g_pipes[g_npipes].fp  = outfp;
                    g_pipes[g_npipes].write = 1;
                    g_npipes++;
                }
            } else {
                const char *mode = (n->flags == 2) ? "a" : "w";
                outfp = fopen(fname, mode);
                if (!outfp) { perror(fname); val_free(&fv); return val_undef(); }
            }
            val_free(&fv);
        }

        if (is_printf) {
            if (n->nchildren > 0) {
                /* Use sprintf then print */
                Value sv = call_builtin("sprintf", n->children, n->nchildren, locals, nlocals);
                char buf2[64];
                fputs(val_to_str_buf(&sv, buf2, sizeof(buf2)), outfp ? outfp : stdout);
                val_free(&sv);
            }
        } else {
            if (n->nchildren == 0) {
                fprintf(outfp ? outfp : stdout, "%s%s", g_line, g_ors);
            } else {
                for (int i = 0; i < n->nchildren; i++) {
                    if (i > 0) fputs(g_ofs, outfp ? outfp : stdout);
                    Value v = eval_node(n->children[i], locals, nlocals);
                    char buf2[64];
                    fputs(val_to_str_buf(&v, buf2, sizeof(buf2)), outfp ? outfp : stdout);
                    val_free(&v);
                }
                fputs(g_ors, outfp ? outfp : stdout);
            }
        }
        if (outfp && outfp != stdout && n->flags != 3) fclose(outfp);
        return val_undef();
    }

    case ND_BLOCK: {
        Value last = val_undef();
        for (int i = 0; i < n->nchildren; i++) {
            val_free(&last);
            last = eval_node(n->children[i], locals, nlocals);
            if (g_exit_flag || g_next_flag || g_nextfile_flag ||
                g_break_flag || g_continue_flag || g_return_flag) break;
        }
        return last;
    }

    case ND_IF: {
        Value cond = eval_node(n->cond, locals, nlocals);
        int b = val_is_true(&cond); val_free(&cond);
        if (b) return eval_node(n->body, locals, nlocals);
        else if (n->elsebody) return eval_node(n->elsebody, locals, nlocals);
        return val_undef();
    }

    case ND_WHILE: {
        while (1) {
            Value cond = eval_node(n->cond, locals, nlocals);
            int b = val_is_true(&cond); val_free(&cond);
            if (!b) break;
            eval_node(n->body, locals, nlocals);
            if (g_break_flag) { g_break_flag = 0; break; }
            if (g_continue_flag) { g_continue_flag = 0; continue; }
            if (g_exit_flag || g_next_flag || g_nextfile_flag || g_return_flag) break;
        }
        return val_undef();
    }

    case ND_DO_WHILE: {
        do {
            eval_node(n->body, locals, nlocals);
            if (g_break_flag) { g_break_flag = 0; break; }
            if (g_continue_flag) { g_continue_flag = 0; }
            if (g_exit_flag || g_next_flag || g_nextfile_flag || g_return_flag) break;
            Value cond = eval_node(n->cond, locals, nlocals);
            int b = val_is_true(&cond); val_free(&cond);
            if (!b) break;
        } while (1);
        return val_undef();
    }

    case ND_FOR: {
        if (n->init) { Value v = eval_node(n->init, locals, nlocals); val_free(&v); }
        while (1) {
            if (n->cond) {
                Value cond = eval_node(n->cond, locals, nlocals);
                int b = val_is_true(&cond); val_free(&cond);
                if (!b) break;
            }
            eval_node(n->body, locals, nlocals);
            if (g_break_flag) { g_break_flag = 0; break; }
            if (g_continue_flag) g_continue_flag = 0;
            if (g_exit_flag || g_next_flag || g_nextfile_flag || g_return_flag) break;
            if (n->incr) { Value v = eval_node(n->incr, locals, nlocals); val_free(&v); }
        }
        return val_undef();
    }

    case ND_FOR_IN: {
        /* for (k in arr) */
        char *kname = n->init->strval;
        char *aname = n->cond->strval;
        Array *arr = array_find(aname);
        if (!arr) return val_undef();
        /* Collect all keys first */
        char **keys = NULL; int nkeys = 0;
        for (int i = 0; i < HASH_SIZE; i++) {
            ArrayEntry *e = arr->buckets[i];
            while (e) {
                keys = xrealloc(keys, sizeof(char*) * (nkeys+1));
                keys[nkeys++] = e->key;
                e = e->next;
            }
        }
        for (int i = 0; i < nkeys; i++) {
            /* assign key to var */
            int found = 0;
            for (int j = 0; j < nlocals; j++) {
                if (locals[j] && strcmp(locals[j]->name, kname) == 0) {
                    val_free(&locals[j]->val); locals[j]->val = val_str(keys[i]); found=1; break;
                }
            }
            if (!found) var_set(kname, val_str(keys[i]));
            eval_node(n->body, locals, nlocals);
            if (g_break_flag) { g_break_flag = 0; break; }
            if (g_continue_flag) g_continue_flag = 0;
            if (g_exit_flag || g_next_flag || g_nextfile_flag || g_return_flag) break;
        }
        free(keys);
        return val_undef();
    }

    case ND_BREAK:    g_break_flag = 1;    return val_undef();
    case ND_CONTINUE: g_continue_flag = 1; return val_undef();
    case ND_NEXT:     g_next_flag = 1;     return val_undef();
    case ND_NEXTFILE: g_nextfile_flag = 1; return val_undef();

    case ND_RETURN:
        g_return_flag = 1;
        g_return_val = n->left ? eval_node(n->left, locals, nlocals) : val_undef();
        return val_copy(&g_return_val);

    case ND_EXIT:
        g_exit_flag = 1;
        if (n->left) {
            Value ev = eval_node(n->left, locals, nlocals);
            g_exit_code = (int)val_to_num(&ev);
            val_free(&ev);
        }
        return val_undef();

    case ND_DELETE: {
        Node *target = n->left;
        if (target->type == ND_IDENT) {
            Array *arr = array_find(target->strval);
            if (arr) array_delete_all(arr);
        } else if (target->type == ND_ARRAY_REF) {
            Array *arr = array_find(target->strval);
            if (arr) {
                char key[MAX_LINE]; key[0] = '\0';
                for (int i = 0; i < target->nchildren; i++) {
                    if (i > 0) strncat(key, g_subsep, sizeof(key)-strlen(key)-1);
                    Value kv = eval_node(target->children[i], locals, nlocals);
                    char buf[64];
                    strncat(key, val_to_str_buf(&kv, buf, sizeof(buf)), sizeof(key)-strlen(key)-1);
                    val_free(&kv);
                }
                array_delete(arr, key);
            }
        }
        return val_undef();
    }

    case ND_GETLINE: {
        /* Simple: read next line from stdin */
        char buf[MAX_LINE];
        if (fgets(buf, sizeof(buf), stdin)) {
            size_t l = strlen(buf);
            if (l > 0 && buf[l-1] == '\n') buf[l-1] = '\0';
            strncpy(g_line, buf, MAX_LINE-1);
            split_fields(g_line, g_fs);
            g_nr++; g_fnr++;
            return val_num(1);
        }
        return val_num(0);
    }

    default:
        return val_undef();
    }
}

/* ============================================================
 * PATTERN MATCHING
 * ============================================================ */
static int match_pattern(Node *pat) {
    if (!pat) return 1; /* no pattern = match all */
    Value v = eval_node(pat, NULL, 0);
    if (pat->type == ND_REGEX) {
        regex_t re;
        int r = 0;
        if (regcomp(&re, pat->strval, REG_EXTENDED|REG_NOSUB) == 0) {
            r = (regexec(&re, g_line, 0, NULL, 0) == 0);
            regfree(&re);
        }
        val_free(&v);
        return r;
    }
    int b = val_is_true(&v);
    val_free(&v);
    return b;
}

/* ============================================================
 * PROCESS ONE LINE
 * ============================================================ */
static void process_line(void) {
    for (int i = 0; i < g_nrules; i++) {
        Rule *r = &g_rules[i];
        if (r->is_begin || r->is_end) continue;

        int fire = 0;
        if (r->pattern2) {
            /* Range pattern */
            if (!r->range_active) {
                if (match_pattern(r->pattern)) {
                    r->range_active = 1;
                    fire = 1;
                }
            } else {
                fire = 1;
                if (match_pattern(r->pattern2)) r->range_active = 0;
            }
        } else {
            fire = match_pattern(r->pattern);
        }

        if (fire) {
            if (r->action) {
                Value v = eval_node(r->action, NULL, 0);
                val_free(&v);
            } else {
                /* Default action: print $0 */
                printf("%s%s", g_line, g_ors);
            }
            if (g_exit_flag || g_next_flag) break;
            if (g_nextfile_flag) break;
        }
    }
    g_next_flag = 0;
}

/* ============================================================
 * RUN BEGIN/END
 * ============================================================ */
static void run_begin(void) {
    for (int i = 0; i < g_nrules; i++) {
        if (!g_rules[i].is_begin) continue;
        if (g_rules[i].action) {
            Value v = eval_node(g_rules[i].action, NULL, 0);
            val_free(&v);
        }
        if (g_exit_flag) break;
    }
}

static void run_end(void) {
    g_exit_flag = 0;
    for (int i = 0; i < g_nrules; i++) {
        if (!g_rules[i].is_end) continue;
        if (g_rules[i].action) {
            Value v = eval_node(g_rules[i].action, NULL, 0);
            val_free(&v);
        }
    }
}

/* ============================================================
 * PROCESS FILE
 * ============================================================ */
static void process_file(FILE *fp, const char *fname) {
    g_filename = (char*)fname;
    g_fnr = 0;
    char buf[MAX_LINE];

    while (fgets(buf, sizeof(buf), fp)) {
        if (g_nextfile_flag) { g_nextfile_flag = 0; break; }
        size_t l = strlen(buf);
        /* Strip record separator */
        if (l > 0 && buf[l-1] == '\n') { buf[--l] = '\0'; }
        if (g_rs[0] && g_rs[1] == '\0' && l > 0 && buf[l-1] == g_rs[0]) buf[--l] = '\0';

        strncpy(g_line, buf, MAX_LINE-1);
        split_fields(g_line, g_fs);
        g_nr++; g_fnr++;
        var_set("NR",  val_num(g_nr));
        var_set("FNR", val_num(g_fnr));
        var_set("NF",  val_num(g_nfields));

        process_line();
        if (g_exit_flag) return;
    }
}

/* ============================================================
 * USAGE / VERSION
 * ============================================================ */
static void usage(void) {
    fprintf(stderr,
        "Usage: awk [-F fs] [-v var=val] 'program' [file ...]\n"
        "       awk [-F fs] [-v var=val] -f progfile [file ...]\n"
        "  -F fs      Set field separator\n"
        "  -v var=val Set variable\n"
        "  -f file    Read program from file\n"
        "  --version  Print version\n"
    );
    exit(1);
}

/* ============================================================
 * MAIN
 * ============================================================ */
int main(int argc, char **argv) {
    g_argv0 = argv[0];
    /* Init defaults */
    strcpy(g_fs,    " ");
    strcpy(g_ofs,   " ");
    strcpy(g_ors,   "\n");
    strcpy(g_rs,    "\n");
    strcpy(g_subsep, "\034");
    srand(1);

    char *program = NULL;
    int   argi = 1;

    /* Parse options */
    while (argi < argc && argv[argi][0] == '-' && argv[argi][1] != '\0') {
        if (strcmp(argv[argi], "--version") == 0) {
            printf("%s\n", VERSION); return 0;
        }
        if (strcmp(argv[argi], "--") == 0) { argi++; break; }
        char opt = argv[argi][1];
        char *optarg2 = argv[argi][2] ? &argv[argi][2] : (argi+1 < argc ? argv[++argi] : NULL);
        argi++;
        if (opt == 'F') {
            if (!optarg2) usage();
            /* Handle escape sequences in FS */
            if (strcmp(optarg2, "\\t") == 0) strcpy(g_fs, "\t");
            else strncpy(g_fs, optarg2, sizeof(g_fs)-1);
        } else if (opt == 'v') {
            if (!optarg2) usage();
            char *eq = strchr(optarg2, '=');
            if (!eq) usage();
            *eq = '\0';
            var_set(optarg2, val_str(eq+1));
            *eq = '=';
        } else if (opt == 'f') {
            if (!optarg2) usage();
            FILE *fp = fopen(optarg2, "r");
            if (!fp) { perror(optarg2); return 1; }
            fseek(fp, 0, SEEK_END); long sz = ftell(fp); fseek(fp, 0, SEEK_SET);
            program = xmalloc(sz+1);
            fread(program, 1, sz, fp); program[sz] = '\0';
            fclose(fp);
        } else {
            usage();
        }
    }

    if (!program) {
        if (argi >= argc) usage();
        program = argv[argi++];
    }

    /* Lex & parse */
    lex_init(program);
    lex_all();
    parse_program();

    /* Set OFS in case user set it via -v before parsing */
    /* (already done via var_set above) */

    /* Run BEGIN */
    run_begin();
    if (g_exit_flag) {
        run_end();
        /* Close pipes */
        for (int i = 0; i < g_npipes; i++) if (g_pipes[i].write) pclose(g_pipes[i].fp);
        return g_exit_code;
    }

    /* Check if there are any non-BEGIN/END rules */
    int has_rules = 0;
    for (int i = 0; i < g_nrules; i++) {
        if (!g_rules[i].is_begin && !g_rules[i].is_end) { has_rules = 1; break; }
    }

    if (has_rules) {
        if (argi >= argc) {
            /* Read from stdin */
            process_file(stdin, "-");
        } else {
            for (; argi < argc && !g_exit_flag; argi++) {
                if (strcmp(argv[argi], "-") == 0) {
                    process_file(stdin, "-");
                } else {
                    FILE *fp = fopen(argv[argi], "r");
                    if (!fp) { perror(argv[argi]); continue; }
                    process_file(fp, argv[argi]);
                    fclose(fp);
                }
            }
        }
    }

    run_end();
    /* Close pipes */
    for (int i = 0; i < g_npipes; i++) if (g_pipes[i].write) pclose(g_pipes[i].fp);

    return g_exit_code;
}
