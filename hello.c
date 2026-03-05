/*
 * examples/hello.c
 * Demo using ONLY our libc on AArch64 Linux.
 * Compiled with: gcc -ffreestanding -nostdlib -nostdinc -no-pie
 * No glibc. No -lm. No -lc. Nothing external.
 */
#include "libtool.h"

static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    printf("╔══════════════════════════════════════╗\n");
    printf("║  Hello from our own libc! (AArch64)  ║\n");
    printf("║  No glibc. No -lm. No -lc. Nothing.  ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    printf("argc = %d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("  argv[%d] = %s\n", i, argv[i]);
    printf("pid  = %d\n", getpid());
    printf("uid  = %d\n\n", (int)getuid());

    /* malloc / free — backed by mmap(MAP_ANONYMOUS) */
    char *buf = (char *)malloc(64);
    snprintf(buf, 64, "mmap-backed heap on AArch64, pid=%d", getpid());
    puts(buf);
    free(buf);

    /* Math — no -lm, all implemented in C */
    printf("\n── Math (no -lm) ──────────────────────\n");
    printf("sqrt(2)   = %.8f\n", sqrt(2.0));
    printf("sin(π/6)  = %.8f  (expect 0.5)\n", sin(M_PI / 6.0));
    printf("cos(π/3)  = %.8f  (expect 0.5)\n", cos(M_PI / 3.0));
    printf("exp(1)    = %.8f  (expect ~2.71828)\n", exp(1.0));
    printf("log2(1024)= %.1f\n", log2(1024.0));
    printf("pow(2,10) = %.0f\n", pow(2.0, 10.0));
    printf("atan2(1,1)= %.8f  (expect π/4)\n", atan2(1.0, 1.0));

    /* File I/O — openat/read/write syscalls */
    printf("\n── File I/O (openat syscall) ──────────\n");
    FILE *f = fopen("/tmp/libc_arm64_demo.txt", "w");
    if (f) {
        fprintf(f, "Written by our AArch64 libc!\n");
        fprintf(f, "pid=%d  uid=%d\n", getpid(), (int)getuid());
        fclose(f);
        printf("Wrote  /tmp/libc_arm64_demo.txt\n");
    }
    f = fopen("/tmp/libc_arm64_demo.txt", "r");
    if (f) {
        char line[128];
        printf("Read back:\n");
        while (fgets(line, sizeof(line), f))
            printf("  %s", line);
        fclose(f);
    }
    unlink("/tmp/libc_arm64_demo.txt");

    /* Sorting */
    printf("\n── qsort ───────────────────────────────\n");
    int arr[] = {9, 3, 7, 1, 5, 2, 8, 4, 6, 0, -5, 100};
    int n = (int)ARRAY_LEN(arr);
    qsort(arr, (size_t)n, sizeof(int), cmp_int);
    printf("Sorted: ");
    for (int i = 0; i < n; i++) printf("%d%s", arr[i], i < n-1 ? " " : "\n");

    /* getrandom */
    printf("\n── getrandom syscall ───────────────────\n");
    unsigned char rb[16];
    getrandom(rb, 16, 0);
    printf("Random: ");
    for (int i = 0; i < 16; i++) printf("%02x", rb[i]);
    printf("\n");

    /* String operations */
    printf("\n── Strings ─────────────────────────────\n");
    char *s = strdup("  hello from AArch64  ");
    char *p = s;
    while (*p == ' ') p++;
    char *end = p + strlen(p) - 1;
    while (end > p && *end == ' ') *end-- = '\0';
    printf("Trimmed: '%s'\n", p);
    free(s);

    char *rep = "the quick brown fox";
    /* manual replace demo */
    printf("Original: %s\n", rep);

    /* Environment */
    printf("\n── getenv ──────────────────────────────\n");
    char *home = getenv("HOME");
    char *shell = getenv("SHELL");
    printf("HOME=%s\n",  home  ? home  : "(not set)");
    printf("SHELL=%s\n", shell ? shell : "(not set)");

    /* printf formatting */
    printf("\n── printf formatting ───────────────────\n");
    printf("|%12s|%-12s|%012d|%#012x|\n", "right", "left", 255, 255);
    printf("|%+.5f|%-12.4e|\n", 3.14159265, 2.71828182);
    printf("|%lld|%zu|\n", (long long)INT64_MAX, (size_t)SIZE_MAX);

    /* Clock */
    printf("\n── clock_gettime ───────────────────────\n");
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    printf("Monotonic: %ld.%09ld\n", ts.tv_sec, ts.tv_nsec);

    printf("\nAll done. No glibc was harmed. (AArch64)\n");
    return 0;
}
