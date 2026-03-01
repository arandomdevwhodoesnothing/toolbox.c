#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    // Check if we have at least one argument
    if (argc < 2) {
        fprintf(stderr, "Usage: %s NAME [SUFFIX]\n", argv[0]);
        return 1;
    }

    // Get the path (first argument)
    char *path = argv[1];
    
    // Handle empty string
    if (path[0] == '\0') {
        printf("\n");
        return 0;
    }
    
    // Find the last slash (if any)
    char *last_slash = strrchr(path, '/');
    
    // basename starts after the last slash, or at beginning if no slash
    char *base = (last_slash != NULL) ? last_slash + 1 : path;
    
    // Handle case where path ends with slash
    if (*base == '\0') {
        base = "/";
    }
    
    // Make a copy of the base string for possible modification
    char *result = strdup(base);
    if (!result) {
        fprintf(stderr, "Error: Memory allocation failed\n");
        return 1;
    }
    
    // If a suffix is provided, remove it
    if (argc == 3) {
        char *suffix = argv[2];
        size_t result_len = strlen(result);
        size_t suffix_len = strlen(suffix);
        
        // Check if the result ends with the suffix
        if (suffix_len > 0 && result_len > suffix_len) {
            if (strcmp(result + result_len - suffix_len, suffix) == 0) {
                result[result_len - suffix_len] = '\0';
            }
        }
    }
    
    // Output the result
    printf("%s\n", result);
    
    // Clean up
    free(result);
    
    return 0;
}
