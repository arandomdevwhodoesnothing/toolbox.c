#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_PATH_LEN 4096

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s [OPTION]\n", prog_name);
    fprintf(stderr, "Print the full filename of the current working directory.\n\n");
    fprintf(stderr, "  -L, --logical   use PWD from environment, even if it contains symlinks\n");
    fprintf(stderr, "  -P, --physical  avoid all symlinks (default)\n");
    fprintf(stderr, "  --help          display this help and exit\n");
}

char *get_physical_path() {
    char *path = getcwd(NULL, 0);  // Let getcwd allocate the buffer
    return path;  // Returns physical path without symlinks
}

char *get_logical_path() {
    char *pwd = getenv("PWD");
    struct stat pwd_stat, dot_stat;
    
    if (!pwd) {
        return NULL;  // No PWD environment variable
    }
    
    // Verify that PWD matches the actual directory
    if (stat(pwd, &pwd_stat) == 0 && stat(".", &dot_stat) == 0) {
        if (pwd_stat.st_dev == dot_stat.st_dev && 
            pwd_stat.st_ino == dot_stat.st_ino) {
            // PWD points to the same inode as current directory
            return strdup(pwd);
        }
    }
    
    return NULL;  // PWD is invalid
}

int main(int argc, char *argv[]) {
    int use_logical = 0;  // Default to physical
    char *path = NULL;
    static char fallback_buffer[MAX_PATH_LEN];  // Static buffer for fallback
    int path_allocated = 0;  // Flag to track if path needs to be freed
    
    // Parse command line options
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-L") == 0 || strcmp(argv[i], "--logical") == 0) {
            use_logical = 1;
        }
        else if (strcmp(argv[i], "-P") == 0 || strcmp(argv[i], "--physical") == 0) {
            use_logical = 0;
        }
        else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "%s: invalid option -- '%s'\n", argv[0], argv[i]);
            print_usage(argv[0]);
            return 1;
        }
        else {
            fprintf(stderr, "%s: ignoring non-option argument '%s'\n", argv[0], argv[i]);
        }
    }
    
    // Get the path based on the option
    if (use_logical) {
        path = get_logical_path();
        if (path) {
            path_allocated = 1;  // strdup allocated memory
        } else {
            // Fall back to physical if logical fails
            path = get_physical_path();
            if (path) {
                path_allocated = 1;  // getcwd with NULL allocated memory
            }
        }
    } else {
        path = get_physical_path();
        if (path) {
            path_allocated = 1;  // getcwd with NULL allocated memory
        }
    }
    
    // If we still don't have a path, try the fallback buffer
    if (!path) {
        if (getcwd(fallback_buffer, sizeof(fallback_buffer)) != NULL) {
            path = fallback_buffer;
            path_allocated = 0;  // Using static buffer, don't free
        }
    }
    
    // Print the path
    if (path) {
        printf("%s\n", path);
        // Free the path if it was dynamically allocated
        if (path_allocated) {
            free(path);
        }
        return 0;
    } else {
        fprintf(stderr, "%s: error retrieving current directory: %s\n", 
                argv[0], strerror(errno));
        return 1;
    }
}
