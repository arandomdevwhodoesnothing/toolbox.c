#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>

#define MAX_LINE_LENGTH 4096
#define MAX_FIELDS 1024
#define DELIMITER_MAX 10

typedef struct {
    int start;
    int end;
    int is_range;
} FieldRange;

typedef struct {
    FieldRange ranges[MAX_FIELDS];
    int count;
} FieldList;

// Global variables
char delimiter = '\t';  // Default delimiter
int delimiter_set = 0;
int complement = 0;      // Complement selection
int only_delimited = 0;  // Only output lines with delimiter
int output_delimiter_set = 0;
char output_delimiter = '\t';
FieldList field_list = { .count = 0 };
int fields_specified = 0;
int character_mode = 0;   // -c flag
int field_mode = 0;       // -f flag
int byte_mode = 0;        // -b flag (same as character mode in POSIX)

// Function prototypes
void parse_field_list(const char *list);
void parse_range(const char *range_str, FieldRange *range);
int is_field_selected(int field_num);
void process_file(FILE *file, const char *filename);
void process_field_mode(FILE *file);
void process_character_mode(FILE *file);
void print_usage(const char *prog_name);
void print_version(void);
int is_delimiter_present(const char *line);

int main(int argc, char *argv[]) {
    int opt;
    int option_index = 0;
    int error = 0;
    
    static struct option long_options[] = {
        {"complement", no_argument, 0, 'c'},
        {"delimiter", required_argument, 0, 'd'},
        {"fields", required_argument, 0, 'f'},
        {"characters", required_argument, 0, 'c'},
        {"bytes", required_argument, 0, 'b'},
        {"only-delimited", no_argument, 0, 's'},
        {"output-delimiter", required_argument, 0, 'o'},
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'v'},
        {0, 0, 0, 0}
    };
    
    // Parse command line arguments
    while ((opt = getopt_long(argc, argv, "b:c:d:f:ho:sv", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'b':  // Byte mode (same as character mode in standard cut)
            case 'c':  // Character mode
                if (field_mode) {
                    fprintf(stderr, "cut: cannot specify both field and character/byte mode\n");
                    error = 1;
                }
                character_mode = 1;
                parse_field_list(optarg);
                break;
                
            case 'd':  // Delimiter
                if (strlen(optarg) > 1) {
                    fprintf(stderr, "cut: the delimiter must be a single character\n");
                    error = 1;
                } else {
                    delimiter = optarg[0];
                    delimiter_set = 1;
                }
                break;
                
            case 'f':  // Field mode
                if (character_mode || byte_mode) {
                    fprintf(stderr, "cut: cannot specify both field and character/byte mode\n");
                    error = 1;
                }
                field_mode = 1;
                parse_field_list(optarg);
                break;
                
            case 'h':  // Help
                print_usage(argv[0]);
                return 0;
                
            case 'o':  // Output delimiter
                if (strlen(optarg) > 1) {
                    fprintf(stderr, "cut: the output delimiter must be a single character\n");
                    error = 1;
                } else {
                    output_delimiter = optarg[0];
                    output_delimiter_set = 1;
                }
                break;
                
            case 's':  // Only delimited
                only_delimited = 1;
                break;
                
            case 'v':  // Version
                print_version();
                return 0;
                
            default:
                error = 1;
                break;
        }
    }
    
    // Check if mode is specified
    if (!field_mode && !character_mode && !byte_mode) {
        fprintf(stderr, "cut: you must specify a list of bytes, characters, or fields\n");
        fprintf(stderr, "Try 'cut --help' for more information.\n");
        return 1;
    }
    
    if (error) {
        return 1;
    }
    
    // Set output delimiter to input delimiter if not specified
    if (!output_delimiter_set) {
        output_delimiter = delimiter;
    }
    
    // If complement is set and no fields specified, treat as all fields
    if (complement && field_list.count == 0) {
        // This is a special case - complement with no fields means select nothing
        // In practice, this would output nothing, but we'll handle it gracefully
    }
    
    // Process files
    if (optind >= argc) {
        // No files specified, read from stdin
        process_file(stdin, "stdin");
    } else {
        for (int i = optind; i < argc; i++) {
            FILE *file = fopen(argv[i], "r");
            if (!file) {
                fprintf(stderr, "cut: %s: No such file or directory\n", argv[i]);
                continue;
            }
            process_file(file, argv[i]);
            fclose(file);
        }
    }
    
    return 0;
}

void parse_field_list(const char *list) {
    char *list_copy = strdup(list);
    char *token;
    char *saveptr;
    
    token = strtok_r(list_copy, ",", &saveptr);
    while (token != NULL && field_list.count < MAX_FIELDS) {
        parse_range(token, &field_list.ranges[field_list.count]);
        field_list.count++;
        token = strtok_r(NULL, ",", &saveptr);
    }
    
    free(list_copy);
}

void parse_range(const char *range_str, FieldRange *range) {
    char *endptr;
    char *dash = strchr(range_str, '-');
    
    if (dash == NULL) {
        // Single number
        range->start = atoi(range_str);
        range->end = range->start;
        range->is_range = 0;
    } else {
        // Range
        range->is_range = 1;
        
        if (dash == range_str) {
            // -N format (from first to N)
            range->start = 1;
            range->end = atoi(range_str + 1);
        } else if (*(dash + 1) == '\0') {
            // N- format (from N to end)
            range->start = atoi(range_str);
            range->end = -1;  // Special value meaning "to end"
        } else {
            // N-M format
            range->start = atoi(range_str);
            range->end = atoi(dash + 1);
        }
    }
    
    // Validate ranges
    if (range->start < 1 || (range->end != -1 && range->end < range->start)) {
        fprintf(stderr, "cut: invalid field range '%s'\n", range_str);
        exit(1);
    }
}

int is_field_selected(int field_num) {
    if (field_num < 1) return 0;
    
    int selected = 0;
    
    for (int i = 0; i < field_list.count; i++) {
        FieldRange *range = &field_list.ranges[i];
        
        if (range->is_range) {
            if (range->end == -1) {
                // N- format (to end)
                if (field_num >= range->start) {
                    selected = 1;
                    break;
                }
            } else {
                // N-M format
                if (field_num >= range->start && field_num <= range->end) {
                    selected = 1;
                    break;
                }
            }
        } else {
            // Single field
            if (field_num == range->start) {
                selected = 1;
                break;
            }
        }
    }
    
    return complement ? !selected : selected;
}

void process_file(FILE *file, const char *filename) {
    if (field_mode) {
        process_field_mode(file);
    } else if (character_mode || byte_mode) {
        process_character_mode(file);
    }
}

int is_delimiter_present(const char *line) {
    return strchr(line, delimiter) != NULL;
}

void process_field_mode(FILE *file) {
    char line[MAX_LINE_LENGTH];
    char *fields[MAX_FIELDS];
    int field_count;
    int output_fields[MAX_FIELDS];
    int output_count;
    
    while (fgets(line, sizeof(line), file)) {
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
        }
        
        // Check if we should skip lines without delimiter
        if (only_delimited && !is_delimiter_present(line)) {
            continue;
        }
        
        // If no delimiter in line and only_delimited is false, output whole line
        if (!is_delimiter_present(line)) {
            printf("%s\n", line);
            continue;
        }
        
        // Parse fields
        field_count = 0;
        char *token;
        char *saveptr;
        char *line_copy = strdup(line);
        
        token = strtok_r(line_copy, &delimiter, &saveptr);
        while (token != NULL && field_count < MAX_FIELDS) {
            fields[field_count++] = token;
            token = strtok_r(NULL, &delimiter, &saveptr);
        }
        
        // Determine which fields to output
        output_count = 0;
        for (int i = 0; i < field_count; i++) {
            if (is_field_selected(i + 1)) {
                output_fields[output_count++] = i;
            }
        }
        
        // Output selected fields
        if (output_count > 0) {
            for (int i = 0; i < output_count; i++) {
                if (i > 0) {
                    printf("%c", output_delimiter);
                }
                printf("%s", fields[output_fields[i]]);
            }
            printf("\n");
        }
        
        free(line_copy);
    }
}

void process_character_mode(FILE *file) {
    char line[MAX_LINE_LENGTH];
    
    while (fgets(line, sizeof(line), file)) {
        // Remove trailing newline
        size_t len = strlen(line);
        int has_newline = 0;
        if (len > 0 && line[len-1] == '\n') {
            line[len-1] = '\0';
            has_newline = 1;
        }
        
        len = strlen(line);
        
        // Build output string
        char output[MAX_LINE_LENGTH] = "";
        int output_pos = 0;
        
        for (int i = 0; i < len; i++) {
            if (is_field_selected(i + 1)) {
                output[output_pos++] = line[i];
            }
        }
        output[output_pos] = '\0';
        
        // Output
        printf("%s", output);
        if (has_newline) {
            printf("\n");
        }
    }
}

void print_usage(const char *prog_name) {
    printf("Usage: %s OPTION... [FILE]...\n", prog_name);
    printf("Print selected parts of lines from each FILE to standard output.\n\n");
    printf("Mandatory arguments to long options are mandatory for short options too.\n");
    printf("  -b, --bytes=LIST        select only these bytes\n");
    printf("  -c, --characters=LIST   select only these characters\n");
    printf("  -d, --delimiter=DELIM   use DELIM instead of TAB for field delimiter\n");
    printf("  -f, --fields=LIST       select only these fields\n");
    printf("  -n                      (ignored)\n");
    printf("      --complement        complement the set of selected bytes, characters or fields\n");
    printf("  -s, --only-delimited    do not print lines not containing delimiters\n");
    printf("      --output-delimiter=STRING  use STRING as the output delimiter\n");
    printf("                          the default is to use the input delimiter\n");
    printf("      --help     display this help and exit\n");
    printf("      --version  output version information and exit\n\n");
    printf("Use one, and only one of -b, -c or -f.  Each LIST is made up of one range, or many\n");
    printf("ranges separated by commas.  Selected input is written in the same order that it\n");
    printf("is read, and is written exactly once.  Each range is one of:\n\n");
    printf("  N     N'th byte, character or field, counted from 1\n");
    printf("  N-    from N'th byte, character or field, to end of line\n");
    printf("  N-M   from N'th to M'th (included) byte, character or field\n");
    printf("  -M    from first to M'th (included) byte, character or field\n\n");
    printf("With no FILE, or when FILE is -, read standard input.\n");
}

void print_version(void) {
    printf("cut (custom implementation) 1.0\n");
    printf("Copyright (C) 2024\n");
    printf("This is free software: you are free to change and redistribute it.\n");
}
