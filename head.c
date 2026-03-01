#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#define DEFAULT_LINES 10
#define BUFFER_SIZE 4096

// Structure to hold program options
typedef struct {
    int num_lines;
    int num_bytes;
    int show_headers;
    int quiet_mode;
    char **files;
    int file_count;
} HeadOptions;

// Function prototypes
void print_usage(const char *program_name);
void print_version(void);
int process_file(const char *filename, HeadOptions *opts);
void print_header(const char *filename);
int head_lines(FILE *fp, int num_lines);
int head_bytes(FILE *fp, int num_bytes);

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [OPTION]... [FILE]...\n", program_name);
    fprintf(stderr, "Print the first %d lines of each FILE to standard output.\n", DEFAULT_LINES);
    fprintf(stderr, "With more than one FILE, precede each with a header giving the file name.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c, --bytes=[-]NUM       print the first NUM bytes of each file;\n");
    fprintf(stderr, "                             with the leading '-', print all but the last\n");
    fprintf(stderr, "                             NUM bytes of each file\n");
    fprintf(stderr, "  -n, --lines=[-]NUM       print the first NUM lines instead of the first %d;\n", DEFAULT_LINES);
    fprintf(stderr, "                             with the leading '-', print all but the last\n");
    fprintf(stderr, "                             NUM lines of each file\n");
    fprintf(stderr, "  -q, --quiet, --silent    never print headers giving file names\n");
    fprintf(stderr, "  -v, --verbose             always print headers giving file names\n");
    fprintf(stderr, "  -V, --version             output version information and exit\n");
    fprintf(stderr, "  -h, --help                display this help and exit\n\n");
    fprintf(stderr, "NUM may have a multiplier suffix: b 512, kB 1000, K 1024, MB 1000*1000,\n");
    fprintf(stderr, "M 1024*1024, GB 1000*1000*1000, G 1024*1024*1024, and so on for T, P, E, Z, Y.\n");
}

void print_version(void) {
    fprintf(stderr, "head (custom implementation) 1.0\n");
    fprintf(stderr, "Copyright (C) 2024 Free Software Foundation, Inc.\n");
    fprintf(stderr, "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
    fprintf(stderr, "This is free software: you are free to change and redistribute it.\n");
    fprintf(stderr, "There is NO WARRANTY, to the extent permitted by law.\n\n");
    fprintf(stderr, "Written by Custom Implementation.\n");
}

// Parse number with optional multiplier suffix
long long parse_number_with_multiplier(const char *str) {
    char *endptr;
    long long num = strtoll(str, &endptr, 10);
    
    if (*endptr != '\0') {
        switch (*endptr) {
            case 'b': num *= 512; break;
            case 'K': num *= 1024; break;
            case 'M': num *= 1024 * 1024; break;
            case 'G': num *= 1024 * 1024 * 1024; break;
            case 'T': num *= 1024LL * 1024 * 1024 * 1024; break;
            case 'P': num *= 1024LL * 1024 * 1024 * 1024 * 1024; break;
            case 'E': num *= 1024LL * 1024 * 1024 * 1024 * 1024 * 1024; break;
            default:
                fprintf(stderr, "Invalid multiplier suffix: %s\n", str);
                exit(EXIT_FAILURE);
        }
    }
    
    return num;
}

// Parse number with optional negative sign
int parse_count(const char *optarg, long long *count, int *count_from_end) {
    *count_from_end = 0;
    
    if (optarg[0] == '-') {
        *count_from_end = 1;
        optarg++;
    }
    
    *count = parse_number_with_multiplier(optarg);
    return 0;
}

// Process command line options
void parse_options(int argc, char *argv[], HeadOptions *opts) {
    int c;
    int option_index = 0;
    
    static struct option long_options[] = {
        {"bytes", required_argument, 0, 'c'},
        {"lines", required_argument, 0, 'n'},
        {"quiet", no_argument, 0, 'q'},
        {"silent", no_argument, 0, 'q'},
        {"verbose", no_argument, 0, 'v'},
        {"version", no_argument, 0, 'V'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    opts->num_lines = DEFAULT_LINES;
    opts->num_bytes = 0;  // 0 means use lines mode
    opts->show_headers = 0;  // Auto: show only with multiple files
    opts->quiet_mode = 0;
    opts->files = NULL;
    opts->file_count = 0;
    
    while ((c = getopt_long(argc, argv, "c:n:qvVh", long_options, &option_index)) != -1) {
        switch (c) {
            case 'c': {
                long long count;
                int from_end;
                if (parse_count(optarg, &count, &from_end) != 0) {
                    fprintf(stderr, "Invalid byte count: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                if (from_end) {
                    fprintf(stderr, "head: option --bytes=-NUM not supported in this implementation\n");
                    exit(EXIT_FAILURE);
                }
                opts->num_bytes = count;
                opts->num_lines = 0;
                break;
            }
            case 'n': {
                long long count;
                int from_end;
                if (parse_count(optarg, &count, &from_end) != 0) {
                    fprintf(stderr, "Invalid line count: %s\n", optarg);
                    exit(EXIT_FAILURE);
                }
                if (from_end) {
                    fprintf(stderr, "head: option --lines=-NUM not supported in this implementation\n");
                    exit(EXIT_FAILURE);
                }
                opts->num_lines = count;
                opts->num_bytes = 0;
                break;
            }
            case 'q':
                opts->quiet_mode = 1;
                opts->show_headers = 0;
                break;
            case 'v':
                opts->show_headers = 1;
                opts->quiet_mode = 0;
                break;
            case 'V':
                print_version();
                exit(EXIT_SUCCESS);
            case 'h':
                print_usage(argv[0]);
                exit(EXIT_SUCCESS);
            case '?':
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            default:
                abort();
        }
    }
    
    // Collect remaining arguments as files
    if (optind < argc) {
        opts->file_count = argc - optind;
        opts->files = &argv[optind];
    }
}

// Print file header
void print_header(const char *filename) {
    printf("==> %s <==\n", filename);
}

// Read and print first N lines from file
int head_lines(FILE *fp, int num_lines) {
    char buffer[BUFFER_SIZE];
    int lines_printed = 0;
    int in_line = 0;
    
    while (lines_printed < num_lines && fgets(buffer, sizeof(buffer), fp) != NULL) {
        fputs(buffer, stdout);
        if (buffer[strlen(buffer) - 1] == '\n') {
            lines_printed++;
        }
    }
    
    return 0;
}

// Read and print first N bytes from file
int head_bytes(FILE *fp, int num_bytes) {
    char buffer[BUFFER_SIZE];
    int bytes_remaining = num_bytes;
    int bytes_read;
    
    while (bytes_remaining > 0 && 
           (bytes_read = fread(buffer, 1, 
                               (bytes_remaining < BUFFER_SIZE) ? bytes_remaining : BUFFER_SIZE, 
                               fp)) > 0) {
        fwrite(buffer, 1, bytes_read, stdout);
        bytes_remaining -= bytes_read;
    }
    
    return 0;
}

// Process a single file
int process_file(const char *filename, HeadOptions *opts) {
    FILE *fp;
    
    if (strcmp(filename, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(filename, "r");
        if (fp == NULL) {
            fprintf(stderr, "head: cannot open '%s' for reading: ", filename);
            perror("");
            return 1;
        }
    }
    
    // Print header if needed
    if (opts->show_headers || (opts->file_count > 1 && !opts->quiet_mode)) {
        print_header(filename);
    }
    
    // Process based on mode
    if (opts->num_bytes > 0) {
        head_bytes(fp, opts->num_bytes);
    } else {
        head_lines(fp, opts->num_lines);
    }
    
    // Add newline between files if needed
    if (opts->file_count > 1 && !opts->quiet_mode && filename != opts->files[opts->file_count - 1]) {
        printf("\n");
    }
    
    if (fp != stdin) {
        fclose(fp);
    }
    
    return 0;
}

int main(int argc, char *argv[]) {
    HeadOptions opts;
    int exit_status = 0;
    int i;
    
    // Parse command line options
    parse_options(argc, argv, &opts);
    
    // If no files specified, read from stdin
    if (opts.file_count == 0) {
        static char *stdin_only[] = { "-" };
        opts.files = stdin_only;
        opts.file_count = 1;
        opts.show_headers = 0;  // Don't show header for stdin
    }
    
    // Process each file
    for (i = 0; i < opts.file_count; i++) {
        if (process_file(opts.files[i], &opts) != 0) {
            exit_status = 1;
        }
    }
    
    return exit_status;
}
