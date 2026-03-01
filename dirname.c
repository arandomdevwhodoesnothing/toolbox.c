#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    // Check if we have at least one argument
    if (argc < 2) {
        fprintf(stderr, "Usage: %s NAME\n", argv[0]);
        return 1;
    }

    // Get the path (first argument)
    char *path = argv[1];
    
    // Handle empty string
    if (path[0] == '\0') {
        printf(".\n");
        return 0;
    }
    
    // Find the last slash
    char *last_slash = strrchr(path, '/');
    
    char *result;
    
    // Case 1: No slash found
    if (last_slash == NULL) {
        result = strdup(".");
    }
    // Case 2: Last slash is at the beginning (e.g., "/file" or "/")
    else if (last_slash == path) {
        // If it's just "/", dirname returns "/"
        if (path[1] == '\0') {
            result = strdup("/");
        } else {
            result = strdup("/");
        }
    }
    // Case 3: Normal case with slash in the middle
    else {
        // Remove trailing slashes
        char *end = last_slash;
        while (end > path && *(end - 1) == '/') {
            end--;
        }
        
        // If we stripped all slashes back to beginning
        if (end == path && *end == '/') {
            result = strdup("/");
        } else {
            // Allocate and copy the directory part
            size_t dir_len = end - path;
            result = malloc(dir_len + 1);
            if (!result) {
                fprintf(stderr, "Error: Memory allocation failed\n");
                return 1;
            }
            strncpy(result, path, dir_len);
            result[dir_len] = '\0';
            
            // If result is empty after stripping, use "."
            if (result[0] == '\0') {
                free(result);
                result = strdup(".");
            }
        }
    }
    
    // Output the result
    printf("%s\n", result);
    
    // Clean up
    free(result);
    
    return 0;
}
