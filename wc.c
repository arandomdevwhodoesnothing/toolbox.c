#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>

#define BUFFER_SIZE 8192

// Structure to hold counts
typedef struct {
    long lines;
    long words;
    long chars;
    long bytes;
    char *filename;
} Counts;

// Function prototypes
void init_counts(Counts *counts);
void process_file(FILE *file, Counts *counts, int count_chars);
void print_counts(Counts *counts, int show_lines, int show_words, 
                  int show_chars, int show_bytes, int show_totals);
void print_usage(char *program_name);

int main(int argc, char *argv[]) {
    int opt;
    int show_lines = 0;
    int show_words = 0;
    int show_chars = 0;
    int show_bytes = 0;
    int show_totals = 0;
    int files_processed = 0;
    Counts total_counts;
    
    // Parse command line options
    while ((opt = getopt(argc, argv, "lwcmLh")) != -1) {
        switch (opt) {
            case 'l':
                show_lines = 1;
                break;
            case 'w':
                show_words = 1;
                break;
            case 'c':
                show_bytes = 1;
                break;
            case 'm':
                show_chars = 1;
                break;
            case 'L':
                // Max line length - would need additional implementation
                fprintf(stderr, "Warning: -L option (max line length) not implemented in this version\n");
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    // If no options specified, show all counts (like standard wc)
    if (!show_lines && !show_words && !show_chars && !show_bytes) {
        show_lines = show_words = show_bytes = 1;
    }
    
    init_counts(&total_counts);
    total_counts.filename = "total";
    
    // Process files or stdin
    if (optind >= argc) {
        // Read from stdin
        Counts counts;
        init_counts(&counts);
        counts.filename = NULL;
        
        process_file(stdin, &counts, show_chars);
        print_counts(&counts, show_lines, show_words, show_chars, show_bytes, 0);
        
        // Add to totals
        total_counts.lines += counts.lines;
        total_counts.words += counts.words;
        total_counts.chars += counts.chars;
        total_counts.bytes += counts.bytes;
        files_processed++;
    } else {
        // Process each file
        for (int i = optind; i < argc; i++) {
            FILE *file = fopen(argv[i], "rb");
            if (!file) {
                fprintf(stderr, "wc: %s: No such file or directory\n", argv[i]);
                continue;
            }
            
            Counts counts;
            init_counts(&counts);
            counts.filename = argv[i];
            
            process_file(file, &counts, show_chars);
            print_counts(&counts, show_lines, show_words, show_chars, show_bytes, 0);
            
            fclose(file);
            
            // Add to totals
            total_counts.lines += counts.lines;
            total_counts.words += counts.words;
            total_counts.chars += counts.chars;
            total_counts.bytes += counts.bytes;
            files_processed++;
        }
    }
    
    // Print totals if more than one file
    if (files_processed > 1) {
        print_counts(&total_counts, show_lines, show_words, show_chars, show_bytes, 1);
    }
    
    return 0;
}

void init_counts(Counts *counts) {
    counts->lines = 0;
    counts->words = 0;
    counts->chars = 0;
    counts->bytes = 0;
    counts->filename = NULL;
}

void process_file(FILE *file, Counts *counts, int count_chars) {
    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    int in_word = 0;
    long local_chars = 0;
    int last_char = 0;
    
    // Reset file position to beginning if not stdin
    if (file != stdin) {
        fseek(file, 0, SEEK_SET);
    }
    
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        counts->bytes += bytes_read;
        
        for (size_t i = 0; i < bytes_read; i++) {
            unsigned char c = buffer[i];
            
            // Count characters if -m option is used
            if (count_chars) {
                // Simple character counting - for multi-byte characters, 
                // we'd need more sophisticated handling
                local_chars++;
            }
            
            // Count lines
            if (c == '\n') {
                counts->lines++;
            }
            
            // Count words
            if (isspace(c)) {
                in_word = 0;
            } else if (!in_word) {
                in_word = 1;
                counts->words++;
            }
        }
    }
    
    // For character count, we use the actual byte count if -m not specified
    // Standard wc uses different counting for -c (bytes) and -m (chars)
    counts->chars = count_chars ? local_chars : counts->bytes;
}

void print_counts(Counts *counts, int show_lines, int show_words, 
                  int show_chars, int show_bytes, int is_total) {
    
    // Standard wc output format: right-aligned numbers in 7-character fields
    if (show_lines) {
        printf("%7ld ", counts->lines);
    }
    if (show_words) {
        printf("%7ld ", counts->words);
    }
    if (show_chars) {
        printf("%7ld ", counts->chars);
    }
    if (show_bytes) {
        printf("%7ld ", counts->bytes);
    }
    
    if (counts->filename) {
        if (is_total) {
            printf("total\n");
        } else {
            printf("%s\n", counts->filename);
        }
    } else {
        printf("\n");
    }
}

void print_usage(char *program_name) {
    printf("Usage: %s [OPTION]... [FILE]...\n", program_name);
    printf("Print line, word, and byte counts for each FILE, and a total if more than one FILE.\n");
    printf("With no FILE, read standard input.\n\n");
    printf("Options:\n");
    printf("  -c, --bytes            print the byte counts\n");
    printf("  -m, --chars            print the character counts\n");
    printf("  -l, --lines            print the newline counts\n");
    printf("  -w, --words            print the word counts\n");
    printf("  -L, --max-line-length  print the maximum display width\n");
    printf("  -h, --help             display this help and exit\n");
}
