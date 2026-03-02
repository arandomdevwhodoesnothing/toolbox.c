#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define BUFFER_SIZE 4096
#define ASCII_SIZE 256

typedef struct {
    bool delete;
    bool squeeze;
    bool complement;
    char *set1;
    char *set2;
    int set1_len;
    int set2_len;
} TrOptions;

// Function prototypes
void parse_range(char *s, bool *set);
void parse_escaped(char *s, char *result);
int parse_arguments(int argc, char *argv[], TrOptions *opts);
void build_translation_table(unsigned char trans[ASCII_SIZE], TrOptions *opts);
void process_stream(FILE *input, FILE *output, unsigned char trans[ASCII_SIZE], TrOptions *opts);

int main(int argc, char *argv[]) {
    TrOptions opts = {0};
    unsigned char translation_table[ASCII_SIZE];
    
    if (parse_arguments(argc, argv, &opts) != 0) {
        fprintf(stderr, "Usage: %s [OPTION]... SET1 [SET2]\n", argv[0]);
        fprintf(stderr, "  -d, --delete     delete characters in SET1\n");
        fprintf(stderr, "  -s, --squeeze-replace  replace each input sequence of repeated\n");
        fprintf(stderr, "                        characters with a single occurrence\n");
        fprintf(stderr, "  -c, --complement  use complement of SET1\n");
        return 1;
    }
    
    build_translation_table(translation_table, &opts);
    process_stream(stdin, stdout, translation_table, &opts);
    
    return 0;
}

void parse_escaped(char *s, char *result) {
    int i, j;
    for (i = 0, j = 0; s[i]; i++, j++) {
        if (s[i] == '\\') {
            i++;
            switch (s[i]) {
                case 'a': result[j] = '\a'; break;
                case 'b': result[j] = '\b'; break;
                case 'f': result[j] = '\f'; break;
                case 'n': result[j] = '\n'; break;
                case 'r': result[j] = '\r'; break;
                case 't': result[j] = '\t'; break;
                case 'v': result[j] = '\v'; break;
                case '\\': result[j] = '\\'; break;
                case '0': case '1': case '2': case '3':
                case '4': case '5': case '6': case '7': {
                    int octal = 0;
                    int count = 0;
                    while (s[i] >= '0' && s[i] <= '7' && count < 3) {
                        octal = octal * 8 + (s[i] - '0');
                        i++;
                        count++;
                    }
                    result[j] = (char)octal;
                    i--; // Adjust for loop increment
                    break;
                }
                default:
                    result[j] = s[i];
                    break;
            }
        } else {
            result[j] = s[i];
        }
    }
    result[j] = '\0';
}

void parse_range(char *s, bool *set) {
    int i;
    char last_char = 0;
    bool in_range = false;
    
    for (i = 0; s[i]; i++) {
        if (s[i] == '\\') {
            i++;
            char c;
            switch (s[i]) {
                case 'a': c = '\a'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'v': c = '\v'; break;
                default: c = s[i]; break;
            }
            set[(unsigned char)c] = true;
            last_char = c;
            in_range = false;
        } else if (s[i] == '-' && i > 0 && s[i+1] && !in_range) {
            in_range = true;
        } else {
            if (in_range) {
                char start = last_char;
                char end = s[i];
                if (start > end) {
                    char temp = start;
                    start = end;
                    end = temp;
                }
                for (int c = start; c <= end; c++) {
                    set[(unsigned char)c] = true;
                }
                in_range = false;
            } else {
                set[(unsigned char)s[i]] = true;
            }
            last_char = s[i];
        }
    }
}

int parse_arguments(int argc, char *argv[], TrOptions *opts) {
    int set1_index = -1;
    int set2_index = -1;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--delete") == 0) {
            opts->delete = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--squeeze-replace") == 0) {
            opts->squeeze = true;
        } else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--complement") == 0) {
            opts->complement = true;
        } else if (argv[i][0] != '-') {
            if (set1_index == -1) {
                set1_index = i;
            } else if (set2_index == -1) {
                set2_index = i;
            } else {
                return -1; // Too many arguments
            }
        } else {
            return -1; // Unknown option
        }
    }
    
    if (set1_index == -1) {
        return -1; // SET1 is required
    }
    
    // Parse SET1 with escape sequences
    char *temp1 = malloc(strlen(argv[set1_index]) + 1);
    parse_escaped(argv[set1_index], temp1);
    opts->set1 = temp1;
    opts->set1_len = strlen(temp1);
    
    if (set2_index != -1) {
        char *temp2 = malloc(strlen(argv[set2_index]) + 1);
        parse_escaped(argv[set2_index], temp2);
        opts->set2 = temp2;
        opts->set2_len = strlen(temp2);
    } else if (!opts->delete) {
        // If no SET2 and not delete mode, duplicate SET1
        opts->set2 = malloc(opts->set1_len + 1);
        strcpy(opts->set2, opts->set1);
        opts->set2_len = opts->set1_len;
    }
    
    // Validate arguments
    if (opts->delete && opts->set2 != NULL && !opts->squeeze) {
        // In delete mode, SET2 is only used with squeeze
        if (opts->squeeze) {
            // Valid: -d -s with SET2 for squeezing after deletion
        } else {
            fprintf(stderr, "tr: extra operand '%s'\n", opts->set2);
            return -1;
        }
    }
    
    return 0;
}

void build_translation_table(unsigned char trans[ASCII_SIZE], TrOptions *opts) {
    bool set1[ASCII_SIZE] = {false};
    bool set2[ASCII_SIZE] = {false};
    
    // Initialize translation table to identity mapping
    for (int i = 0; i < ASCII_SIZE; i++) {
        trans[i] = i;
    }
    
    // Parse SET1
    parse_range(opts->set1, set1);
    
    // Parse SET2 if exists
    if (opts->set2) {
        parse_range(opts->set2, set2);
    }
    
    if (opts->delete) {
        // For delete mode, mark characters to be deleted (mapped to 0)
        if (opts->complement) {
            // Delete characters NOT in SET1
            for (int i = 0; i < ASCII_SIZE; i++) {
                if (!set1[i]) {
                    trans[i] = 0; // Mark for deletion
                }
            }
        } else {
            // Delete characters in SET1
            for (int i = 0; i < ASCII_SIZE; i++) {
                if (set1[i]) {
                    trans[i] = 0; // Mark for deletion
                }
            }
        }
    } else {
        // Translation mode
        // Build list of SET1 characters
        unsigned char set1_chars[ASCII_SIZE];
        int set1_count = 0;
        for (int i = 0; i < ASCII_SIZE; i++) {
            if (set1[i]) {
                set1_chars[set1_count++] = i;
            }
        }
        
        // Build list of SET2 characters
        unsigned char set2_chars[ASCII_SIZE];
        int set2_count = 0;
        for (int i = 0; i < ASCII_SIZE; i++) {
            if (set2[i]) {
                set2_chars[set2_count++] = i;
            }
        }
        
        if (opts->complement) {
            // Translate characters NOT in SET1 to SET2
            int last_set2 = set2_count - 1;
            int set2_index = 0;
            
            for (int i = 0; i < ASCII_SIZE; i++) {
                if (!set1[i]) {
                    if (set2_index < set2_count) {
                        trans[i] = set2_chars[set2_index++];
                    } else {
                        trans[i] = set2_chars[last_set2]; // Repeat last character
                    }
                }
            }
        } else {
            // Translate characters in SET1 to SET2
            int last_set2 = set2_count - 1;
            
            for (int i = 0; i < set1_count; i++) {
                unsigned char from = set1_chars[i];
                if (i < set2_count) {
                    trans[from] = set2_chars[i];
                } else {
                    trans[from] = set2_chars[last_set2]; // Repeat last character
                }
            }
        }
    }
}

void process_stream(FILE *input, FILE *output, unsigned char trans[ASCII_SIZE], TrOptions *opts) {
    unsigned char buffer[BUFFER_SIZE];
    size_t bytes_read;
    unsigned char prev_char = 0;
    bool prev_printed = false;
    
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, input)) > 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            unsigned char c = buffer[i];
            unsigned char new_c = trans[c];
            
            if (opts->delete && new_c == 0) {
                // Character is marked for deletion
                continue;
            }
            
            if (opts->squeeze) {
                if (new_c == prev_char) {
                    // Check if we should squeeze this character
                    bool should_squeeze = false;
                    
                    if (opts->delete) {
                        // With -d -s, squeeze characters in SET2
                        bool set2[ASCII_SIZE] = {false};
                        if (opts->set2) {
                            parse_range(opts->set2, set2);
                        }
                        should_squeeze = set2[new_c];
                    } else {
                        // With -s only, squeeze characters in SET1 after translation
                        bool set1[ASCII_SIZE] = {false};
                        parse_range(opts->set1, set1);
                        
                        if (opts->complement) {
                            // Squeeze characters NOT in SET1
                            should_squeeze = !set1[new_c];
                        } else {
                            // Squeeze characters in SET1
                            should_squeeze = set1[new_c];
                        }
                    }
                    
                    if (should_squeeze && prev_printed) {
                        continue; // Skip this duplicate
                    }
                }
                
                // Output the character
                fputc(new_c, output);
                prev_char = new_c;
                prev_printed = true;
            } else {
                // No squeezing, just output
                fputc(new_c, output);
            }
        }
    }
    
    // Clean up allocated memory
    if (opts->set1) free(opts->set1);
    if (opts->set2) free(opts->set2);
}
