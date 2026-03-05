/*
 * libc/src/start.c
 * Runtime startup, errno, process control, file I/O syscall wrappers.
 * AArch64 Linux — no external dependencies.
 *
 * Key AArch64 difference from x86-64:
 *   - No open() syscall; must use openat(AT_FDCWD, ...) instead
 *   - No dup2();  must use dup3()
 *   - SYS_exit_group = 94 (not 231)
 *   - SYS_getpid  = 172, SYS_getuid = 174, SYS_getgid = 176
 */

#include "../include/libtool.h"
#include "../include/syscall.h"

/* ── Global state ────────────────────────────────────────────────── */
int    errno   = 0;
char **environ = NULL;

/* ── atexit ──────────────────────────────────────────────────────── */
#define ATEXIT_MAX 32
static void (*__atexit_fns[ATEXIT_MAX])(void);
static int   __atexit_count = 0;

int atexit(void (*fn)(void)) {
    if (__atexit_count >= ATEXIT_MAX) return -1;
    __atexit_fns[__atexit_count++] = fn;
    return 0;
}

/* ── C startup ───────────────────────────────────────────────────── */
extern int main(int argc, char **argv, char **envp);

void __libc_start_main(int argc, char **argv, char **envp) {
    environ = envp;
    extern void __stdio_init(void);
    __stdio_init();
    int ret = main(argc, argv, envp);
    exit(ret);
}

/* ── errno helper ────────────────────────────────────────────────── */
static inline long __check_errno(long r) {
    if (UNLIKELY(r < 0 && r > -4096)) {
        errno = (int)-r;
        return -1;
    }
    return r;
}

/* ── Process exit ────────────────────────────────────────────────── */
void _exit(int status) {
    syscall1(SYS_exit_group, status);
    __builtin_unreachable();
}

void exit(int status) {
    for (int i = __atexit_count - 1; i >= 0; i--)
        __atexit_fns[i]();
    extern void __stdio_flush_all(void);
    __stdio_flush_all();
    _exit(status);
}

void abort(void) {
    syscall2(SYS_kill, getpid(), 6 /* SIGABRT */);
    _exit(134);
    __builtin_unreachable();
}

/* ── Process info ────────────────────────────────────────────────── */
pid_t  getpid(void)  { return (pid_t)syscall0(SYS_getpid);  }
pid_t  getppid(void) { return (pid_t)syscall0(SYS_getppid); }
uid_t  getuid(void)  { return (uid_t)syscall0(SYS_getuid);  }
gid_t  getgid(void)  { return (gid_t)syscall0(SYS_getgid);  }

/* ── File I/O ────────────────────────────────────────────────────── */

/*
 * AArch64 has no open() syscall — only openat().
 * We implement open() as openat(AT_FDCWD, path, flags, mode).
 */
int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    return (int)__check_errno(
        syscall4(SYS_openat, AT_FDCWD, path, flags, (long)mode));
}

ssize_t read(int fd, void *buf, size_t n) {
    return (ssize_t)__check_errno(syscall3(SYS_read, fd, buf, (long)n));
}

ssize_t write(int fd, const void *buf, size_t n) {
    return (ssize_t)__check_errno(syscall3(SYS_write, fd, buf, (long)n));
}

int close(int fd) {
    return (int)__check_errno(syscall1(SYS_close, fd));
}

off_t lseek(int fd, off_t offset, int whence) {
    return (off_t)__check_errno(syscall3(SYS_lseek, fd, (long)offset, whence));
}

/*
 * AArch64 has no unlink() — use unlinkat(AT_FDCWD, path, 0).
 */
int unlink(const char *path) {
    return (int)__check_errno(syscall3(SYS_unlinkat, AT_FDCWD, path, 0));
}

/*
 * AArch64 has no rename() — use renameat2(AT_FDCWD, old, AT_FDCWD, new, 0).
 */
int rename(const char *old, const char *newp) {
    return (int)__check_errno(
        syscall5(SYS_renameat2, AT_FDCWD, old, AT_FDCWD, newp, 0));
}

/*
 * AArch64 has no mkdir() — use mkdirat(AT_FDCWD, path, mode).
 */
int mkdir(const char *path, mode_t mode) {
    return (int)__check_errno(syscall3(SYS_mkdirat, AT_FDCWD, path, (long)mode));
}

/*
 * AArch64 has no rmdir() — use unlinkat(AT_FDCWD, path, AT_REMOVEDIR).
 */
int rmdir(const char *path) {
    return (int)__check_errno(syscall3(SYS_unlinkat, AT_FDCWD, path, 0x200));
}

char *getcwd(char *buf, size_t size) {
    long r = __check_errno(syscall2(SYS_getcwd, buf, (long)size));
    return r < 0 ? NULL : buf;
}

int chdir(const char *path) {
    return (int)__check_errno(syscall1(SYS_chdir, path));
}

int dup(int fd) {
    return (int)__check_errno(syscall1(SYS_dup, fd));
}

/*
 * AArch64 has no dup2() — use dup3(oldfd, newfd, 0).
 */
int dup2(int oldfd, int newfd) {
    return (int)__check_errno(syscall3(SYS_dup3, oldfd, newfd, 0));
}

/* ── mmap ────────────────────────────────────────────────────────── */
void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off) {
    long r = syscall6(SYS_mmap, addr, (long)len, prot, flags, fd, (long)off);
    if (r < 0 && r > -4096) { errno = (int)-r; return MAP_FAILED; }
    return (void *)r;
}

int munmap(void *addr, size_t len) {
    return (int)__check_errno(syscall2(SYS_munmap, addr, (long)len));
}

int mprotect(void *addr, size_t len, int prot) {
    return (int)__check_errno(syscall3(SYS_mprotect, addr, (long)len, prot));
}

/* ── Assert ──────────────────────────────────────────────────────── */
void __assert_fail(const char *expr, const char *file, int line) {
    write(2, "Assertion failed: ", 18);
    write(2, expr, strlen(expr));
    write(2, "  [", 3);
    write(2, file, strlen(file));
    write(2, ":", 1);
    char tmp[16]; int len=0, n=line;
    if (!n) tmp[len++]='0';
    while (n>0) { tmp[len++]='0'+n%10; n/=10; }
    for (int i=0;i<len/2;i++) SWAP(char,tmp[i],tmp[len-1-i]);
    write(2, tmp, (size_t)len);
    write(2, "]\n", 2);
    abort();
}

/* ── Time ────────────────────────────────────────────────────────── */
int clock_gettime(int clk_id, struct timespec *ts) {
    return (int)__check_errno(syscall2(SYS_clock_gettime, clk_id, ts));
}

int gettimeofday(struct timeval *tv, void *tz) {
    return (int)__check_errno(syscall2(SYS_gettimeofday, tv, tz));
}

time_t time(time_t *t) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (t) *t = (time_t)tv.tv_sec;
    return (time_t)tv.tv_sec;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    return (int)__check_errno(syscall2(SYS_nanosleep, req, rem));
}

/* ── Random ──────────────────────────────────────────────────────── */
ssize_t getrandom(void *buf, size_t n, unsigned flags) {
    return (ssize_t)__check_errno(syscall3(SYS_getrandom, buf, (long)n, (long)flags));
}

static unsigned long __rand_state = 1;
void srand(unsigned seed) { __rand_state = seed; }
int  rand(void) {
    __rand_state = __rand_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((__rand_state >> 33) & 0x7fffffff);
}

/* ── Environment ─────────────────────────────────────────────────── */
char *getenv(const char *name) {
    if (!environ || !name) return NULL;
    size_t nlen = strlen(name);
    for (char **e = environ; *e; e++)
        if (strncmp(*e, name, nlen) == 0 && (*e)[nlen] == '=')
            return *e + nlen + 1;
    return NULL;
}

/* ── strerror / perror ───────────────────────────────────────────── */
const char *strerror(int e) {
    switch (e) {
    case EPERM:  return "Operation not permitted";
    case ENOENT: return "No such file or directory";
    case EINTR:  return "Interrupted system call";
    case EIO:    return "I/O error";
    case EBADF:  return "Bad file descriptor";
    case EAGAIN: return "Resource temporarily unavailable";
    case ENOMEM: return "Out of memory";
    case EACCES: return "Permission denied";
    case EFAULT: return "Bad address";
    case EEXIST: return "File exists";
    case EINVAL: return "Invalid argument";
    case ERANGE: return "Result too large";
    default:     return "Unknown error";
    }
}

void perror(const char *s) {
    if (s && *s) { write(2, s, strlen(s)); write(2, ": ", 2); }
    const char *m = strerror(errno);
    write(2, m, strlen(m));
    write(2, "\n", 1);
}
