#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <getopt.h>
#include <stdbool.h>
#include <limits.h>

#define MAX_LINE_LENGTH 8192

/* Structure to hold program options */
typedef struct {
    bool count;           /* -c: prefix lines by the number of occurrences */
    bool repeated;        /* -d: only print duplicate lines */
    bool all_repeated;    /* -D: print all duplicate lines */
    bool unique;          /* -u: only print unique lines */
    int skip_fields;      /* -f N: avoid comparing the first N fields */
    int skip_chars;       /* -s N: avoid comparing the first N characters */
    int compare_chars;    /* -w N: compare no more than N characters in lines */
    bool ignore_case;     /* -i: ignore case when comparing */
    bool zero_terminated; /* -z: lines are terminated by 0 byte, not newline */
    bool show_version;    /* --version */
    bool show_help;       /* --help */
} uniq_options;

/* Global options */
uniq_options options = {0};

/* Function prototypes */
void print_version(void);
void print_help(const char *program_name);
char *process_line(const char *line);
int compare_lines(const char *line1, const char *line2);
char *skip_fields(const char *line, int fields);
char *skip_chars(const char *line, int chars);
void usage(const char *program_name);

/* Print version information */
void print_version(void) {
    printf("uniq (custom implementation) 1.0\n");
    printf("Copyright (C) 2026\n");
}

/* Print help information */
void print_help(const char *program_name) {
    printf("Usage: %s [OPTION]... [INPUT [OUTPUT]]\n", program_name);
    printf("Filter adjacent matching lines from INPUT (or standard input),\n");
    printf("writing to OUTPUT (or standard output).\n\n");
    printf("With no options, matching lines are merged to the first occurrence.\n\n");
    printf("Options:\n");
    printf("  -c, --count           prefix lines by the number of occurrences\n");
    printf("  -d, --repeated        only print duplicate lines, one for each group\n");
    printf("  -D, --all-repeated    print all duplicate lines\n");
    printf("  -f, --skip-fields=N   avoid comparing the first N fields\n");
    printf("  -i, --ignore-case     ignore case differences when comparing\n");
    printf("  -s, --skip-chars=N    avoid comparing the first N characters\n");
    printf("  -u, --unique          only print unique lines\n");
    printf("  -w, --check-chars=N   compare no more than N characters in lines\n");
    printf("  -z, --zero-terminated line delimiter is NUL, not newline\n");
    printf("      --help            display this help and exit\n");
    printf("      --version         output version information and exit\n");
}

/* Skip specified number of fields in a line */
char *skip_fields(const char *line, int fields) {
    if (fields <= 0) return (char *)line;
    
    const char *ptr = line;
    int field_count = 0;
    
    while (*ptr && field_count < fields) {
        /* Skip leading delimiters */
        while (*ptr && isspace(*ptr)) ptr++;
        if (!*ptr) break;
        
        /* Skip the field content */
        while (*ptr && !isspace(*ptr)) ptr++;
        field_count++;
        
        /* Skip trailing delimiters after the field */
        while (*ptr && isspace(*ptr)) ptr++;
    }
    
    return (char *)ptr;
}

/* Skip specified number of characters in a line */
char *skip_chars(const char *line, int chars) {
    if (chars <= 0) return (char *)line;
    
    const char *ptr = line;
    int count = 0;
    
    while (*ptr && count < chars) {
        ptr++;
        count++;
    }
    
    return (char *)ptr;
}

/* Compare two lines according to options */
int compare_lines(const char *line1, const char *line2) {
    const char *s1 = line1;
    const char *s2 = line2;
    
    /* Skip fields if requested */
    if (options.skip_fields > 0) {
        s1 = skip_fields(s1, options.skip_fields);
        s2 = skip_fields(s2, options.skip_fields);
    }
    
    /* Skip characters if requested */
    if (options.skip_chars > 0) {
        s1 = skip_chars(s1, options.skip_chars);
        s2 = skip_chars(s2, options.skip_chars);
    }
    
    /* Compare with optional case insensitivity and character limit */
    int chars_compared = 0;
    
    while (*s1 && *s2) {
        if (options.compare_chars > 0 && chars_compared >= options.compare_chars) {
            return 0; /* Reached character limit, lines match so far */
        }
        
        char c1 = options.ignore_case ? tolower(*s1) : *s1;
        char c2 = options.ignore_case ? tolower(*s2) : *s2;
        
        if (c1 != c2) {
            return (c1 - c2);
        }
        
        s1++;
        s2++;
        chars_compared++;
    }
    
    /* Check if we hit the character limit */
    if (options.compare_chars > 0 && chars_compared >= options.compare_chars) {
        return 0;
    }
    
    /* If one string ended but we haven't hit the limit, check lengths */
    if (!*s1 && !*s2) return 0;
    if (!*s1) return -1;
    return 1;
}

/* Process a line: strip newline and handle zero termination */
char *process_line(const char *line) {
    static char buffer[MAX_LINE_LENGTH];
    size_t len = strlen(line);
    
    /* Remove trailing delimiter */
    if (len > 0) {
        char delim = options.zero_terminated ? '\0' : '\n';
        if (line[len-1] == delim) {
            len--;
        }
    }
    
    /* Copy to buffer */
    memcpy(buffer, line, len);
    buffer[len] = '\0';
    
    return buffer;
}

/* Usage message for errors */
void usage(const char *program_name) {
    fprintf(stderr, "Try '%s --help' for more information.\n", program_name);
}

/* Main uniq implementation */
int uniq_file(FILE *input, FILE *output) {
    char buffer[MAX_LINE_LENGTH];
    char *current_line = NULL;
    char *next_line = NULL;
    int line_count = 1;
    
    /* Read first line */
    if (fgets(buffer, sizeof(buffer), input) == NULL) {
        return 0; /* Empty file */
    }
    
    current_line = strdup(process_line(buffer));
    if (!current_line) {
        perror("Memory allocation failed");
        return -1;
    }
    
    /* Process the rest of the file */
    while (fgets(buffer, sizeof(buffer), input) != NULL) {
        next_line = process_line(buffer);
        
        /* Compare current line with next line */
        if (compare_lines(current_line, next_line) == 0) {
            /* Lines are identical according to options */
            line_count++;
        } else {
            /* Lines are different - output current line based on options */
            if (options.repeated) {
                /* Only print if there were duplicates */
                if (line_count > 1) {
                    if (options.count) {
                        fprintf(output, "%7d %s\n", line_count, current_line);
                    } else {
                        fprintf(output, "%s\n", current_line);
                    }
                }
            } else if (options.unique) {
                /* Only print if line was unique */
                if (line_count == 1) {
                    if (options.count) {
                        fprintf(output, "%7d %s\n", 1, current_line);
                    } else {
                        fprintf(output, "%s\n", current_line);
                    }
                }
            } else if (options.all_repeated) {
                /* Print all duplicate lines */
                if (line_count > 1) {
                    /* For each duplicate in the group */
                    for (int i = 0; i < line_count; i++) {
                        fprintf(output, "%s\n", current_line);
                    }
                }
            } else {
                /* Default behavior: print first occurrence with optional count */
                if (options.count) {
                    fprintf(output, "%7d %s\n", line_count, current_line);
                } else {
                    fprintf(output, "%s\n", current_line);
                }
            }
            
            /* Free current line and set to next */
            free(current_line);
            current_line = strdup(next_line);
            if (!current_line) {
                perror("Memory allocation failed");
                return -1;
            }
            line_count = 1;
        }
    }
    
    /* Handle the last line */
    if (options.repeated) {
        if (line_count > 1) {
            if (options.count) {
                fprintf(output, "%7d %s\n", line_count, current_line);
            } else {
                fprintf(output, "%s\n", current_line);
            }
        }
    } else if (options.unique) {
        if (line_count == 1) {
            if (options.count) {
                fprintf(output, "%7d %s\n", 1, current_line);
            } else {
                fprintf(output, "%s\n", current_line);
            }
        }
    } else if (options.all_repeated) {
        if (line_count > 1) {
            for (int i = 0; i < line_count; i++) {
                fprintf(output, "%s\n", current_line);
            }
        }
    } else {
        if (options.count) {
            fprintf(output, "%7d %s\n", line_count, current_line);
        } else {
            fprintf(output, "%s\n", current_line);
        }
    }
    
    free(current_line);
    return 0;
}

int main(int argc, char *argv[]) {
    FILE *input = stdin;
    FILE *output = stdout;
    const char *input_file = NULL;
    const char *output_file = NULL;
    
    /* Initialize options to zero */
    memset(&options, 0, sizeof(options));
    
    /* Long options */
    static struct option long_options[] = {
        {"count", no_argument, 0, 'c'},
        {"repeated", no_argument, 0, 'd'},
        {"all-repeated", no_argument, 0, 'D'},
        {"skip-fields", required_argument, 0, 'f'},
        {"ignore-case", no_argument, 0, 'i'},
        {"skip-chars", required_argument, 0, 's'},
        {"unique", no_argument, 0, 'u'},
        {"check-chars", required_argument, 0, 'w'},
        {"zero-terminated", no_argument, 0, 'z'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "cdf:is:uw:zDhv", long_options, NULL)) != -1) {
        switch (opt) {
            case 'c':
                options.count = true;
                break;
            case 'd':
                options.repeated = true;
                break;
            case 'D':
                options.all_repeated = true;
                break;
            case 'f':
                options.skip_fields = atoi(optarg);
                if (options.skip_fields < 0) options.skip_fields = 0;
                break;
            case 'i':
                options.ignore_case = true;
                break;
            case 's':
                options.skip_chars = atoi(optarg);
                if (options.skip_chars < 0) options.skip_chars = 0;
                break;
            case 'u':
                options.unique = true;
                break;
            case 'w':
                options.compare_chars = atoi(optarg);
                if (options.compare_chars < 0) options.compare_chars = 0;
                break;
            case 'z':
                options.zero_terminated = true;
                break;
            case 'h':
                print_help(argv[0]);
                return 0;
            case 'v':
                print_version();
                return 0;
            default:
                usage(argv[0]);
                return 1;
        }
    }
    
    /* Check for mutually exclusive options */
    int exclusive_count = (options.count ? 1 : 0) + 
                         (options.repeated ? 1 : 0) + 
                         (options.unique ? 1 : 0) + 
                         (options.all_repeated ? 1 : 0);
    if (exclusive_count > 1) {
        fprintf(stderr, "uniq: options -c, -d, -D, and -u are mutually exclusive\n");
        usage(argv[0]);
        return 1;
    }
    
    /* Parse input/output files */
    if (optind < argc) {
        input_file = argv[optind++];
        if (optind < argc) {
            output_file = argv[optind++];
            if (optind < argc) {
                fprintf(stderr, "uniq: extra operand '%s'\n", argv[optind]);
                usage(argv[0]);
                return 1;
            }
        }
    }
    
    /* Open input file if specified */
    if (input_file) {
        input = fopen(input_file, "r");
        if (!input) {
            perror(input_file);
            return 1;
        }
    }
    
    /* Open output file if specified */
    if (output_file) {
        output = fopen(output_file, "w");
        if (!output) {
            perror(output_file);
            if (input != stdin) fclose(input);
            return 1;
        }
    }
    
    /* Run uniq */
    int result = uniq_file(input, output);
    
    /* Cleanup */
    if (input != stdin) fclose(input);
    if (output != stdout) fclose(output);
    
    return result;
}
