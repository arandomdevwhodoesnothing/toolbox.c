#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>

#define VERSION "1.0.0"
#define PROGRAM_NAME "rmdir"

typedef struct {
    int ignore_non_empty;
    int parents;
    int verbose;
} Options;

void print_help(void) {
    printf("Usage: %s [OPTION]... DIRECTORY...\n", PROGRAM_NAME);
    printf("Remove the DIRECTORY(ies), if they are empty.\n\n");
    printf("      --ignore-fail-on-non-empty\n"
           "                  ignore each failure to remove a non-empty directory\n");
    printf("  -p, --parents   remove DIRECTORY and its ancestors;\n"
           "                  e.g., 'rmdir -p a/b' is similar to 'rmdir a/b a'\n");
    printf("  -v, --verbose   output a diagnostic for every directory processed\n");
    printf("      --help      display this help and exit\n");
    printf("      --version   output version information and exit\n");
}

void print_version(void) {
    printf("%s %s\n", PROGRAM_NAME, VERSION);
}

int remove_directory(const char *path, Options *opts) {
    int result = rmdir(path);
    
    if (result == 0) {
        if (opts->verbose) {
            printf("%s: removing directory, '%s'\n", PROGRAM_NAME, path);
        }
        return 0;
    }
    
    // Handle errors
    if (errno == ENOTEMPTY) {
        if (!opts->ignore_non_empty) {
            fprintf(stderr, "%s: failed to remove '%s': Directory not empty\n", 
                    PROGRAM_NAME, path);
        }
        return -1;
    } else if (errno == ENOENT) {
        fprintf(stderr, "%s: failed to remove '%s': No such file or directory\n", 
                PROGRAM_NAME, path);
        return -1;
    } else {
        fprintf(stderr, "%s: failed to remove '%s': %s\n", 
                PROGRAM_NAME, path, strerror(errno));
        return -1;
    }
}

int remove_with_parents(char *path, Options *opts) {
    char *path_copy = strdup(path);
    char *current_path = path_copy;
    int success = 0;
    
    // Create an array to store all paths we'll try to remove
    char **paths = NULL;
    int path_count = 0;
    int path_capacity = 0;
    
    // First, collect all parent paths
    while (1) {
        // Add current path to the list
        if (path_count >= path_capacity) {
            path_capacity = path_capacity ? path_capacity * 2 : 10;
            paths = realloc(paths, path_capacity * sizeof(char *));
        }
        paths[path_count++] = strdup(current_path);
        
        // Check if we've reached the root
        char *dir = dirname(current_path);
        if (strcmp(current_path, dir) == 0) {
            break;
        }
        current_path = dir;
    }
    
    // Remove directories in reverse order (deepest first)
    for (int i = path_count - 1; i >= 0; i--) {
        if (remove_directory(paths[i], opts) != 0) {
            success = -1;
            break;
        }
    }
    
    // Clean up
    for (int i = 0; i < path_count; i++) {
        free(paths[i]);
    }
    free(paths);
    free(path_copy);
    
    return success;
}

int main(int argc, char *argv[]) {
    Options opts = {0};
    int opt_index = 1;
    int exit_status = 0;
    
    if (argc < 2) {
        print_help();
        return 1;
    }
    
    // Parse options
    while (opt_index < argc && argv[opt_index][0] == '-') {
        char *arg = argv[opt_index];
        
        if (strcmp(arg, "--help") == 0) {
            print_help();
            return 0;
        } else if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(arg, "--ignore-fail-on-non-empty") == 0) {
            opts.ignore_non_empty = 1;
        } else if (strcmp(arg, "-p") == 0 || strcmp(arg, "--parents") == 0) {
            opts.parents = 1;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            opts.verbose = 1;
        } else if (arg[0] == '-' && arg[1] != '-') {
            // Handle combined short options (e.g., -pv)
            for (int i = 1; arg[i] != '\0'; i++) {
                switch (arg[i]) {
                    case 'p':
                        opts.parents = 1;
                        break;
                    case 'v':
                        opts.verbose = 1;
                        break;
                    default:
                        fprintf(stderr, "%s: invalid option -- '%c'\n", PROGRAM_NAME, arg[i]);
                        fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
                        return 1;
                }
            }
        } else {
            fprintf(stderr, "%s: unrecognized option '%s'\n", PROGRAM_NAME, arg);
            fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
            return 1;
        }
        opt_index++;
    }
    
    // Check if directories were specified
    if (opt_index >= argc) {
        fprintf(stderr, "%s: missing operand\n", PROGRAM_NAME);
        fprintf(stderr, "Try '%s --help' for more information.\n", PROGRAM_NAME);
        return 1;
    }
    
    // Process each directory
    for (int i = opt_index; i < argc; i++) {
        int result;
        
        if (opts.parents) {
            result = remove_with_parents(argv[i], &opts);
        } else {
            result = remove_directory(argv[i], &opts);
        }
        
        if (result != 0) {
            exit_status = 1;
        }
    }
    
    return exit_status;
}
