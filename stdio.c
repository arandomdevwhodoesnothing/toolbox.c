/*
 * libc/src/stdio.c
 * Buffered FILE I/O built on raw read()/write() syscalls.
 * No dependency on any external stdio.
 */

#include "../include/libtool.h"
#include "../include/syscall.h"

/* ── FILE struct ─────────────────────────────────────────────────────── */
#define BUFSZ       4096
#define FILE_READ   0x01
#define FILE_WRITE  0x02
#define FILE_EOF    0x04
#define FILE_ERR    0x08
#define FILE_STATIC 0x10   /* don't free this FILE* */

struct FILE {
    int    fd;
    int    flags;
    char  *buf;
    size_t buf_pos;
    size_t buf_len;
    size_t buf_cap;
};

/* ── Static stdin/stdout/stderr ──────────────────────────────────────── */
static char __stdin_buf[BUFSZ];
static char __stdout_buf[BUFSZ];
static char __stderr_buf[64];   /* stderr: small, line-buffered */

static FILE __stdin_file  = { 0, FILE_READ  | FILE_STATIC, __stdin_buf,  0, 0, BUFSZ };
static FILE __stdout_file = { 1, FILE_WRITE | FILE_STATIC, __stdout_buf, 0, 0, BUFSZ };
static FILE __stderr_file = { 2, FILE_WRITE | FILE_STATIC, __stderr_buf, 0, 0, 64   };

FILE *stdin  = &__stdin_file;
FILE *stdout = &__stdout_file;
FILE *stderr = &__stderr_file;

/* Track all open FILEs for flush-on-exit */
#define MAX_FILES 64
static FILE *__open_files[MAX_FILES];
static int   __open_file_count = 0;

void __stdio_init(void) {
    __open_files[__open_file_count++] = stdout;
    __open_files[__open_file_count++] = stderr;
}

/* ── Internal flush ──────────────────────────────────────────────────── */
static int file_flush(FILE *f) {
    if (!(f->flags & FILE_WRITE) || f->buf_pos == 0) return 0;
    size_t written = 0;
    while (written < f->buf_pos) {
        ssize_t r = write(f->fd, f->buf + written, f->buf_pos - written);
        if (r <= 0) { f->flags |= FILE_ERR; return -1; }
        written += (size_t)r;
    }
    f->buf_pos = 0;
    return 0;
}

void __stdio_flush_all(void) {
    for (int i = 0; i < __open_file_count; i++)
        if (__open_files[i]) file_flush(__open_files[i]);
}

/* ── fopen / fclose ──────────────────────────────────────────────────── */
FILE *fopen(const char *path, const char *mode) {
    int flags = 0, fmode = 0;
    if (mode[0] == 'r') { flags = O_RDONLY; fmode = FILE_READ; }
    else if (mode[0] == 'w') { flags = O_WRONLY|O_CREAT|O_TRUNC; fmode = FILE_WRITE; }
    else if (mode[0] == 'a') { flags = O_WRONLY|O_CREAT|O_APPEND; fmode = FILE_WRITE; }
    if (mode[1] == '+' || (mode[1] && mode[2] == '+'))
        flags = (flags & ~(O_RDONLY|O_WRONLY)) | O_RDWR, fmode = FILE_READ|FILE_WRITE;

    int fd = open(path, flags, (mode_t)0644);
    if (fd < 0) return NULL;

    FILE *f = (FILE *)malloc(sizeof(FILE));
    if (!f) { close(fd); return NULL; }
    f->buf = (char *)malloc(BUFSZ);
    if (!f->buf) { free(f); close(fd); return NULL; }
    f->fd      = fd;
    f->flags   = fmode;
    f->buf_pos = 0;
    f->buf_len = 0;
    f->buf_cap = BUFSZ;

    if (__open_file_count < MAX_FILES)
        __open_files[__open_file_count++] = f;

    return f;
}

int fclose(FILE *f) {
    if (!f) return -1;
    file_flush(f);
    int r = close(f->fd);
    /* Remove from open list */
    for (int i = 0; i < __open_file_count; i++) {
        if (__open_files[i] == f) {
            __open_files[i] = __open_files[--__open_file_count];
            break;
        }
    }
    if (!(f->flags & FILE_STATIC)) {
        free(f->buf);
        free(f);
    }
    return r;
}

int fflush(FILE *f) {
    if (!f) { __stdio_flush_all(); return 0; }
    return file_flush(f);
}

int feof(FILE *f)   { return !!(f->flags & FILE_EOF); }
int ferror(FILE *f) { return !!(f->flags & FILE_ERR); }

/* ── fread / fwrite ──────────────────────────────────────────────────── */
size_t fread(void *buf, size_t size, size_t nmemb, FILE *f) {
    size_t total = size * nmemb;
    size_t got   = 0;
    char  *out   = (char *)buf;

    while (got < total) {
        /* Consume buffered data first */
        if (f->buf_pos < f->buf_len) {
            size_t avail = f->buf_len - f->buf_pos;
            size_t copy  = avail < (total-got) ? avail : (total-got);
            memcpy(out + got, f->buf + f->buf_pos, copy);
            f->buf_pos += copy;
            got        += copy;
            continue;
        }
        /* Refill buffer */
        ssize_t r = read(f->fd, f->buf, f->buf_cap);
        if (r <= 0) {
            if (r == 0) f->flags |= FILE_EOF;
            else        f->flags |= FILE_ERR;
            break;
        }
        f->buf_pos = 0;
        f->buf_len = (size_t)r;
    }
    return (size > 0) ? got / size : 0;
}

size_t fwrite(const void *buf, size_t size, size_t nmemb, FILE *f) {
    size_t total   = size * nmemb;
    const char *in = (const char *)buf;
    size_t written = 0;

    while (written < total) {
        size_t avail = f->buf_cap - f->buf_pos;
        if (avail == 0) {
            if (file_flush(f) < 0) break;
            avail = f->buf_cap;
        }
        size_t copy = (total - written) < avail ? (total - written) : avail;
        memcpy(f->buf + f->buf_pos, in + written, copy);
        f->buf_pos += copy;
        written    += copy;

        /* Line-buffer stderr: flush on newline */
        if (f == stderr) {
            for (size_t i = 0; i < copy; i++) {
                if ((in + written - copy)[i] == '\n') { file_flush(f); break; }
            }
        }
    }
    return (size > 0) ? written / size : 0;
}

/* ── fgetc / fputc ───────────────────────────────────────────────────── */
int fgetc(FILE *f) {
    if (f->buf_pos >= f->buf_len) {
        ssize_t r = read(f->fd, f->buf, f->buf_cap);
        if (r <= 0) { if (r==0) f->flags|=FILE_EOF; else f->flags|=FILE_ERR; return EOF; }
        f->buf_pos = 0;
        f->buf_len = (size_t)r;
    }
    return (unsigned char)f->buf[f->buf_pos++];
}

int fputc(int c, FILE *f) {
    char ch = (char)c;
    if (fwrite(&ch, 1, 1, f) != 1) return EOF;
    if (f == stderr || ch == '\n') file_flush(f);
    return (unsigned char)ch;
}

char *fgets(char *buf, int n, FILE *f) {
    if (n <= 0) return NULL;
    int i = 0;
    for (; i < n-1; i++) {
        int c = fgetc(f);
        if (c == EOF) { if (i==0) return NULL; break; }
        buf[i] = (char)c;
        if (c == '\n') { i++; break; }
    }
    buf[i] = '\0';
    return buf;
}

int fputs(const char *s, FILE *f) {
    size_t n = strlen(s);
    return fwrite(s, 1, n, f) == n ? (int)n : EOF;
}

/* ── Console shortcuts ───────────────────────────────────────────────── */
int puts(const char *s) {
    if (fputs(s, stdout) == EOF) return EOF;
    return fputc('\n', stdout);
}

int putchar(int c)  { return fputc(c, stdout); }
int getchar(void)   { return fgetc(stdin); }

/* ── fseek / ftell ───────────────────────────────────────────────────── */
int fseek(FILE *f, long offset, int whence) {
    file_flush(f);
    f->buf_pos = f->buf_len = 0;
    f->flags &= ~FILE_EOF;
    return lseek(f->fd, (off_t)offset, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f) {
    off_t pos = lseek(f->fd, 0, SEEK_CUR);
    if (pos < 0) return -1;
    return (long)(pos - (off_t)(f->buf_len - f->buf_pos));
}
