/*
 * md5sum - compute and check MD5 message digests
 * Implements the standard md5sum utility from scratch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

/* ===== MD5 Implementation ===== */

typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t  buffer[64];
} MD5_CTX;

static const uint32_t T[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
};

static const int S[64] = {
    7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
    5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
    4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
    6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21,
};

#define ROTLEFT(x,n) (((x)<<(n))|((x)>>(32-(n))))

#define F(b,c,d) (((b)&(c))|(~(b)&(d)))
#define G(b,c,d) (((b)&(d))|((c)&~(d)))
#define H(b,c,d) ((b)^(c)^(d))
#define I(b,c,d) ((c)^((b)|~(d)))

static void md5_transform(uint32_t state[4], const uint8_t block[64])
{
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t M[16];
    int i;

    for (i = 0; i < 16; i++)
        M[i] = (uint32_t)block[i*4]
             | ((uint32_t)block[i*4+1] << 8)
             | ((uint32_t)block[i*4+2] << 16)
             | ((uint32_t)block[i*4+3] << 24);

    for (i = 0; i < 64; i++) {
        uint32_t f, g, temp;
        if (i < 16)      { f = F(b,c,d); g = i; }
        else if (i < 32) { f = G(b,c,d); g = (5*i+1)%16; }
        else if (i < 48) { f = H(b,c,d); g = (3*i+5)%16; }
        else             { f = I(b,c,d); g = (7*i)%16; }

        temp = d;
        d = c;
        c = b;
        b = b + ROTLEFT(a + f + M[g] + T[i], S[i]);
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

static void md5_init(MD5_CTX *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->count[0] = ctx->count[1] = 0;
}

static void md5_update(MD5_CTX *ctx, const uint8_t *data, size_t len)
{
    size_t idx = (ctx->count[0] >> 3) & 0x3f;

    ctx->count[0] += (uint32_t)(len << 3);
    if (ctx->count[0] < (uint32_t)(len << 3))
        ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);

    size_t part = 64 - idx;
    size_t i = 0;

    if (len >= part) {
        memcpy(&ctx->buffer[idx], data, part);
        md5_transform(ctx->state, ctx->buffer);
        for (i = part; i + 63 < len; i += 64)
            md5_transform(ctx->state, data + i);
        idx = 0;
    }
    memcpy(&ctx->buffer[idx], data + i, len - i);
}

static void md5_final(uint8_t digest[16], MD5_CTX *ctx)
{
    static const uint8_t padding[64] = { 0x80 };
    uint8_t bits[8];
    uint32_t lo = ctx->count[0], hi = ctx->count[1];

    bits[0] = lo & 0xff; bits[1] = (lo>>8)&0xff;
    bits[2] = (lo>>16)&0xff; bits[3] = (lo>>24)&0xff;
    bits[4] = hi & 0xff; bits[5] = (hi>>8)&0xff;
    bits[6] = (hi>>16)&0xff; bits[7] = (hi>>24)&0xff;

    size_t idx = (ctx->count[0] >> 3) & 0x3f;
    size_t pad = (idx < 56) ? (56 - idx) : (120 - idx);
    md5_update(ctx, padding, pad);
    md5_update(ctx, bits, 8);

    for (int i = 0; i < 4; i++) {
        digest[i*4]   = ctx->state[i] & 0xff;
        digest[i*4+1] = (ctx->state[i] >> 8) & 0xff;
        digest[i*4+2] = (ctx->state[i] >> 16) & 0xff;
        digest[i*4+3] = (ctx->state[i] >> 24) & 0xff;
    }
}

/* ===== Utility ===== */

static void digest_to_hex(const uint8_t digest[16], char hex[33])
{
    static const char *h = "0123456789abcdef";
    for (int i = 0; i < 16; i++) {
        hex[i*2]   = h[(digest[i] >> 4) & 0xf];
        hex[i*2+1] = h[digest[i] & 0xf];
    }
    hex[32] = '\0';
}

/* Compute MD5 of an open FILE stream. Returns 0 on success. */
static int md5_file(FILE *fp, char hex[33])
{
    MD5_CTX ctx;
    uint8_t buf[65536], digest[16];
    size_t n;

    md5_init(&ctx);
    while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
        md5_update(&ctx, buf, n);
    if (ferror(fp))
        return -1;
    md5_final(digest, &ctx);
    digest_to_hex(digest, hex);
    return 0;
}

/* ===== Check mode ===== */

/*
 * Parse a line from a checksum file.
 * Format: <hex>  <name>   (two spaces = binary, one space = text, both accepted)
 * Returns 1 on parse success, 0 on failure.
 */
static int parse_check_line(char *line, char **hex_out, int *binary_out, char **name_out)
{
    /* Strip trailing newline */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
        line[--len] = '\0';

    /* Skip blank lines and comments */
    if (len == 0 || line[0] == '#')
        return 0;

    /* 32 hex chars + ' ' + mode + filename */
    if (len < 35)
        return 0;
    for (int i = 0; i < 32; i++)
        if (!((line[i]>='0'&&line[i]<='9')||(line[i]>='a'&&line[i]<='f')||(line[i]>='A'&&line[i]<='F')))
            return 0;

    if (line[32] != ' ')
        return 0;

    char mode = line[33]; /* ' ' (text) or '*' (binary) */
    if (line[34] == '\0')
        return 0;
    if (mode != ' ' && mode != '*')
        return 0;

    *hex_out    = line;
    line[32]    = '\0';
    *binary_out = (mode == '*');
    *name_out   = line + 34;
    return 1;
}

/* ===== main ===== */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [OPTION]... [FILE]...\n"
        "Print or check MD5 (128-bit) checksums.\n"
        "\n"
        "With no FILE, or when FILE is -, read standard input.\n"
        "\n"
        "  -b, --binary         read in binary mode\n"
        "  -c, --check          read MD5 sums from the FILEs and check them\n"
        "      --tag            create a BSD-style checksum\n"
        "  -t, --text           read in text mode (default)\n"
        "  -z, --zero           end each output line with NUL, not newline,\n"
        "                       and disable file name escaping\n"
        "\n"
        "The following five options are useful only when verifying checksums:\n"
        "      --ignore-missing don't fail or report status for missing files\n"
        "      --quiet          don't print OK for each successfully verified file\n"
        "      --status         don't output anything, status code shows success\n"
        "      --strict         exit non-zero for improperly formatted checksum lines\n"
        "  -w, --warn           warn about improperly formatted checksum lines\n"
        "\n"
        "      --help           display this help and exit\n"
        "      --version        output version information and exit\n",
        prog);
}

int main(int argc, char *argv[])
{
    int opt_binary        = 0;
    int opt_check         = 0;
    int opt_tag           = 0;
    int opt_zero          = 0;
    int opt_ignore_missing= 0;
    int opt_quiet         = 0;
    int opt_status        = 0;
    int opt_strict        = 0;
    int opt_warn          = 0;

    int i;
    int file_start = argc; /* index where file args begin */

    /* Parse options */
    for (i = 1; i < argc; i++) {
        if (argv[i][0] != '-' || strcmp(argv[i], "-") == 0)
            break;
        if (strcmp(argv[i], "--") == 0) { i++; break; }

        /* Long options */
        if (strncmp(argv[i], "--", 2) == 0) {
            if      (strcmp(argv[i], "--binary")         == 0) opt_binary = 1;
            else if (strcmp(argv[i], "--check")          == 0) opt_check  = 1;
            else if (strcmp(argv[i], "--tag")            == 0) opt_tag    = 1;
            else if (strcmp(argv[i], "--text")           == 0) opt_binary = 0;
            else if (strcmp(argv[i], "--zero")           == 0) opt_zero   = 1;
            else if (strcmp(argv[i], "--ignore-missing") == 0) opt_ignore_missing = 1;
            else if (strcmp(argv[i], "--quiet")          == 0) opt_quiet  = 1;
            else if (strcmp(argv[i], "--status")         == 0) opt_status = 1;
            else if (strcmp(argv[i], "--strict")         == 0) opt_strict = 1;
            else if (strcmp(argv[i], "--warn")           == 0) opt_warn   = 1;
            else if (strcmp(argv[i], "--help")           == 0) { usage(argv[0]); return 0; }
            else if (strcmp(argv[i], "--version")        == 0) {
                printf("md5sum (custom implementation) 1.0\n");
                return 0;
            } else {
                fprintf(stderr, "%s: unrecognized option '%s'\n", argv[0], argv[i]);
                fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                return 1;
            }
            continue;
        }

        /* Short options (may be combined) */
        for (int j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
                case 'b': opt_binary = 1; break;
                case 'c': opt_check  = 1; break;
                case 't': opt_binary = 0; break;
                case 'z': opt_zero   = 1; break;
                case 'w': opt_warn   = 1; break;
                default:
                    fprintf(stderr, "%s: invalid option -- '%c'\n", argv[0], argv[i][j]);
                    fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
                    return 1;
            }
        }
    }
    file_start = i;

    char eol = opt_zero ? '\0' : '\n';
    int exit_code = 0;

    if (!opt_check) {
        /* ---- Compute mode ---- */
        int no_files = (file_start >= argc);

        if (no_files) {
            /* Read from stdin */
            char hex[33];
            if (md5_file(stdin, hex) != 0) {
                fprintf(stderr, "%s: stdin: %s\n", argv[0], strerror(errno));
                return 1;
            }
            if (opt_tag)
                printf("MD5 (-) = %s%c", hex, eol);
            else
                printf("%s %c-%c", hex, opt_binary ? '*' : ' ', eol);
            return 0;
        }

        for (i = file_start; i < argc; i++) {
            const char *name = argv[i];
            FILE *fp;
            int is_stdin = (strcmp(name, "-") == 0);

            if (is_stdin) {
                fp = stdin;
            } else {
                fp = fopen(name, opt_binary ? "rb" : "r");
                if (!fp) {
                    fprintf(stderr, "%s: %s: %s\n", argv[0], name, strerror(errno));
                    exit_code = 1;
                    continue;
                }
            }

            char hex[33];
            int err = md5_file(fp, hex);
            if (!is_stdin) fclose(fp);

            if (err) {
                fprintf(stderr, "%s: %s: %s\n", argv[0], name, strerror(errno));
                exit_code = 1;
                continue;
            }

            if (opt_tag) {
                printf("MD5 (%s) = %s%c", name, hex, eol);
            } else {
                /* Escape backslash and newline in name (unless --zero) */
                if (!opt_zero && (strchr(name, '\\') || strchr(name, '\n'))) {
                    /* Print leading backslash to signal escaping */
                    putchar('\\');
                    printf("%s ", hex);
                    putchar(opt_binary ? '*' : ' ');
                    for (const char *p = name; *p; p++) {
                        if (*p == '\\') { putchar('\\'); putchar('\\'); }
                        else if (*p == '\n') { putchar('\\'); putchar('n'); }
                        else putchar(*p);
                    }
                    putchar(eol);
                } else {
                    printf("%s %c%s%c", hex, opt_binary ? '*' : ' ', name, eol);
                }
            }
        }
    } else {
        /* ---- Check mode ---- */
        int no_files = (file_start >= argc);
        int total_checked = 0, total_failed = 0, total_missing = 0, total_invalid = 0;

        /* Helper lambda-like to process one check file */
        #define CHECK_FILE(cfp, cname) do { \
            char line[65536]; \
            int lineno = 0; \
            while (fgets(line, sizeof(line), cfp)) { \
                lineno++; \
                char *hex_str, *fname; \
                int is_bin; \
                if (!parse_check_line(line, &hex_str, &is_bin, &fname)) { \
                    if (opt_warn) \
                        fprintf(stderr, "%s: %s: %d: improperly formatted MD5 checksum line\n", \
                                argv[0], cname, lineno); \
                    total_invalid++; \
                    continue; \
                } \
                FILE *tfp; \
                int missing = 0; \
                tfp = fopen(fname, is_bin ? "rb" : "r"); \
                if (!tfp) { \
                    missing = 1; \
                    if (!opt_ignore_missing) { \
                        if (!opt_status) \
                            fprintf(stderr, "%s: %s: No such file or directory\n", argv[0], fname); \
                        total_missing++; \
                        total_failed++; \
                    } \
                } \
                if (!missing) { \
                    char computed[33]; \
                    int err = md5_file(tfp, computed); \
                    fclose(tfp); \
                    total_checked++; \
                    if (err || strcasecmp(computed, hex_str) != 0) { \
                        if (!opt_status) \
                            printf("%s: FAILED%c", fname, eol); \
                        total_failed++; \
                    } else { \
                        if (!opt_quiet && !opt_status) \
                            printf("%s: OK%c", fname, eol); \
                    } \
                } \
            } \
        } while(0)

        if (no_files) {
            CHECK_FILE(stdin, "stdin");
        } else {
            for (i = file_start; i < argc; i++) {
                const char *cname = argv[i];
                if (strcmp(cname, "-") == 0) {
                    CHECK_FILE(stdin, "stdin");
                } else {
                    FILE *cfp = fopen(cname, "r");
                    if (!cfp) {
                        fprintf(stderr, "%s: %s: %s\n", argv[0], cname, strerror(errno));
                        exit_code = 1;
                        continue;
                    }
                    CHECK_FILE(cfp, cname);
                    fclose(cfp);
                }
            }
        }

        if (total_failed > 0) {
            exit_code = 1;
            if (!opt_status)
                fprintf(stderr, "%s: WARNING: %d computed checksum%s did NOT match\n",
                        argv[0], total_failed, total_failed == 1 ? "" : "s");
        }
        if (opt_strict && total_invalid > 0)
            exit_code = 1;
        if (total_invalid > 0 && !opt_status)
            fprintf(stderr, "%s: WARNING: %d line%s improperly formatted\n",
                    argv[0], total_invalid, total_invalid == 1 ? "" : "s");
        if (opt_ignore_missing && total_checked == 0 && !opt_status) {
            fprintf(stderr, "%s: --ignore-missing was specified but no file was skipped\n", argv[0]);
        }
    }

    return exit_code;
}
