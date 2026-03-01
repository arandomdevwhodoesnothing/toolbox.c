/*
 * yes.c - Implementation of the yes command
 *
 * Repeatedly outputs a string (default "y") until killed.
 * Optimized with large buffered writes like GNU coreutils yes.
 *
 * Usage: yes [STRING]...
 *        yes --help
 *        yes --version
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#define VERSION     "1.0.0"
#define PROGRAM     "yes"
#define BUFSIZE     (8 * 1024)  /* 8 KB output buffer */

static void usage(void) {
    printf("Usage: %s [STRING]...\n", PROGRAM);
    printf("  or:  %s OPTION\n", PROGRAM);
    printf("\nRepeatedly output a line with all specified STRING(s), or 'y'.\n\n");
    printf("      --help     display this help and exit\n");
    printf("      --version  output version information and exit\n");
}

static void version(void) {
    printf("%s version %s\n", PROGRAM, VERSION);
    printf("A from-scratch implementation of the GNU yes command.\n");
}

int main(int argc, char *argv[]) {
    /* Handle --help and --version (only when they are the sole argument) */
    if (argc == 2) {
        if (strcmp(argv[1], "--help") == 0)    { usage();   return EXIT_SUCCESS; }
        if (strcmp(argv[1], "--version") == 0) { version(); return EXIT_SUCCESS; }
    }

    /* Build the output line from arguments, space-separated, newline-terminated */
    char line[BUFSIZE];
    size_t line_len = 0;

    if (argc == 1) {
        /* Default: just "y\n" */
        line[0] = 'y';
        line[1] = '\n';
        line_len = 2;
    } else {
        for (int i = 1; i < argc; i++) {
            size_t arg_len = strlen(argv[i]);
            /* Check we won't overflow (line + space + arg + newline) */
            if (line_len + arg_len + 2 > sizeof(line)) {
                fprintf(stderr, "%s: argument too long\n", PROGRAM);
                return EXIT_FAILURE;
            }
            if (i > 1) line[line_len++] = ' ';
            memcpy(line + line_len, argv[i], arg_len);
            line_len += arg_len;
        }
        line[line_len++] = '\n';
    }

    /*
     * Performance: fill a large buffer with as many copies of `line` as fit,
     * then write the buffer in one call. This matches GNU coreutils behaviour
     * and achieves near-maximum throughput.
     */
    char buf[BUFSIZE];
    size_t buf_len = 0;

    /* Fill buffer with repeated copies of the line */
    while (buf_len + line_len <= sizeof(buf)) {
        memcpy(buf + buf_len, line, line_len);
        buf_len += line_len;
    }

    /* Write forever until SIGPIPE or write error (e.g. pipe closed) */
    while (1) {
        size_t written = 0;
        while (written < buf_len) {
            ssize_t n = write(STDOUT_FILENO, buf + written, buf_len - written);
            if (n < 0) {
                /* EPIPE means the reader closed the pipe â€” normal exit */
                if (errno == EPIPE)
                    return EXIT_SUCCESS;
                fprintf(stderr, "%s: write error: %s\n", PROGRAM, strerror(errno));
                return EXIT_FAILURE;
            }
            written += (size_t)n;
        }
    }
}
