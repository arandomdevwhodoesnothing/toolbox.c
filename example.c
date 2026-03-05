/*
 * example.c — libtool demonstration
 *
 * Compile:
 *   gcc -ffreestanding -nostdlib -nostdinc -no-pie \
 *       -Iinclude example.c crt0.o libtool.a -o example
 *
 * Run:
 *   ./example
 */
#include "libtool.h"

/* ── helpers ─────────────────────────────────────────────────────── */
static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

/* ── 1. Hello world ──────────────────────────────────────────────── */
static void example_hello(void) {
    printf("── 1. Hello World ──────────────────────\n");
    printf("Hello from libtool on AArch64!\n");
    printf("pid=%d  uid=%d\n\n", getpid(), (int)getuid());
}

/* ── 2. Strings ──────────────────────────────────────────────────── */
static void example_strings(void) {
    printf("── 2. Strings ──────────────────────────\n");

    char *s = strdup("  Hello, World!  ");
    printf("original : '%s'\n", s);
    printf("strlen   : %zu\n", strlen(s));
    printf("upper[7] : %c\n", toupper((unsigned char)s[7]));

    char *p = s;
    while (*p == ' ') p++;
    char *end = p + strlen(p) - 1;
    while (end > p && *end == ' ') *end-- = '\0';
    printf("trimmed  : '%s'\n", p);
    free(s);

    char csv[] = "alpha,beta,gamma,delta,epsilon";
    char *save, *tok;
    printf("split(','):  ");
    tok = strtok_r(csv, ",", &save);
    while (tok) {
        printf("[%s] ", tok);
        tok = strtok_r(NULL, ",", &save);
    }
    printf("\n");

    char buf[64];
    snprintf(buf, sizeof(buf), "%-10s %05d %#010x", "libtool", 42, 0xdeadbeef);
    printf("snprintf : '%s'\n", buf);

    printf("strcmp   : %d\n", strcmp("apple", "banana"));
    printf("strcaseq : %d\n", strcasecmp("Hello", "HELLO"));
    printf("\n");
}

/* ── 3. Memory / heap ────────────────────────────────────────────── */
static void example_memory(void) {
    printf("── 3. Memory ───────────────────────────\n");

    int *arr = (int *)malloc(10 * sizeof(int));
for (int i = 0; i < 10; i++) arr[i] = i * i;
    printf("squares  : ");
for (int i = 0; i < 10; i++) printf("%d ", arr[i]);
    printf("\n");
    free(arr);

    char *zeroed = (char *)calloc(8, sizeof(char));
    int all_zero = 1;
for (int i = 0; i < 8; i++) if (zeroed[i]) all_zero = 0;
    printf("calloc   : all zero = %s\n", all_zero ? "yes" : "no");
    free(zeroed);

    char *buf = (char *)malloc(8);
    strlcpy(buf, "hello", 8);
    buf = (char *)realloc(buf, 64);
    strlcat(buf, " world", 64);
    printf("realloc  : '%s'\n", buf);
    free(buf);

    void *ptrs[128];
for (int i = 0; i < 128; i++) ptrs[i] = malloc((size_t)(i + 1) * 8);
for (int i = 0; i < 128; i++) free(ptrs[i]);
    printf("128 alloc/free cycles: ok\n\n");
}

/* ── 4. Math (no -lm) ────────────────────────────────────────────── */
static void example_math(void) {
    printf("── 4. Math (no -lm) ────────────────────\n");
    printf("sqrt(2)      = %.10f\n", sqrt(2.0));
    printf("sqrt(144)    = %.1f\n",  sqrt(144.0));
    printf("pow(2, 32)   = %.0f\n",  pow(2.0, 32.0));
    printf("exp(1)       = %.10f\n", exp(1.0));
    printf("log(e)       = %.10f\n", log(M_E));
    printf("log2(65536)  = %.1f\n",  log2(65536.0));
    printf("log10(1000)  = %.1f\n",  log10(1000.0));
    printf("sin(π/2)     = %.10f\n", sin(M_PI / 2.0));
    printf("cos(0)       = %.10f\n", cos(0.0));
    printf("tan(π/4)     = %.10f\n", tan(M_PI / 4.0));
    printf("atan2(1,1)   = %.10f  (expect π/4=%.10f)\n",
           atan2(1.0, 1.0), M_PI / 4.0);
    printf("floor(3.9)   = %.1f\n",  floor(3.9));
    printf("ceil(3.1)    = %.1f\n",  ceil(3.1));
    printf("round(2.5)   = %.1f\n",  round(2.5));
    printf("fabs(-7.3)   = %.1f\n",  fabs(-7.3));
    printf("fmod(10,3)   = %.1f\n",  fmod(10.0, 3.0));
    printf("\n");
}

/* ── 5. Sorting ──────────────────────────────────────────────────── */
static void example_sort(void) {
    printf("── 5. Sorting ──────────────────────────\n");

    int nums[] = { 42, -7, 0, 100, 3, 99, -50, 1, 17, 8 };
    int n = (int)ARRAY_LEN(nums);
    qsort(nums, (size_t)n, sizeof(int), cmp_int);
    printf("int qsort: ");
for (int i = 0; i < n; i++) printf("%d%s", nums[i], i < n-1 ? " " : "\n");

    const char *words[] = { "banana", "apple", "cherry", "date", "avocado" };
    int wn = (int)ARRAY_LEN(words);
    qsort((void *)words, (size_t)wn, sizeof(char *), cmp_str);
    printf("str qsort: ");
for (int i = 0; i < wn; i++) printf("%s%s", words[i], i < wn-1 ? " " : "\n");

    int key = 17;
    int *found = (int *)bsearch(&key, nums, (size_t)n, sizeof(int), cmp_int);
    printf("bsearch(17):  %s\n", found ? "found" : "not found");
    key = 999;
    found = (int *)bsearch(&key, nums, (size_t)n, sizeof(int), cmp_int);
    printf("bsearch(999): %s\n\n", found ? "found" : "not found");
}

/* ── 6. File I/O ─────────────────────────────────────────────────── */
static void example_files(void) {
    printf("── 6. File I/O ─────────────────────────\n");

    const char *path = "/tmp/libtool_example.txt";

    FILE *f = fopen(path, "w");
    if (!f) { printf("fopen write failed\n"); return; }
    fprintf(f, "Line 1: libtool on AArch64\n");
    fprintf(f, "Line 2: pid=%d\n", getpid());
    fprintf(f, "Line 3: sqrt(2)=%.6f\n", sqrt(2.0));
    fclose(f);
    printf("Wrote: %s\n", path);

    f = fopen(path, "r");
    if (!f) { printf("fopen read failed\n"); return; }
    char buf[128];
    int lineno = 0;
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
        printf("  [%d] %s\n", ++lineno, buf);
    }
    fclose(f);

    f = fopen(path, "r");
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    printf("File size: %ld bytes\n", size);
    fclose(f);

    unlink(path);
    printf("Deleted: %s\n\n", path);
}

/* ── 7. Number parsing ───────────────────────────────────────────── */
static void example_numbers(void) {
    printf("── 7. Number Parsing ───────────────────\n");
    printf("atoi(\"  -42 \")    = %d\n",  atoi("  -42 "));
    printf("atof(\"3.14159\")   = %.5f\n", atof("3.14159"));
    printf("strtol(\"0xFF\",16) = %ld\n",  strtol("0xFF", NULL, 16));
    printf("strtol(\"0777\",0)  = %ld\n",  strtol("0777", NULL, 0));
    printf("strtoul max       = %lu\n",   strtoul("4294967295", NULL, 10));

    char *endptr;
    double d = strtod("1.23e4 rest", &endptr);
    printf("strtod            = %.1f, rest='%s'\n", d, endptr);
    printf("\n");
}

/* ── 8. Random ───────────────────────────────────────────────────── */
static void example_random(void) {
    printf("── 8. Random ───────────────────────────\n");

    unsigned char buf[16];
    getrandom(buf, sizeof(buf), 0);
    printf("getrandom: ");
for (int i = 0; i < 16; i++) printf("%02x", buf[i]);
    printf("\n");

    srand(12345);
    printf("rand()x5:  ");
for (int i = 0; i < 5; i++) printf("%d ", rand() % 100);
    printf("\n\n");
}

/* ── 9. Time ─────────────────────────────────────────────────────── */
static void example_time(void) {
    printf("── 9. Time ─────────────────────────────\n");

    struct timespec mono, real;
    clock_gettime(CLOCK_MONOTONIC, &mono);
    clock_gettime(CLOCK_REALTIME,  &real);
    printf("CLOCK_MONOTONIC: %ld.%09ld\n", mono.tv_sec, mono.tv_nsec);
    printf("CLOCK_REALTIME:  %ld.%09ld\n", real.tv_sec, real.tv_nsec);

    struct timespec req = { 0, 1000000L };
    clock_gettime(CLOCK_MONOTONIC, &mono);
    nanosleep(&req, NULL);
    struct timespec after;
    clock_gettime(CLOCK_MONOTONIC, &after);
    long elapsed = (after.tv_sec  - mono.tv_sec)  * 1000000L
                 + (after.tv_nsec - mono.tv_nsec) / 1000L;
    printf("nanosleep(1ms):  elapsed ~%ld µs\n\n", elapsed);
}

/* ── 10. Environment ─────────────────────────────────────────────── */
static void example_env(void) {
    printf("── 10. Environment ─────────────────────\n");
    const char *vars[] = { "HOME", "SHELL", "USER", "PATH", "TERM" };
for (int i = 0; i < (int)ARRAY_LEN(vars); i++) {
        char *val = getenv(vars[i]);
        if (val) {
            char trunc[48];
            strlcpy(trunc, val, sizeof(trunc));
            if (strlen(val) >= sizeof(trunc))
                strlcat(trunc, "...", sizeof(trunc));
            printf("%-6s = %s\n", vars[i], trunc);
        }
    }
    printf("\n");
}

/* ── main ────────────────────────────────────────────────────────── */
int main(int argc, char **argv, char **envp) {
    (void)argc; (void)argv; (void)envp;

    printf("╔══════════════════════════════════════════╗\n");
    printf("║        libtool — AArch64 Example         ║\n");
    printf("║  -nostdlib -nostdinc -ffreestanding      ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    example_hello();
    example_strings();
    example_memory();
    example_math();
    example_sort();
    example_files();
    example_numbers();
    example_random();
    example_time();
    example_env();

    printf("Done.\n");
    return 0;
}
