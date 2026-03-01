#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#define BUFFER_SIZE 8192
#define MAX_FILES 256

typedef struct {
    int fd;
    char *path;
} OutputFile;

static OutputFile files[MAX_FILES];
static int file_count = 0;
static int ignore_interrupt = 0;
static char buffer[BUFFER_SIZE];

void signal_handler(int sig) {
    // Just exit on interrupt - standard tee behavior
    exit(130);
}

void cleanup(void) {
    for (int i = 0; i < file_count; i++) {
        if (files[i].fd != -1) {
            close(files[i].fd);
        }
        if (files[i].path) {
            free(files[i].path);
        }
    }
}

void print_usage(const char *program_name) {
    fprintf(stderr, 
        "Usage: %s [OPTION]... [FILE]...\n"
        "Copy standard input to each FILE, and also to standard output.\n\n"
        "Options:\n"
        "  -a, --append              append to the given FILEs, do not overwrite\n"
        "  -i, --ignore-interrupts   ignore interrupt signals\n"
        "  --help                    display this help and exit\n"
        "  --version                 output version information and exit\n",
        program_name);
}

void print_version(void) {
    fprintf(stderr, "tee (standard coreutils implementation) 1.0\n");
}

int open_output_file(const char *path, int append_mode) {
    int flags = O_WRONLY | O_CREAT;
    mode_t mode = 0644;  // Standard permissions: rw-r--r--
    
    if (append_mode) {
        flags |= O_APPEND;
    } else {
        flags |= O_TRUNC;
    }
    
    return open(path, flags, mode);
}

ssize_t full_write(int fd, const void *buf, size_t count) {
    size_t nwritten = 0;
    const char *ptr = buf;
    
    while (count > 0) {
        ssize_t written = write(fd, ptr, count);
        if (written <= 0) {
            if (written == -1 && errno == EINTR) {
                continue;  // Interrupted, retry
            }
            return nwritten > 0 ? nwritten : written;
        }
        nwritten += written;
        ptr += written;
        count -= written;
    }
    
    return nwritten;
}

int main(int argc, char *argv[]) {
    int opt;
    int option_index = 0;
    int append_mode = 0;
    
    static struct option long_options[] = {
        {"append",          no_argument, 0, 'a'},
        {"ignore-interrupts", no_argument, 0, 'i'},
        {"help",            no_argument, 0, 128},
        {"version",         no_argument, 0, 129},
        {0, 0, 0, 0}
    };
    
    // Parse command line options
    while ((opt = getopt_long(argc, argv, "ai", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'a':
                append_mode = 1;
                break;
            case 'i':
                ignore_interrupt = 1;
                break;
            case 128: // --help
                print_usage(argv[0]);
                exit(0);
            case 129: // --version
                print_version();
                exit(0);
            default:
                print_usage(argv[0]);
                exit(1);
        }
    }
    
    // Setup signal handlers
    if (!ignore_interrupt) {
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
    }
    signal(SIGPIPE, SIG_IGN);  // Standard tee ignores SIGPIPE
    
    // Register cleanup function
    atexit(cleanup);
    
    // Open output files
    for (int i = optind; i < argc; i++) {
        if (file_count >= MAX_FILES) {
            fprintf(stderr, "tee: too many files\n");
            exit(1);
        }
        
        int fd = open_output_file(argv[i], append_mode);
        
        if (fd == -1) {
            fprintf(stderr, "tee: %s: %s\n", argv[i], strerror(errno));
            exit(1);
        }
        
        files[file_count].fd = fd;
        files[file_count].path = strdup(argv[i]);
        file_count++;
    }
    
    // Main processing loop
    ssize_t bytes_read;
    
    while ((bytes_read = read(STDIN_FILENO, buffer, BUFFER_SIZE)) > 0) {
        ssize_t total_written = 0;
        ssize_t result;
        
        // Write to stdout
        result = full_write(STDOUT_FILENO, buffer, bytes_read);
        if (result != bytes_read) {
            // Error writing to stdout, exit with error
            if (result == -1) {
                fprintf(stderr, "tee: stdout: %s\n", strerror(errno));
            }
            exit(1);
        }
        
        // Write to all files
        for (int i = 0; i < file_count; i++) {
            result = full_write(files[i].fd, buffer, bytes_read);
            if (result != bytes_read) {
                if (result == -1) {
                    fprintf(stderr, "tee: %s: %s\n", files[i].path, strerror(errno));
                }
                // Don't exit on file write error, just continue (standard behavior)
            }
        }
    }
    
    // Check for read error
    if (bytes_read == -1) {
        fprintf(stderr, "tee: read error: %s\n", strerror(errno));
        exit(1);
    }
    
    return 0;
}
