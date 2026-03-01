#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#define INITIAL_BUFFER_SIZE 1024
#define LINE_CHUNK_SIZE 256

typedef struct {
    char **lines;
    size_t count;
    size_t capacity;
} LineBuffer;

typedef struct {
    int before;
    int after;
    const char *separator;
    int missing_newline;
} SeparatorOptions;

/* Initialize line buffer */
LineBuffer* init_line_buffer(void) {
    LineBuffer *lb = malloc(sizeof(LineBuffer));
    if (!lb) return NULL;
    
    lb->capacity = INITIAL_BUFFER_SIZE;
    lb->count = 0;
    lb->lines = malloc(lb->capacity * sizeof(char*));
    
    if (!lb->lines) {
        free(lb);
        return NULL;
    }
    
    return lb;
}

/* Add line to buffer */
int add_line(LineBuffer *lb, const char *line) {
    if (lb->count >= lb->capacity) {
        lb->capacity *= 2;
        char **new_lines = realloc(lb->lines, lb->capacity * sizeof(char*));
        if (!new_lines) return -1;
        lb->lines = new_lines;
    }
    
    lb->lines[lb->count] = strdup(line);
    if (!lb->lines[lb->count]) return -1;
    
    lb->count++;
    return 0;
}

/* Free line buffer */
void free_line_buffer(LineBuffer *lb) {
    if (!lb) return;
    
    for (size_t i = 0; i < lb->count; i++) {
        free(lb->lines[i]);
    }
    free(lb->lines);
    free(lb);
}

/* Read file into line buffer */
LineBuffer* read_file(const char *filename, SeparatorOptions *sep_opt) {
    FILE *fp;
    int should_close = 0;
    
    if (filename == NULL || strcmp(filename, "-") == 0) {
        fp = stdin;
    } else {
        fp = fopen(filename, "r");
        if (!fp) {
            fprintf(stderr, "tac: cannot open '%s' for reading\n", filename);
            return NULL;
        }
        should_close = 1;
    }
    
    LineBuffer *lb = init_line_buffer();
    if (!lb) {
        if (should_close) fclose(fp);
        return NULL;
    }
    
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    
    while ((read = getline(&line, &len, fp)) != -1) {
        /* Remove newline if present */
        if (read > 0 && line[read - 1] == '\n') {
            line[read - 1] = '\0';
        }
        
        if (add_line(lb, line) != 0) {
            fprintf(stderr, "tac: memory allocation failed\n");
            free(line);
            free_line_buffer(lb);
            if (should_close) fclose(fp);
            return NULL;
        }
    }
    
    free(line);
    
    if (should_close) fclose(fp);
    
    /* Handle missing newline at EOF */
    if (lb->count > 0 && sep_opt->missing_newline) {
        sep_opt->missing_newline = 1;
    }
    
    return lb;
}

/* Print lines in reverse order with separator handling */
void print_reverse(LineBuffer *lb, SeparatorOptions *sep_opt) {
    if (lb->count == 0) return;
    
    /* Handle --before option */
    if (sep_opt->before) {
        for (size_t i = 0; i < lb->count; i++) {
            if (i > 0) printf("%s", sep_opt->separator);
            printf("%s", lb->lines[i]);
        }
    } 
    /* Handle --after option (default) */
    else if (sep_opt->after) {
        for (size_t i = 0; i < lb->count; i++) {
            if (i > 0) printf("%s", sep_opt->separator);
            printf("%s", lb->lines[i]);
        }
    } 
    /* Default behavior: print in reverse */
    else {
        for (size_t i = lb->count; i > 0; i--) {
            printf("%s", lb->lines[i-1]);
            if (i > 1 || (sep_opt->missing_newline && sep_opt->after)) {
                printf("%s", sep_opt->separator);
            }
        }
    }
}

/* Print usage information */
void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [OPTION]... [FILE]...\n", prog_name);
    fprintf(stderr, "Write each FILE to standard output, last line first.\n\n");
    fprintf(stderr, "  -b, --before             attach the separator before instead of after\n");
    fprintf(stderr, "  -r, --regex               interpret the separator as a regular expression (not implemented)\n");
    fprintf(stderr, "  -s, --separator=STRING    use STRING as the separator instead of newline\n");
    fprintf(stderr, "  -h, --help                display this help and exit\n");
    fprintf(stderr, "  -V, --version             output version information and exit\n\n");
    fprintf(stderr, "If no FILE, or when FILE is -, read standard input.\n");
}

/* Print version information */
void print_version(void) {
    fprintf(stderr, "tac (custom implementation) 1.0\n");
    fprintf(stderr, "Copyright (C) 2024\n");
    fprintf(stderr, "This is free software: you are free to change and redistribute it.\n");
}

int main(int argc, char *argv[]) {
    int opt;
    int option_index = 0;
    SeparatorOptions sep_opt = {0, 1, "\n", 0}; /* Default: separator after, newline */
    
    static struct option long_options[] = {
        {"before",    no_argument,       0, 'b'},
        {"regex",     no_argument,       0, 'r'},
        {"separator", required_argument, 0, 's'},
        {"help",      no_argument,       0, 'h'},
        {"version",   no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };
    
    /* Parse command line options */
    while ((opt = getopt_long(argc, argv, "brs:hV", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'b':
                sep_opt.before = 1;
                sep_opt.after = 0;
                break;
            case 'r':
                fprintf(stderr, "tac: warning: regex option not fully implemented\n");
                break;
            case 's':
                sep_opt.separator = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'V':
                print_version();
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    /* Process files */
    if (optind >= argc) {
        /* No files specified, read from stdin */
        LineBuffer *lb = read_file("-", &sep_opt);
        if (lb) {
            print_reverse(lb, &sep_opt);
            free_line_buffer(lb);
        }
    } else {
        /* Process each file */
        int first_file = 1;
        for (int i = optind; i < argc; i++) {
            /* Add separator between files */
            if (!first_file) {
                printf("%s", sep_opt.separator);
            }
            first_file = 0;
            
            LineBuffer *lb = read_file(argv[i], &sep_opt);
            if (lb) {
                print_reverse(lb, &sep_opt);
                free_line_buffer(lb);
            }
        }
    }
    
    return 0;
}
