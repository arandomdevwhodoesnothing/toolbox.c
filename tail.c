#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>

#define DEFAULT_LINES 10
#define BUFFER_SIZE 4096
#define MAX_LINE_LENGTH 65536  // Maximum line length for in-memory operations

// Structure to hold program options
typedef struct {
    int num_lines;
    int num_bytes;
    int follow_mode;           // -f option
    int follow_retry;           // --retry option
    int show_headers;
    int quiet_mode;
    int zero_terminated;        // -z option
    int max_unchanged_stats;     // --max-unchanged-stats
    int pid_to_monitor;          // --pid
    int sleep_interval;          // -s option
    char **files;
    int file_count;
} TailOptions;

// Structure for circular buffer (for lines mode)
typedef struct {
    char **lines;
    int size;
    int start;
    int count;
    int total_allocated;
} CircularBuffer;

// Function prototypes
void print_usage(const char *program_name);
void print_version(void);
int parse_number(const char *str, long long *result);
int parse_count(const char *optarg, long long *count, int *count_from_start);
int process_file(const char *filename, TailOptions *opts);
int tail_lines(FILE *fp, int num_lines);
int tail_bytes(FILE *fp, int num_bytes);
int tail_from_start_lines(FILE *fp, int num_lines);
int tail_from_start_bytes(FILE *fp, int num_bytes);
int follow_file(const char *filename, TailOptions *opts);
void print_header(const char *filename);
void init_circular_buffer(CircularBuffer *cb, int size);
void add_to_circular_buffer(CircularBuffer *cb, const char *line);
void free_circular_buffer(CircularBuffer *cb);

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [OPTION]... [FILE]...\n", program_name);
    fprintf(stderr, "Print the last %d lines of each FILE to standard output.\n", DEFAULT_LINES);
    fprintf(stderr, "With more than one FILE, precede each with a header giving the file name.\n\n");
    
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -c, --bytes=[+]NUM       output the last NUM bytes; or use -c +NUM to\n");
    fprintf(stderr, "                             output starting with byte NUM of each file\n");
    fprintf(stderr, "  -f, --follow[={name|descriptor}] output appended data as the file grows;\n");
    fprintf(stderr, "                             -f, --follow, and --follow=descriptor are\n");
    fprintf(stderr, "                             equivalent\n");
    fprintf(stderr, "  -F                         same as --follow=name --retry\n");
    fprintf(stderr, "  -n, --lines=[+]NUM        output the last NUM lines, instead of the last %d;\n", DEFAULT_LINES);
    fprintf(stderr, "                             or use -n +NUM to output starting with line NUM\n");
    fprintf(stderr, "  -q, --quiet, --silent     never print headers giving file names\n");
    fprintf(stderr, "  -v, --verbose             always print headers giving file names\n");
    fprintf(stderr, "  -z, --zero-terminated     line delimiter is NUL, not newline\n");
    fprintf(stderr, "  -s, --sleep-interval=N    with -f, sleep for approximately N seconds\n");
    fprintf(stderr, "                             (default 1.0) between iterations\n");
    fprintf(stderr, "      --pid=PID            with -f, terminate after process ID, PID dies\n");
    fprintf(stderr, "      --retry               keep trying to open a file if it is inaccessible\n");
    fprintf(stderr, "      --max-unchanged-stats=N with --follow=name, reopen a file which has not\n");
    fprintf(stderr, "                             changed size for N iterations to see if it has\n");
    fprintf(stderr, "                             been unlinked or renamed\n");
    fprintf(stderr, "  -V, --version             output version information and exit\n");
    fprintf(stderr, "  -h, --help                display this help and exit\n\n");
    
    fprintf(stderr, "NUM may have a multiplier suffix: b 512, kB 1000, K 1024, MB 1000*1000,\n");
    fprintf(stderr, "M 1024*1024, GB 1000*1000*1000, G 1024*1024*1024, and so on for T, P, E, Z, Y.\n");
}

void print_version(void) {
    fprintf(stderr, "tail (custom implementation) 1.0\n");
    fprintf(stderr, "Copyright (C) 2024 Free Software Foundation, Inc.\n");
    fprintf(stderr, "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>.\n");
    fprintf(stderr, "This is free software: you are free to change and redistribute it.\n");
    fprintf(stderr, "There is NO WARRANTY, to the extent permitted by law.\n\n");
    fprintf(stderr, "Written by Custom Implementation.\n");
}

// Initialize circular buffer
void init_circular_buffer(CircularBuffer *cb, int size) {
    cb->size = size;
    cb->start = 0;
    cb->count = 0;
    cb->total_allocated = 0;
    cb->lines = malloc(size * sizeof(char *));
    if (!cb->lines) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }
    for (int i = 0; i < size; i++) {
        cb->lines[i] = NULL;
    }
}

// Add line to circular buffer
void add_to_circular_buffer(CircularBuffer *cb, const char *line) {
    int pos = (cb->start + cb->count) % cb->size;
    
    // Free old line if it exists
    if (cb->lines[pos] != NULL) {
        free(cb->lines[pos]);
        cb->total_allocated--;
    }
    
    cb->lines[pos] = strdup(line);
    if (cb->lines[pos]) {
        cb->total_allocated++;
    }
    
    if (cb->count < cb->size) {
        cb->count++;
    } else {
        cb->start = (cb->start + 1) % cb->size;
    }
}

// Free circular buffer
void free_circular_buffer(CircularBuffer *cb) {
    for (int i = 0; i < cb->size; i++) {
        if (cb->lines[i] != NULL) {
            free(cb->lines[i]);
        }
    }
    free(cb->lines);
}

// Parse number with optional multiplier suffix
int parse_number(const char *str, long long *result) {
    char *endptr;
    long long num = strtoll(str, &endptr, 10);
    
    if (endptr == str) {
        return -1;  // No digits found
    }
    
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
                return -1;  // Invalid suffix
        }
    }
    
    *result = num;
    return 0;
}

// Parse count with optional leading '+'
int parse_count(const char *optarg, long long *count, int *count_from_start) {
    *count_from_start = 0;
    
    if (optarg[0] == '+') {
        *count_from_start = 1;
        optarg++;
    }
    
    return parse_number(optarg, count);
}

// Parse command line options
void parse_options(int argc, char *argv[], TailOptions *opts) {
    int c;
    int option_index = 0;
    int f_option_count = 0;
    
    static struct option long_options[] = {
        {"bytes", required_argument, 0, 'c'},
        {"follow", optional_argument, 0, 'f'},
        {"lines", required_argument, 0, 'n'},
        {"quiet", no_argument, 0, 'q'},
        {"silent", no_argument, 0, 'q'},
        {"verbose", no_argument, 0, 'v'},
        {"zero-terminated", no_argument, 0, 'z'},
        {"sleep-interval", required_argument, 0, 's'},
        {"pid", required_argument, 0, 0},
        {"retry", no_argument, 0, 0},
        {"max-unchanged-stats", required_argument, 0, 0},
        {"version", no_argument, 0, 'V'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    
    // Set defaults
    opts->num_lines = DEFAULT_LINES;
    opts->num_bytes = 0;
    opts->follow_mode = 0;
    opts->follow_retry = 0;
    opts->show_headers = 0;
    opts->quiet_mode = 0;
    opts->zero_terminated = 0;
    opts->max_unchanged_stats = 5;  // Default value
    opts->pid_to_monitor = -1;
    opts->sleep_interval = 1;
    opts->files = NULL;
    opts->file_count = 0;
    
    while ((c = getopt_long(argc, argv, "c:n:fFqvzVs:h", long_options, &option_index)) != -1) {
        switch (c) {
            case 'c': {
                long long count;
                int from_start;
                if (parse_count(optarg, &count, &from_start) != 0) {
                    fprintf(stderr, "tail: invalid number of bytes: '%s'\n", optarg);
                    exit(EXIT_FAILURE);
                }
                if (from_start) {
                    opts->num_lines = 0;
                    opts->num_bytes = -count;  // Negative indicates from start
                } else {
                    opts->num_lines = 0;
                    opts->num_bytes = count;
                }
                break;
            }
            case 'n': {
                long long count;
                int from_start;
                if (parse_count(optarg, &count, &from_start) != 0) {
                    fprintf(stderr, "tail: invalid number of lines: '%s'\n", optarg);
                    exit(EXIT_FAILURE);
                }
                if (from_start) {
                    opts->num_bytes = 0;
                    opts->num_lines = -count;  // Negative indicates from start
                } else {
                    opts->num_bytes = 0;
                    opts->num_lines = count;
                }
                break;
            }
            case 'f':
                opts->follow_mode = 1;
                if (optarg) {
                    if (strcmp(optarg, "name") == 0) {
                        // Follow by name
                        opts->follow_mode = 2;
                    } else if (strcmp(optarg, "descriptor") == 0) {
                        opts->follow_mode = 1;
                    }
                }
                f_option_count++;
                break;
            case 'F':
                opts->follow_mode = 2;  // follow by name
                opts->follow_retry = 1;
                break;
            case 'q':
                opts->quiet_mode = 1;
                opts->show_headers = 0;
                break;
            case 'v':
                opts->show_headers = 1;
                opts->quiet_mode = 0;
                break;
            case 'z':
                opts->zero_terminated = 1;
                break;
            case 's':
                opts->sleep_interval = atoi(optarg);
                if (opts->sleep_interval <= 0) {
                    opts->sleep_interval = 1;
                }
                break;
            case 'V':
                print_version();
                exit(EXIT_SUCCESS);
            case 'h':
                print_usage(argv[0]);
                exit(EXIT_SUCCESS);
            case 0:
                // Handle long options
                if (strcmp(long_options[option_index].name, "pid") == 0) {
                    opts->pid_to_monitor = atoi(optarg);
                } else if (strcmp(long_options[option_index].name, "retry") == 0) {
                    opts->follow_retry = 1;
                } else if (strcmp(long_options[option_index].name, "max-unchanged-stats") == 0) {
                    opts->max_unchanged_stats = atoi(optarg);
                }
                break;
            case '?':
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            default:
                abort();
        }
    }
    
    // Handle -f being specified multiple times
    if (f_option_count > 1) {
        opts->follow_mode = 2;  // Follow by name if multiple -f options
    }
    
    // Collect remaining arguments as files
    if (optind < argc) {
        opts->file_count = argc - optind;
        opts->files = &argv[optind];
    }
}

// Print file header
void print_header(const char *filename) {
    printf("\n==> %s <==\n", filename);
}

// Tail using circular buffer (for regular tail)
int tail_lines(FILE *fp, int num_lines) {
    CircularBuffer cb;
    char buffer[MAX_LINE_LENGTH];
    int line_count = 0;
    
    if (num_lines <= 0) return 0;
    
    init_circular_buffer(&cb, num_lines);
    
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        add_to_circular_buffer(&cb, buffer);
        line_count++;
    }
    
    // Print the circular buffer contents in order
    for (int i = 0; i < cb.count; i++) {
        int pos = (cb.start + i) % cb.size;
        if (cb.lines[pos]) {
            fputs(cb.lines[pos], stdout);
        }
    }
    
    free_circular_buffer(&cb);
    return 0;
}

// Tail from start (for +NUM option) lines mode
int tail_from_start_lines(FILE *fp, int num_lines) {
    char buffer[MAX_LINE_LENGTH];
    int line_count = 0;
    int lines_to_skip = num_lines - 1;
    
    // Skip first num_lines-1 lines
    while (line_count < lines_to_skip && fgets(buffer, sizeof(buffer), fp) != NULL) {
        line_count++;
    }
    
    // Output the rest
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        fputs(buffer, stdout);
    }
    
    return 0;
}

// Tail bytes mode
int tail_bytes(FILE *fp, int num_bytes) {
    long file_size;
    long offset;
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    
    if (num_bytes <= 0) return 0;
    
    // Get file size
    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    
    // Calculate where to start reading
    if (num_bytes >= file_size) {
        offset = 0;
    } else {
        offset = file_size - num_bytes;
    }
    
    // Seek to the starting position
    fseek(fp, offset, SEEK_SET);
    
    // Read and output
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        fwrite(buffer, 1, bytes_read, stdout);
    }
    
    return 0;
}

// Tail from start (for +NUM option) bytes mode
int tail_from_start_bytes(FILE *fp, int num_bytes) {
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    
    // Seek to the specified byte position (num_bytes is positive here)
    if (num_bytes > 1) {
        fseek(fp, num_bytes - 1, SEEK_SET);
    }
    
    // Read and output from that position
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        fwrite(buffer, 1, bytes_read, stdout);
    }
    
    return 0;
}

// Follow file (like tail -f)
int follow_file(const char *filename, TailOptions *opts) {
    FILE *fp = NULL;
    long last_size = -1;
    int retry_count = 0;
    int unchanged_count = 0;
    
    while (1) {
        // Check if we should exit due to PID
        if (opts->pid_to_monitor > 0) {
            if (kill(opts->pid_to_monitor, 0) == -1 && errno == ESRCH) {
                // PID no longer exists
                break;
            }
        }
        
        // Try to open the file
        if (fp == NULL) {
            fp = fopen(filename, "r");
            if (fp == NULL) {
                if (opts->follow_retry) {
                    if (retry_count % 10 == 0) {  // Print every 10 attempts
                        fprintf(stderr, "tail: cannot open '%s' for reading: %s\n", 
                                filename, strerror(errno));
                        fprintf(stderr, "tail: will retry\n");
                    }
                    retry_count++;
                    sleep(opts->sleep_interval);
                    continue;
                } else {
                    fprintf(stderr, "tail: cannot open '%s' for reading\n", filename);
                    return 1;
                }
            }
            
            // Seek to end
            fseek(fp, 0, SEEK_END);
            last_size = ftell(fp);
        }
        
        // Check if file has grown
        fseek(fp, 0, SEEK_END);
        long current_size = ftell(fp);
        
        if (current_size > last_size) {
            // Read new data
            char buffer[BUFFER_SIZE];
            fseek(fp, last_size, SEEK_SET);
            
            while (fgets(buffer, sizeof(buffer), fp) != NULL) {
                fputs(buffer, stdout);
                fflush(stdout);
            }
            
            last_size = current_size;
            unchanged_count = 0;
        } else if (current_size < last_size) {
            // File was truncated or rotated
            if (opts->follow_mode == 2) {  // Follow by name
                fclose(fp);
                fp = NULL;
                fprintf(stderr, "tail: file '%s' has been replaced; following new file\n", filename);
            }
        } else {
            unchanged_count++;
            if (opts->follow_mode == 2 && unchanged_count >= opts->max_unchanged_stats) {
                // Check if file has been renamed/rotated by trying to reopen
                FILE *test_fp = fopen(filename, "r");
                if (test_fp) {
                    fseek(test_fp, 0, SEEK_END);
                    long new_size = ftell(test_fp);
                    fclose(test_fp);
                    
                    if (new_size != last_size) {
                        // File has changed
                        if (fp) fclose(fp);
                        fp = NULL;
                        unchanged_count = 0;
                        continue;
                    }
                }
            }
        }
        
        fflush(stdout);
        sleep(opts->sleep_interval);
    }
    
    if (fp) {
        fclose(fp);
    }
    
    return 0;
}

// Process a single file
int process_file(const char *filename, TailOptions *opts) {
    FILE *fp;
    int result = 0;
    
    if (strcmp(filename, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(filename, "r");
        if (fp == NULL) {
            fprintf(stderr, "tail: cannot open '%s' for reading: %s\n", 
                    filename, strerror(errno));
            return 1;
        }
    }
    
    // Print header if needed
    if (opts->show_headers || (opts->file_count > 1 && !opts->quiet_mode)) {
        print_header(filename);
    }
    
    // Process based on mode
    if (opts->num_bytes > 0) {
        // Regular tail by bytes
        result = tail_bytes(fp, opts->num_bytes);
    } else if (opts->num_bytes < 0) {
        // Tail from start by bytes (+NUM)
        result = tail_from_start_bytes(fp, -opts->num_bytes);
    } else if (opts->num_lines > 0) {
        // Regular tail by lines
        result = tail_lines(fp, opts->num_lines);
    } else if (opts->num_lines < 0) {
        // Tail from start by lines (+NUM)
        result = tail_from_start_lines(fp, -opts->num_lines);
    } else {
        // Default: last 10 lines
        result = tail_lines(fp, DEFAULT_LINES);
    }
    
    // Follow mode if requested and not reading from stdin
    if (opts->follow_mode && fp != stdin) {
        fclose(fp);
        result = follow_file(filename, opts);
    } else if (fp != stdin) {
        fclose(fp);
    }
    
    return result;
}

int main(int argc, char *argv[]) {
    TailOptions opts;
    int exit_status = 0;
    int i;
    
    // Parse command line options
    parse_options(argc, argv, &opts);
    
    // If no files specified, read from stdin
    if (opts.file_count == 0) {
        static char *stdin_only[] = { "-" };
        opts.files = stdin_only;
        opts.file_count = 1;
        opts.show_headers = 0;
        
        // Follow mode with stdin doesn't make sense
        if (opts.follow_mode) {
            fprintf(stderr, "tail: warning: -f option ignored when reading from stdin\n");
            opts.follow_mode = 0;
        }
    }
    
    // Process each file
    for (i = 0; i < opts.file_count; i++) {
        if (process_file(opts.files[i], &opts) != 0) {
            exit_status = 1;
        }
        
        // In follow mode with multiple files, only the last one is followed
        if (opts.follow_mode && i < opts.file_count - 1) {
            // Just process without follow for earlier files
            TailOptions temp_opts = opts;
            temp_opts.follow_mode = 0;
            process_file(opts.files[i], &temp_opts);
        }
    }
    
    return exit_status;
}
