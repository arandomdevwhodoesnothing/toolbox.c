#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#define BUFFER_SIZE 1024

// Function to process escape sequences
void process_escapes(const char *src, char *dst) {
    while (*src) {
        if (*src == '\\') {
            src++; // Skip backslash
            switch (*src) {
                case 'a': *dst++ = '\a'; break; // Alert (bell)
                case 'b': *dst++ = '\b'; break; // Backspace
                case 'c': // No newline (special case)
                    return;
                case 'e':
                case 'E': *dst++ = '\033'; break; // Escape character
                case 'f': *dst++ = '\f'; break; // Form feed
                case 'n': *dst++ = '\n'; break; // Newline
                case 'r': *dst++ = '\r'; break; // Carriage return
                case 't': *dst++ = '\t'; break; // Horizontal tab
                case 'v': *dst++ = '\v'; break; // Vertical tab
                case '\\': *dst++ = '\\'; break; // Backslash
                case '0': { // Octal value
                    int i = 0;
                    int val = 0;
                    while (i < 3 && src[1] >= '0' && src[1] <= '7') {
                        val = val * 8 + (*++src - '0');
                        i++;
                    }
                    *dst++ = val;
                    break;
                }
                case 'x': { // Hexadecimal value
                    src++; // Skip 'x'
                    int val = 0;
                    int i = 0;
                    while (i < 2 && 
                           ((src[0] >= '0' && src[0] <= '9') ||
                            (src[0] >= 'a' && src[0] <= 'f') ||
                            (src[0] >= 'A' && src[0] <= 'F'))) {
                        if (src[0] >= '0' && src[0] <= '9')
                            val = val * 16 + (src[0] - '0');
                        else if (src[0] >= 'a' && src[0] <= 'f')
                            val = val * 16 + (src[0] - 'a' + 10);
                        else if (src[0] >= 'A' && src[0] <= 'F')
                            val = val * 16 + (src[0] - 'A' + 10);
                        src++;
                        i++;
                    }
                    *dst++ = val;
                    src--; // Adjust because we'll increment at the end of loop
                    break;
                }
                default: // Unknown escape sequence - output literally
                    *dst++ = '\\';
                    *dst++ = *src;
                    break;
            }
        } else {
            *dst++ = *src;
        }
        src++;
    }
    *dst = '\0';
}

// Function to print usage information
void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [OPTIONS] [STRING...]\n", program_name);
    fprintf(stderr, "Echo the STRING(s) to standard output.\n\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -n             do not output the trailing newline\n");
    fprintf(stderr, "  -e             enable interpretation of backslash escapes\n");
    fprintf(stderr, "  -E             disable interpretation of backslash escapes (default)\n");
    fprintf(stderr, "  --help         display this help and exit\n");
    fprintf(stderr, "  --version      output version information and exit\n\n");
    fprintf(stderr, "If -e is in effect, the following sequences are recognized:\n");
    fprintf(stderr, "  \\a     alert (bell)\n");
    fprintf(stderr, "  \\b     backspace\n");
    fprintf(stderr, "  \\c     produce no further output\n");
    fprintf(stderr, "  \\e     escape\n");
    fprintf(stderr, "  \\f     form feed\n");
    fprintf(stderr, "  \\n     new line\n");
    fprintf(stderr, "  \\r     carriage return\n");
    fprintf(stderr, "  \\t     horizontal tab\n");
    fprintf(stderr, "  \\v     vertical tab\n");
    fprintf(stderr, "  \\\\     backslash\n");
    fprintf(stderr, "  \\0NNN  byte with octal value NNN (1 to 3 digits)\n");
    fprintf(stderr, "  \\xHH   byte with hexadecimal value HH (1 to 2 digits)\n");
}

int main(int argc, char *argv[]) {
    int no_newline = 0;
    int enable_escapes = 0; // Default: escapes disabled
    int opt;
    
    // Define long options
    static struct option long_options[] = {
        {"help", no_argument, 0, 1},
        {"version", no_argument, 0, 2},
        {0, 0, 0, 0}
    };
    
    // Parse command line options
    while ((opt = getopt_long(argc, argv, "neE", long_options, NULL)) != -1) {
        switch (opt) {
            case 'n':
                no_newline = 1;
                break;
            case 'e':
                enable_escapes = 1;
                break;
            case 'E':
                enable_escapes = 0;
                break;
            case 1: // --help
                print_usage(argv[0]);
                return 0;
            case 2: // --version
                printf("echo (custom implementation) version 1.0\n");
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // Check if there's anything to print
    if (optind >= argc) {
        // Just print newline if no arguments
        if (!no_newline) {
            printf("\n");
        }
        return 0;
    }
    
    // Process and print each argument
    for (int i = optind; i < argc; i++) {
        if (enable_escapes) {
            // Process escape sequences
            char processed[BUFFER_SIZE];
            process_escapes(argv[i], processed);
            printf("%s", processed);
        } else {
            printf("%s", argv[i]);
        }
        
        // Print space between arguments (but not after the last one)
        if (i < argc - 1) {
            printf(" ");
        }
    }
    
    // Print newline unless suppressed
    if (!no_newline) {
        printf("\n");
    }
    
    return 0;
}
