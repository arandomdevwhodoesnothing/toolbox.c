#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define BUFFER_SIZE PATH_MAX

void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s [OPTION]... FILE\n", progname);
    fprintf(stderr, "Display value of a symbolic link on standard output.\n\n");
    fprintf(stderr, "  -f, --canonicalize     canonicalize by following every symlink in\n");
    fprintf(stderr, "                          every component of the given name recursively\n");
    fprintf(stderr, "  -e, --canonicalize-existing   canonicalize by following every symlink in\n");
    fprintf(stderr, "                          every component recursively, all components must exist\n");
    fprintf(stderr, "  -m, --canonicalize-missing   canonicalize by following every symlink in\n");
    fprintf(stderr, "                          every component recursively, without requirements on\n");
    fprintf(stderr, "                          components existence\n");
    fprintf(stderr, "  -n, --no-newline        do not output the trailing newline\n");
    fprintf(stderr, "  -q, --quiet, -s         suppress most error messages (same as -s for compatibility)\n");
    fprintf(stderr, "  -v, --verbose           report error messages\n");
    fprintf(stderr, "  -z, --zero              end each output line with NUL, not newline\n");
    fprintf(stderr, "      --help               display this help and exit\n");
    fprintf(stderr, "      --version            output version information and exit\n");
}

void print_version() {
    printf("readlink (custom implementation) 1.0\n");
    printf("Copyright (C) 2024\n");
    printf("This is free software: you are free to change and redistribute it.\n");
}

char *canonicalize_missing_path(const char *path) {
    char *dup_path = strdup(path);
    if (!dup_path) return NULL;
    
    char *last_slash = strrchr(dup_path, '/');
    
    if (last_slash == NULL) {
        // No slash, just return the path
        return dup_path;
    }
    
    *last_slash = '\0';
    char *dir_part = dup_path;
    char *file_part = last_slash + 1;
    
    // Try to resolve the directory part
    char dir_resolved[PATH_MAX];
    char *result = NULL;
    
    if (realpath(dir_part, dir_resolved) != NULL) {
        // Combine resolved directory with file part
        if (asprintf(&result, "%s/%s", dir_resolved, file_part) == -1) {
            result = NULL;
        }
        free(dup_path);
    } else {
        // Can't resolve directory, return original
        result = dup_path;
    }
    
    return result;
}

int main(int argc, char *argv[]) {
    int opt;
    int option_index = 0;
    int canonicalize = 0;
    int canonicalize_existing = 0;
    int canonicalize_missing = 0;
    int no_newline = 0;
    int quiet = 0;
    int verbose = 0;
    int zero = 0;
    char *path = NULL;
    
    static struct option long_options[] = {
        {"canonicalize", no_argument, 0, 'f'},
        {"canonicalize-existing", no_argument, 0, 'e'},
        {"canonicalize-missing", no_argument, 0, 'm'},
        {"no-newline", no_argument, 0, 'n'},
        {"quiet", no_argument, 0, 'q'},
        {"silent", no_argument, 0, 's'},
        {"verbose", no_argument, 0, 'v'},
        {"zero", no_argument, 0, 'z'},
        {"help", no_argument, 0, 256},
        {"version", no_argument, 0, 257},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "femnqsvz", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'f':
                canonicalize = 1;
                break;
            case 'e':
                canonicalize_existing = 1;
                break;
            case 'm':
                canonicalize_missing = 1;
                break;
            case 'n':
                no_newline = 1;
                break;
            case 'q':
            case 's':
                quiet = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            case 'z':
                zero = 1;
                break;
            case 256: // --help
                print_usage(argv[0]);
                return 0;
            case 257: // --version
                print_version();
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Check if we have a file argument
    if (optind >= argc) {
        if (!quiet) {
            fprintf(stderr, "%s: missing operand\n", argv[0]);
            print_usage(argv[0]);
        }
        return 1;
    }
    
    path = argv[optind];
    
    // Check for multiple file arguments
    if (optind + 1 < argc) {
        if (!quiet) {
            fprintf(stderr, "%s: extra operand '%s'\n", argv[0], argv[optind + 1]);
            print_usage(argv[0]);
        }
        return 1;
    }
    
    // Handle conflicting canonicalize options
    int canon_count = canonicalize + canonicalize_existing + canonicalize_missing;
    if (canon_count > 1) {
        if (!quiet) {
            fprintf(stderr, "%s: only one of -f, -e, -m can be specified\n", argv[0]);
        }
        return 1;
    }
    
    char *result = NULL;
    char buffer[PATH_MAX];
    int needs_free = 0;
    
    if (canonicalize || canonicalize_existing || canonicalize_missing) {
        // Use realpath for canonicalization
        if (realpath(path, buffer) != NULL) {
            result = buffer;
        } else {
            if (canonicalize_existing || (!canonicalize_missing && errno == ENOENT)) {
                if (!quiet) {
                    fprintf(stderr, "%s: %s: %s\n", argv[0], path, strerror(errno));
                }
                return 1;
            } else if (canonicalize_missing) {
                // For -m, we need to manually canonicalize as much as possible
                result = canonicalize_missing_path(path);
                if (result == NULL) {
                    if (!quiet) {
                        fprintf(stderr, "%s: %s: %s\n", argv[0], path, strerror(errno));
                    }
                    return 1;
                }
                needs_free = (result != path && result != buffer);
            } else {
                if (!quiet) {
                    fprintf(stderr, "%s: %s: %s\n", argv[0], path, strerror(errno));
                }
                return 1;
            }
        }
    } else {
        // Regular readlink
        ssize_t len = readlink(path, buffer, sizeof(buffer) - 1);
        
        if (len == -1) {
            if (!quiet) {
                if (verbose) {
                    fprintf(stderr, "%s: %s: %s\n", argv[0], path, strerror(errno));
                } else {
                    // Standard readlink suppresses errors by default
                    // But we'll output a minimal error for missing files
                    if (errno != ENOENT || !quiet) {
                        fprintf(stderr, "%s: %s: %s\n", argv[0], path, strerror(errno));
                    }
                }
            }
            return 1;
        }
        
        buffer[len] = '\0';
        result = buffer;
    }
    
    // Output the result
    if (result != NULL) {
        fputs(result, stdout);
        
        if (zero) {
            // Write a null character, not as part of format string
            putchar('\0');
        } else if (!no_newline) {
            putchar('\n');
        }
        
        // Free allocated memory if necessary
        if (needs_free) {
            free(result);
        }
    }
    
    return 0;
}
